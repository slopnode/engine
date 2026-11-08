#include "script/thing_script.hpp"
#include "core/sim_clock.hpp"
#include "game/sim_interpolation.hpp"
#include "net/net_state.hpp"
#include "script/s7_read.hpp"
#include "script/script_scope.hpp"

#include "assets/asset_services.hpp"
#include "assets/asset_store.hpp"
#include "assets/skeleton_loader.hpp"
#include "assets/sprite_anim_loader.hpp"
#include "game/game_state.hpp"
#include "map/bsp.hpp"
#include "map/nav_graph.hpp"
#include "map/nav_navmesh_build.hpp"
#include "map/pvs.hpp"
#include "map/area_gate_runtime.hpp"
#include "map/sound_propagation.hpp"
#include "map/thing.hpp"
#include "map/thing_def_registry.hpp"
#include "map/things_spawn.hpp"
#include "navigation/nav_components.hpp"
#include "physics/components.hpp"
#include "physics/motored_body.hpp"
#include "physics/physics_module.hpp"
#include "physics/rigid_mover.hpp"
#include "physics/sight_components.hpp"
#include "physics/sight_module.hpp"
#include "physics/trigger_components.hpp"
#include "render/animation_player.hpp"
#include "render/animation_systems.hpp"
#include "render/attachment_path.hpp"
#include "render/bone_attachment.hpp"
#include "render/bone_springs.hpp"
#include "render/components.hpp"
#include "render/debug_line_pool.hpp"
#include "render/dynamic_light.hpp"
#include "render/fx_local_light.hpp"
#include "render/gpu_skinning.hpp"
#include "render/model_hitboxes.hpp"
#include "render/skin_cut.hpp"
#include "physics/ragdoll_rig.hpp"
#include "render/sprite_animator.hpp"
#include "render/sprite_billboard.hpp"
#include "particles/components.hpp"
#include "particles/particle_module.hpp"
#include "particles/particle_sim.hpp"
#include "fx/trail.hpp"
#include "fx/tracer.hpp"
#include "render/transform.hpp"
#include "script/first_person_script.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <raylib.h>
#include <raymath.h>
#include <s7.h>

namespace slopengine {

namespace {

flecs::world* g_thingWorld = nullptr;

struct ThingDespawnQueue {
    std::vector<std::string> ids;
};

struct TimedDespawn {
    float age = 0.0f;
    float lifetime = 0.0f;
};

bool isProtectedThingId(std::string_view id) {
    return id == "Player" || id == "MapStatic";
}

std::string entityIdString(flecs::entity entity) {
    const char* name = entity.name();
    if (name != nullptr && name[0] != '\0') {
        return name;
    }
    return std::to_string(static_cast<std::uint64_t>(entity.id()));
}

std::unordered_map<std::string, flecs::entity_t> g_spawnedThings;

void rememberSpawnedThing(const std::string& id, flecs::entity entity) {
    for (auto it = g_spawnedThings.begin(); it != g_spawnedThings.end();) {
        const flecs::entity known = g_thingWorld->entity(it->second);
        if (!known.is_alive() || g_thingWorld->lookup(it->first.c_str()).is_valid()) {
            it = g_spawnedThings.erase(it);
        } else {
            ++it;
        }
    }
    g_spawnedThings[id] = entity.id();
}

flecs::entity lookupThing(const std::string& id) {
    if (g_thingWorld == nullptr || id.empty()) {
        return {};
    }
    flecs::entity entity = g_thingWorld->lookup(id.c_str());
    if (entity.is_valid()) {
        return entity;
    }
    const auto pending = g_spawnedThings.find(id);
    if (pending == g_spawnedThings.end()) {
        return {};
    }
    flecs::entity spawned = g_thingWorld->entity(pending->second);
    return spawned.is_alive() ? spawned : flecs::entity{};
}

flecs::entity lookupActor(std::string_view id) {
    if (g_thingWorld == nullptr || id.empty()) {
        return {};
    }
    flecs::entity entity = g_thingWorld->lookup(std::string(id).c_str());
    if (!entity.is_valid() || !entity.has<Actor>()) {
        return {};
    }
    return entity;
}

flecs::entity resolveActorPath(std::string_view path) {
    const auto split = splitAttachmentPath(path, [](std::string_view prefix) {
        return lookupActor(prefix).is_valid();
    });
    if (!split) {
        return {};
    }
    return descendAttachmentPath(lookupActor(split->anchor), split->names);
}

bool actorHasTag(const CollisionTags& tags, std::string_view tag) {
    for (const std::string& entry : tags.tags) {
        if (entry == tag) {
            return true;
        }
    }
    return false;
}

PhysicsWorld* physicsWorld() {
    if (g_thingWorld == nullptr || !g_thingWorld->has<PhysicsContext>()) {
        return nullptr;
    }
    return g_thingWorld->get_mut<PhysicsContext>().world;
}

s7_pointer g_thing_despawn(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "thing-despawn", args);
    const char* id = in.string("thing-id string");
    if (isProtectedThingId(id)) {
        return s7_f(sc);
    }

    flecs::entity entity = g_thingWorld->lookup(id);
    if (!entity.is_valid()) {
        return s7_f(sc);
    }

    if (!g_thingWorld->has<ThingDespawnQueue>()) {
        g_thingWorld->set<ThingDespawnQueue>({});
    }
    g_thingWorld->get_mut<ThingDespawnQueue>().ids.emplace_back(id);
    return s7_t(sc);
}

void registerFlushThingDespawns(flecs::world& world) {
    world.system("FlushThingDespawns")
        .kind(flecs::OnUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            if (!world.has<ThingDespawnQueue>()) {
                return;
            }

            ThingDespawnQueue& queue = world.get_mut<ThingDespawnQueue>();
            if (queue.ids.empty()) {
                return;
            }

            for (const std::string& id : queue.ids) {
                if (isProtectedThingId(id)) {
                    continue;
                }
                flecs::entity entity = world.lookup(id.c_str());
                if (entity.is_valid()) {
                    if (world.has<PhysicsContext>()) {
                        PhysicsWorld* physics = world.get_mut<PhysicsContext>().world;
                        if (physics != nullptr) {
                            const std::uint64_t eid = static_cast<std::uint64_t>(entity.id());
                            physics->destroyCharacter(eid);
                            physics->destroyKinematic(eid);
                        }
                    }
                    entity.destruct();
                }
            }
            queue.ids.clear();
        }).add<SimSystem>().add<CosmeticSystem>();
}

void registerTimedDespawnSystem(flecs::world& world) {
    world.component<TimedDespawn>();
    world.system<TimedDespawn>("TimedDespawnAdvance")
        .kind(flecs::OnUpdate)
        .each([](flecs::entity entity, TimedDespawn& timed) {
            flecs::world world = entity.world();
            if (isSimulationPaused(world)) {
                return;
            }
            const float dt = kSimTickDt;
            timed.age += dt;
            if (timed.lifetime > 0.0f && timed.age >= timed.lifetime) {
                queueThingDespawn(world, entityIdString(entity));
            }
        }).add<SimSystem>().add<CosmeticSystem>();
}

s7_pointer g_motored_spawn(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "motored-spawn", args);
    const std::string id = in.string("id string");
    const Vector3 at = in.vec3("x y z numbers");
    const Vector3 velocity = in.vec3("vx vy vz numbers");
    const std::string kind = in.string("kind string");
    const std::string path = in.string("path string");
    const float radius = in.optRealf(0.12f);
    const float gravity = in.optRealf(0.0f);
    const float lifetime = in.optRealf(8.0f);
    std::string onImpact = in.optString("");
    std::string ignoreId = in.optString("");

    if (id.empty() || isProtectedThingId(id)) {
        return s7_f(sc);
    }
    if (g_thingWorld->lookup(id.c_str()).is_valid()) {
        return s7_f(sc);
    }
    if (!g_thingWorld->has<AssetServices>() || g_thingWorld->get<AssetServices>().store == nullptr) {
        return s7_f(sc);
    }
    AssetStore& assets = *g_thingWorld->get_mut<AssetServices>().store;

    const bool isSprite = kind == "sprite";
    const bool isGeo = kind == "geo";
    if (isSprite == isGeo) {
        return s7_f(sc);
    }
    if (isSprite && !assets.hasSprite(path)) {
        TraceLog(LOG_WARNING, "motored-spawn: missing sprite '%s'", path.c_str());
        return s7_f(sc);
    }
    if (isGeo && !assets.hasGeo(path)) {
        TraceLog(LOG_WARNING, "motored-spawn: missing geo '%s'", path.c_str());
        return s7_f(sc);
    }

    flecs::entity entity = g_thingWorld->entity(id.c_str());
    LocalTransformation local{};
    local.position = at;
    local.scale = {1.0f, 1.0f, 1.0f};
    local.rotation = isGeo ? motoredFacing(velocity) : QuaternionIdentity();

    const float horiz = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
    const float facingYaw = horiz > 1.0e-4f ? std::atan2(velocity.x, velocity.z) : 0.0f;

    entity.add<WorldSpace>().add<MapOwned>().set<LocalTransformation>(local);

    if (isSprite) {
        SpriteInstance sprite{
            .sprite = path,
            .frame = "A",
            .facingYaw = facingYaw,
        };
        if (assets.hasSpriteAnim(path)) {
            const SpriteAnimBank* bank = assets.getSpriteAnimBank(path);
            if (bank != nullptr && bank->clipIndexByName.contains("fly")) {
                SpriteAnimator animator{};
                animator.animPath = path;
                playSpriteAnim(animator, sprite, bank, "fly", /*shouldLoop=*/true);
                entity.set<SpriteInstance>(sprite);
                entity.set<SpriteAnimator>(animator);
            } else {
                entity.set<SpriteInstance>(sprite);
            }
        } else {
            entity.set<SpriteInstance>(sprite);
        }
    } else {
        const Model source = assets.getGeoModel(path);
        Model model = cloneGeoModelInstance(source);
        if (model.meshCount <= 0) {
            entity.destruct();
            return s7_f(sc);
        }
        entity.set<Model3D>({model, WHITE});
    }

    rememberSpawnedThing(id, entity);
    entity.set<NetSpawnRecipe>({NetSpawnKind::Motored, isSprite, path, {}, {}});

    MotoredBody body{};
    body.velocity = velocity;
    body.gravity = gravity;
    body.radius = radius > 0.0f ? radius : 0.12f;
    body.lifetime = lifetime;
    body.age = 0.0f;
    body.onImpact = std::move(onImpact);
    body.ignoreId = std::move(ignoreId);
    entity.set<MotoredBody>(body);

    if (entity.has<LocalTransformation>() && entity.has<GlobalTransformation>()) {
        updateTransform(
            entity,
            entity.get_mut<LocalTransformation>(),
            entity.get_mut<GlobalTransformation>());
    }

    return s7_t(sc);
}

struct LightArgs {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float intensity = 1.0f;
    float range = 3.0f;
};

LightArgs readLightArgs(ArgReader& in) {
    LightArgs light{};
    light.r = in.realf("r number");
    light.g = in.realf("g number");
    light.b = in.realf("b number");
    light.intensity = in.realf("intensity number");
    light.range = in.realf("range number");
    return light;
}

FxLocalLight makeFxLocalLight(const LightArgs& args) {
    FxLocalLight light{};
    light.color.space = DynamicLightColorSpace::Rgb;
    light.color.value = {args.r, args.g, args.b};
    light.intensity = args.intensity;
    light.range = args.range;
    return light;
}

DynamicLight makePointDynamicLight(const LightArgs& args) {
    DynamicLight light{};
    light.kind = DynamicLightKind::Point;
    setDynamicLightRgb(light, {args.r, args.g, args.b});
    light.intensity = args.intensity;
    light.range = args.range;
    light.castShadows = false;
    return light;
}

s7_pointer g_fx_light_attach(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (g_thingWorld->has<Headless>()) {
        return s7_t(sc);
    }
    ArgReader in(sc, "fx-light-attach", args);
    const std::string id = in.string("id string");
    const LightArgs light = readLightArgs(in);

    flecs::entity entity = lookupThing(id);
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    entity.set<FxLocalLight>(makeFxLocalLight(light));
    return s7_t(sc);
}

s7_pointer g_dyn_light_attach(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "dyn-light-attach", args);
    const std::string id = in.string("id string");
    const LightArgs light = readLightArgs(in);

    flecs::entity entity = g_thingWorld->lookup(id.c_str());
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    entity.set<DynamicLight>(makePointDynamicLight(light));
    return s7_t(sc);
}

s7_pointer g_dyn_light_set_pos(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "dyn-light-set-pos!", args);
    const std::string id = in.string("id string");
    const Vector3 at = in.vec3("x y z numbers");

    flecs::entity entity = g_thingWorld->lookup(id.c_str());
    if (!entity.is_valid() || !entity.has<LocalTransformation>() ||
        !entity.has<GlobalTransformation>()) {
        return s7_f(sc);
    }
    LocalTransformation& local = entity.get_mut<LocalTransformation>();
    local.position = at;
    updateTransform(entity, local, entity.get_mut<GlobalTransformation>());
    return s7_t(sc);
}

s7_pointer g_dyn_light_set_hsv(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "dyn-light-set-hsv!", args);
    const std::string id = in.string("id string");
    const Vector3 hsv = in.vec3("h s v numbers");

    flecs::entity entity = g_thingWorld->lookup(id.c_str());
    if (!entity.is_valid() || !entity.has<DynamicLight>()) {
        return s7_f(sc);
    }
    DynamicLight light = entity.get<DynamicLight>();
    setDynamicLightHsv(light, hsv);
    entity.set<DynamicLight>(light);
    return s7_t(sc);
}

s7_pointer g_dyn_light_spawn(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (g_thingWorld->has<Headless>()) {
        return s7_t(sc);
    }
    ArgReader in(sc, "dyn-light-spawn", args);
    const std::string id = in.string("id string");
    const Vector3 at = in.vec3("x y z numbers");
    const LightArgs params = readLightArgs(in);
    const float lifetime = in.optRealf(0.0f);

    if (id.empty() || isProtectedThingId(id)) {
        return s7_f(sc);
    }
    if (g_thingWorld->lookup(id.c_str()).is_valid()) {
        return s7_f(sc);
    }

    DynamicLight light = makePointDynamicLight(params);
    if (lifetime > 0.0f) {
        light.castShadows = false;
    }
    flecs::entity entity = spawnDynamicLight(
        *g_thingWorld,
        id.c_str(),
        at,
        QuaternionIdentity(),
        light);
    entity.add<MapOwned>();
    if (lifetime > 0.0f) {
        entity.set<TimedDespawn>({.age = 0.0f, .lifetime = lifetime});
    }
    return s7_t(sc);
}

s7_pointer g_fx_light_spawn(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (g_thingWorld->has<Headless>()) {
        return s7_t(sc);
    }
    ArgReader in(sc, "fx-light-spawn", args);
    const std::string id = in.string("id string");
    const Vector3 at = in.vec3("x y z numbers");
    const LightArgs light = readLightArgs(in);
    const float lifetime = in.optRealf(0.0f);

    if (id.empty() || isProtectedThingId(id)) {
        return s7_f(sc);
    }
    if (g_thingWorld->lookup(id.c_str()).is_valid()) {
        return s7_f(sc);
    }

    flecs::entity entity = spawnFxLocalLight(*g_thingWorld, id.c_str(), at, makeFxLocalLight(light));
    entity.add<MapOwned>();
    if (lifetime > 0.0f) {
        entity.set<TimedDespawn>({.age = 0.0f, .lifetime = lifetime});
    }
    return s7_t(sc);
}

s7_pointer g_sprite_spawn(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "sprite-spawn", args);
    const std::string id = in.string("id string");
    const Vector3 at = in.vec3("x y z numbers");
    const std::string path = in.string("path string");
    const std::string clip = in.optString("");
    const float lifetime = in.optRealf(0.5f);

    if (id.empty() || isProtectedThingId(id)) {
        return s7_f(sc);
    }
    if (g_thingWorld->lookup(id.c_str()).is_valid()) {
        return s7_f(sc);
    }
    if (!g_thingWorld->has<AssetServices>() || g_thingWorld->get<AssetServices>().store == nullptr) {
        return s7_f(sc);
    }
    AssetStore& assets = *g_thingWorld->get_mut<AssetServices>().store;
    if (!assets.hasSprite(path)) {
        TraceLog(LOG_WARNING, "sprite-spawn: missing sprite '%s'", path.c_str());
        return s7_f(sc);
    }

    flecs::entity entity = g_thingWorld->entity(id.c_str());
    LocalTransformation local{};
    local.position = at;
    local.scale = {1.0f, 1.0f, 1.0f};
    local.rotation = QuaternionIdentity();

    SpriteInstance sprite{
        .sprite = path,
        .frame = "A",
        .facingYaw = 0.0f,
    };
    if (!clip.empty()) {
        SpriteAnimator animator{};
        animator.animPath = path;
        const SpriteAnimBank* bank = assets.getSpriteAnimBank(path);
        playSpriteAnim(animator, sprite, bank, clip, false);
        entity.add<WorldSpace>().add<MapOwned>().set<LocalTransformation>(local);
        entity.set<SpriteInstance>(sprite);
        entity.set<SpriteAnimator>(animator);
    } else {
        entity.add<WorldSpace>().add<MapOwned>().set<LocalTransformation>(local);
        entity.set<SpriteInstance>(sprite);
    }

    if (lifetime > 0.0f) {
        entity.set<TimedDespawn>({.age = 0.0f, .lifetime = lifetime});
    }

    return s7_t(sc);
}

s7_pointer g_particle_spawn(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (g_thingWorld->has<Headless>()) {
        return s7_t(sc);
    }
    ArgReader in(sc, "particle-spawn", args);
    const std::string id = in.string("id string");
    const Vector3 at = in.vec3("x y z numbers");
    const std::string path = in.string("path string");

    bool hasYaw = false;
    bool hasAim = false;
    float yaw = 0.0f;
    Vector3 aim{0.0f, 1.0f, 0.0f};
    if (in.nextIsNumber()) {
        const float first = in.realf();
        if (in.nextIsNumber()) {
            aim.x = first;
            aim.y = in.realf("dy number");
            aim.z = in.realf("dz number");
            hasAim = true;
        } else {
            yaw = first;
            hasYaw = true;
        }
    }

    if (id.empty() || isProtectedThingId(id)) {
        return s7_f(sc);
    }
    if (g_thingWorld->lookup(id.c_str()).is_valid()) {
        return s7_f(sc);
    }
    if (!g_thingWorld->has<AssetServices>() || g_thingWorld->get<AssetServices>().store == nullptr) {
        return s7_f(sc);
    }
    AssetStore& assets = *g_thingWorld->get_mut<AssetServices>().store;
    if (!assets.hasParticle(path)) {
        TraceLog(LOG_WARNING, "particle-spawn: missing system '%s'", path.c_str());
        return s7_f(sc);
    }

    flecs::entity entity{};
    if (hasAim) {
        entity = spawnParticleSystemAimed(
            *g_thingWorld,
            assets,
            id.c_str(),
            at,
            aim,
            path,
            true,
            true);
    } else {
        entity = spawnParticleSystem(
            *g_thingWorld,
            assets,
            id.c_str(),
            at,
            hasYaw ? yaw : 0.0f,
            path,
            true,
            true);
    }
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    if (const ParticleSystemAsset* asset = assets.getParticleAsset(path); asset != nullptr && !asset->loop) {
        entity.add<ParticleOneShot>();
    }
    if (entity.has<ParticleSystemInstance>() && entity.has<GlobalTransformation>()) {
        ParticleSystemInstance& instance = entity.get_mut<ParticleSystemInstance>();
        tickParticleSystemInstance(
            instance, assets, entity.get<GlobalTransformation>().matrix, 1.0e-3f, {});
    }
    return s7_t(sc);
}

s7_pointer g_particle_spawn_fp(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (g_thingWorld->has<Headless>()) {
        return s7_t(sc);
    }
    ArgReader in(sc, "particle-spawn-fp", args);
    const std::string id = in.string("id string");
    const std::string socket = in.string("socket string");
    const std::string path = in.string("path string");
    const std::string attach = in.string("attach string");
    const float depth = in.optRealf(0.35f);

    if (id.empty() || isProtectedThingId(id)) {
        return s7_f(sc);
    }
    if (g_thingWorld->lookup(id.c_str()).is_valid()) {
        return s7_f(sc);
    }
    if (!g_thingWorld->has<AssetServices>() || g_thingWorld->get<AssetServices>().store == nullptr) {
        return s7_f(sc);
    }
    AssetStore& assets = *g_thingWorld->get_mut<AssetServices>().store;
    if (!assets.hasParticle(path)) {
        TraceLog(LOG_WARNING, "particle-spawn-fp: missing system '%s'", path.c_str());
        return s7_f(sc);
    }

    flecs::entity host = findFirstPersonSocketSprite(*g_thingWorld, socket.c_str());
    if (!host.is_valid()) {
        TraceLog(LOG_WARNING, "particle-spawn-fp: no ViewSprite on socket '%s'", socket.c_str());
        return s7_f(sc);
    }

    flecs::entity entity = spawnParticleSystemFp(
        *g_thingWorld,
        assets,
        id.c_str(),
        host,
        attach,
        path,
        depth,
        true);
    return entity.is_valid() ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_particle_spawn_attach(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (g_thingWorld->has<Headless>()) {
        return s7_t(sc);
    }
    ArgReader in(sc, "particle-spawn-attach", args);
    const std::string id = in.string("id string");
    const std::string target = in.string("target string");
    const std::string path = in.string("path string");
    const std::string attach = in.string("attach string");

    if (id.empty() || isProtectedThingId(id)) {
        return s7_f(sc);
    }
    if (g_thingWorld->lookup(id.c_str()).is_valid()) {
        return s7_f(sc);
    }
    if (!g_thingWorld->has<AssetServices>() || g_thingWorld->get<AssetServices>().store == nullptr) {
        return s7_f(sc);
    }
    AssetStore& assets = *g_thingWorld->get_mut<AssetServices>().store;
    if (!assets.hasParticle(path)) {
        TraceLog(LOG_WARNING, "particle-spawn-attach: missing system '%s'", path.c_str());
        return s7_f(sc);
    }

    flecs::entity host = lookupActor(target);
    if (!host.is_valid()) {
        TraceLog(LOG_WARNING, "particle-spawn-attach: no actor '%s'", target.c_str());
        return s7_f(sc);
    }

    flecs::entity entity = spawnParticleSystemWorldAttach(
        *g_thingWorld,
        assets,
        id.c_str(),
        host,
        attach,
        path,
        true);
    return entity.is_valid() ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_particle_spawn_follow(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (g_thingWorld->has<Headless>()) {
        return s7_t(sc);
    }
    ArgReader in(sc, "particle-spawn-follow", args);
    std::string fields[3];
    const char* expected[3] = {"id string", "target string", "path string"};
    for (int index = 0; index < 3; ++index) {
        fields[index] = in.string(expected[index]);
    }
    float back = 0.0f;
    if (in.more()) {
        back = in.realf("back number");
    }
    if (fields[0].empty() || isProtectedThingId(fields[0]) || g_thingWorld->lookup(fields[0].c_str()).is_valid()) {
        return s7_f(sc);
    }
    if (!g_thingWorld->has<AssetServices>() || g_thingWorld->get<AssetServices>().store == nullptr) {
        return s7_f(sc);
    }
    AssetStore& assets = *g_thingWorld->get_mut<AssetServices>().store;
    if (!assets.hasParticle(fields[2])) {
        TraceLog(LOG_WARNING, "particle-spawn-follow: missing system '%s'", fields[2].c_str());
        return s7_f(sc);
    }
    flecs::entity host = lookupThing(fields[1]);
    if (!host.is_valid()) {
        return s7_f(sc);
    }
    flecs::entity entity =
        spawnParticleSystemFollow(*g_thingWorld, assets, fields[0].c_str(), host, back, fields[2], true);
    return entity.is_valid() ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_particle_spawn_bone(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (g_thingWorld->has<Headless>()) {
        return s7_t(sc);
    }
    ArgReader in(sc, "particle-spawn-bone", args);
    std::string fields[4];
    const char* expected[4] = {"id string", "target string", "bone string", "path string"};
    for (int index = 0; index < 4; ++index) {
        fields[index] = in.string(expected[index]);
    }
    bool fromParent = false;
    if (in.more()) {
        std::string mode;
        readName(in.peek(), mode);
        if (mode == "stump") {
            fromParent = true;
        } else if (mode != "limb") {
            return in.wrongType("'limb or 'stump");
        }
    }
    if (fields[0].empty() || g_thingWorld->lookup(fields[0].c_str()).is_valid()) {
        return s7_f(sc);
    }
    if (!g_thingWorld->has<AssetServices>() || g_thingWorld->get<AssetServices>().store == nullptr) {
        return s7_f(sc);
    }
    AssetStore& assets = *g_thingWorld->get_mut<AssetServices>().store;
    if (!assets.hasParticle(fields[3])) {
        TraceLog(LOG_WARNING, "particle-spawn-bone: missing system '%s'", fields[3].c_str());
        return s7_f(sc);
    }
    flecs::entity host = g_thingWorld->lookup(fields[1].c_str());
    flecs::entity entity = spawnParticleSystemBone(
        *g_thingWorld, assets, fields[0].c_str(), host, fields[2], fromParent, fields[3], true);
    return entity.is_valid() ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_trail_spawn(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (g_thingWorld->has<Headless>()) {
        return s7_t(sc);
    }
    ArgReader in(sc, "trail-spawn", args);
    const std::string id = in.string("id string");
    const Vector3 start = in.vec3("sx sy sz numbers");
    const Vector3 end = in.vec3("ex ey ez numbers");
    const std::string path = in.string("path string");
    const float lifetime = in.realf("lifetime number");
    const float width = in.optRealf(0.08f);

    if (id.empty() || isProtectedThingId(id)) {
        return s7_f(sc);
    }
    if (g_thingWorld->lookup(id.c_str()).is_valid()) {
        return s7_f(sc);
    }
    if (!g_thingWorld->has<AssetServices>() || g_thingWorld->get<AssetServices>().store == nullptr) {
        return s7_f(sc);
    }
    AssetStore& assets = *g_thingWorld->get_mut<AssetServices>().store;
    if (!assets.hasTexture(path)) {
        TraceLog(LOG_WARNING, "trail-spawn: missing texture '%s'", path.c_str());
        return s7_f(sc);
    }

    flecs::entity entity = spawnTrail(*g_thingWorld, id.c_str(), start, end, path, lifetime, width, true);
    return entity.is_valid() ? s7_t(sc) : s7_f(sc);
}

bool readTracerColor(s7_scheme* sc, s7_pointer value, Color& out) {
    int channels[4] = {out.r, out.g, out.b, out.a};
    int count = 0;
    for (; count < 4 && s7_is_pair(value); ++count) {
        if (!s7_is_number(s7_car(value))) {
            return false;
        }
        channels[count] = static_cast<int>(s7_number_to_real(sc, s7_car(value)));
        value = s7_cdr(value);
    }
    if (count < 3) {
        return false;
    }
    out = {
        static_cast<unsigned char>(std::clamp(channels[0], 0, 255)),
        static_cast<unsigned char>(std::clamp(channels[1], 0, 255)),
        static_cast<unsigned char>(std::clamp(channels[2], 0, 255)),
        static_cast<unsigned char>(std::clamp(channels[3], 0, 255)),
    };
    return true;
}

bool applyTracerStyleEntry(s7_scheme* sc, const char* key, s7_pointer value, TracerStyle& style, float& lead) {
    if (std::strcmp(key, "color") == 0) {
        return readTracerColor(sc, value, style.core);
    }
    if (std::strcmp(key, "glow-color") == 0) {
        return readTracerColor(sc, value, style.glow);
    }
    struct RealKey {
        const char* name;
        float* target;
    };
    const RealKey reals[] = {
        {"speed", &style.speed},
        {"length", &style.length},
        {"width", &style.width},
        {"glow-width", &style.glowWidth},
        {"min-pixels", &style.minPixels},
        {"tail-alpha", &style.tailAlpha},
        {"lead", &lead},
    };
    if (s7_is_pair(value) && s7_is_null(sc, s7_cdr(value))) {
        value = s7_car(value);
    }
    for (const RealKey& entry : reals) {
        if (std::strcmp(key, entry.name) == 0) {
            if (!s7_is_number(value)) {
                return false;
            }
            *entry.target = static_cast<float>(s7_number_to_real(sc, value));
            return true;
        }
    }
    return false;
}

s7_pointer g_tracer_spawn(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (g_thingWorld->has<Headless>()) {
        return s7_t(sc);
    }
    ArgReader in(sc, "tracer-spawn", args);
    const Vector3 start = in.vec3("sx sy sz numbers");
    const Vector3 end = in.vec3("ex ey ez numbers");
    TracerStyle style{};
    float lead = 0.0f;
    if (in.more()) {
        s7_pointer alist = in.alist("style alist");
        s7_pointer rejected = rejectedAlistEntry(alist, [&](const char* key, s7_pointer value) {
            return applyTracerStyleEntry(sc, key, value, style, lead);
        });
        if (rejected != nullptr) {
            return in.wrongTypeAt(7, rejected, "style alist");
        }
    }
    spawnTracer(*g_thingWorld, start, end, style, lead);
    return s7_t(sc);
}

s7_pointer g_trail_spawn_fp(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (g_thingWorld->has<Headless>()) {
        return s7_t(sc);
    }
    ArgReader in(sc, "trail-spawn-fp", args);
    const std::string id = in.string("id string");
    const std::string socket = in.string("socket string");
    const std::string path = in.string("path string");
    const std::string attach = in.string("attach string");
    const Vector3 end = in.vec3("ex ey ez numbers");
    const float lifetime = in.realf("lifetime number");
    const float width = in.optRealf(0.08f);
    const float depth = in.optRealf(0.35f);

    if (id.empty() || isProtectedThingId(id)) {
        return s7_f(sc);
    }
    if (g_thingWorld->lookup(id.c_str()).is_valid()) {
        return s7_f(sc);
    }
    if (!g_thingWorld->has<AssetServices>() || g_thingWorld->get<AssetServices>().store == nullptr) {
        return s7_f(sc);
    }
    AssetStore& assets = *g_thingWorld->get_mut<AssetServices>().store;
    if (!assets.hasTexture(path)) {
        TraceLog(LOG_WARNING, "trail-spawn-fp: missing texture '%s'", path.c_str());
        return s7_f(sc);
    }

    flecs::entity host = findFirstPersonSocketSprite(*g_thingWorld, socket.c_str());
    if (!host.is_valid()) {
        TraceLog(LOG_WARNING, "trail-spawn-fp: no ViewSprite on socket '%s'", socket.c_str());
        return s7_f(sc);
    }

    flecs::entity entity = spawnTrailFp(
        *g_thingWorld,
        id.c_str(),
        host,
        attach,
        depth,
        end,
        path,
        lifetime,
        width,
        true);
    return entity.is_valid() ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_particle_play(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "particle-play", args);
    if (g_thingWorld == nullptr || !in.nextIsString()) {
        return s7_f(sc);
    }
    flecs::entity entity = g_thingWorld->lookup(in.string());
    if (!entity.is_valid() || !entity.has<ParticleSystemInstance>()) {
        return s7_f(sc);
    }
    ParticleSystemInstance& instance = entity.get_mut<ParticleSystemInstance>();
    if (!instance.playing) {
        resetParticleSystemInstance(instance);
        instance.playing = true;
    }
    return s7_t(sc);
}

s7_pointer g_particle_stop(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "particle-stop", args);
    if (g_thingWorld == nullptr || !in.nextIsString()) {
        return s7_f(sc);
    }
    flecs::entity entity = g_thingWorld->lookup(in.string());
    if (!entity.is_valid() || !entity.has<ParticleSystemInstance>()) {
        return s7_f(sc);
    }
    entity.get_mut<ParticleSystemInstance>().playing = false;
    return s7_t(sc);
}

s7_pointer g_particle_despawn(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "particle-despawn", args);
    if (g_thingWorld == nullptr || !in.nextIsString()) {
        return s7_f(sc);
    }
    const std::string id = in.string();
    if (id.empty() || isProtectedThingId(id)) {
        return s7_f(sc);
    }
    flecs::entity entity = g_thingWorld->lookup(id.c_str());
    if (!entity.is_valid() || !entity.has<ParticleSystemInstance>()) {
        return s7_f(sc);
    }
    entity.destruct();
    return s7_t(sc);
}

constexpr SchemeBinding kSpawnBindings[] = {
    {"thing-despawn", g_thing_despawn, 1, 0, false, "(thing-despawn id)", ScriptCap::WorldMutate},
    {"motored-spawn", g_motored_spawn, 9, 5, false, "(motored-spawn id x y z vx vy vz kind path [radius gravity lifetime on-impact ignore])", ScriptCap::WorldMutate},
    {"sprite-spawn", g_sprite_spawn, 5, 2, false, "(sprite-spawn id x y z path [clip] [lifetime])", ScriptCap::WorldMutate},
    {"particle-spawn", g_particle_spawn, 5, 3, false, "(particle-spawn id x y z path [yaw | dx dy dz])", ScriptCap::WorldMutate},
    {"particle-spawn-fp", g_particle_spawn_fp, 4, 1, false, "(particle-spawn-fp id socket path attach [depth])", ScriptCap::WorldMutate},
    {"particle-spawn-attach", g_particle_spawn_attach, 4, 0, false, "(particle-spawn-attach id target path attach)", ScriptCap::WorldMutate},
    {"particle-spawn-follow", g_particle_spawn_follow, 3, 1, false, "(particle-spawn-follow id target path [back])", ScriptCap::WorldMutate},
    {"particle-spawn-bone", g_particle_spawn_bone, 4, 1, false, "(particle-spawn-bone id target bone path ['limb|'stump])", ScriptCap::WorldMutate},
    {"trail-spawn-fp", g_trail_spawn_fp, 8, 2, false, "(trail-spawn-fp id socket path attach ex ey ez lifetime [width] [depth])", ScriptCap::WorldMutate},
    {"trail-spawn", g_trail_spawn, 9, 1, false, "(trail-spawn id sx sy sz ex ey ez path lifetime [width])", ScriptCap::WorldMutate},
    {"tracer-spawn", g_tracer_spawn, 6, 1, false, "(tracer-spawn sx sy sz ex ey ez [style])", ScriptCap::WorldMutate},
    {"particle-play", g_particle_play, 1, 0, false, "(particle-play id)", ScriptCap::WorldMutate},
    {"particle-stop", g_particle_stop, 1, 0, false, "(particle-stop id)", ScriptCap::WorldMutate},
    {"particle-despawn", g_particle_despawn, 1, 0, false, "(particle-despawn id)", ScriptCap::WorldMutate},
    {"fx-light-spawn", g_fx_light_spawn, 9, 1, false, "(fx-light-spawn id x y z r g b intensity range [lifetime])", ScriptCap::WorldMutate},
    {"fx-light-attach", g_fx_light_attach, 6, 0, false, "(fx-light-attach id r g b intensity range)", ScriptCap::WorldMutate},
    {"dyn-light-spawn", g_dyn_light_spawn, 9, 1, false, "(dyn-light-spawn id x y z r g b intensity range [lifetime])", ScriptCap::WorldMutate},
    {"dyn-light-attach", g_dyn_light_attach, 6, 3, false, "(dyn-light-attach id r g b intensity range)", ScriptCap::WorldMutate},
    {"dyn-light-set-pos!", g_dyn_light_set_pos, 4, 0, false, "(dyn-light-set-pos! id x y z)", ScriptCap::WorldMutate},
    {"dyn-light-set-hsv!", g_dyn_light_set_hsv, 4, 0, false, "(dyn-light-set-hsv! id h s v)", ScriptCap::WorldMutate},
};

s7_pointer g_actor_spawn(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "actor-spawn", args);
    const std::string id = in.string("id string");
    const Vector3 at = in.vec3("x y z numbers");
    const float yaw = in.realf("yaw number");
    const std::string kind = in.string("kind string");
    const std::string path = in.string("path string");
    const float radius = in.optRealf(0.3f);
    const float height = in.optRealf(1.1f);
    const float speed = in.optRealf(6.0f);
    const float gravity = in.optRealf(9.81f);

    std::vector<std::string> tags;
    for (s7_pointer cursor = in.optList(s7_nil(sc)); s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        if (s7_is_string(s7_car(cursor))) {
            tags.emplace_back(s7_string(s7_car(cursor)));
        }
    }
    if (tags.empty()) {
        tags.emplace_back("actor");
    }

    if (id.empty() || isProtectedThingId(id)) {
        return s7_f(sc);
    }
    if (g_thingWorld->lookup(id.c_str()).is_valid()) {
        return s7_f(sc);
    }
    if (!g_thingWorld->has<AssetServices>() || g_thingWorld->get<AssetServices>().store == nullptr) {
        return s7_f(sc);
    }
    AssetStore& assets = *g_thingWorld->get_mut<AssetServices>().store;

    const bool isSprite = kind == "sprite";
    const bool isGeo = kind == "geo";
    if (isSprite == isGeo) {
        return s7_f(sc);
    }
    if (isSprite && !assets.hasSprite(path)) {
        TraceLog(LOG_WARNING, "actor-spawn: missing sprite '%s'", path.c_str());
        return s7_f(sc);
    }
    if (isGeo && !assets.hasGeo(path)) {
        TraceLog(LOG_WARNING, "actor-spawn: missing geo '%s'", path.c_str());
        return s7_f(sc);
    }

    flecs::entity entity = g_thingWorld->entity(id.c_str());
    LocalTransformation local{};
    local.position = at;
    local.scale = {1.0f, 1.0f, 1.0f};
    local.rotation = QuaternionFromAxisAngle({0.0f, 1.0f, 0.0f}, yaw);
    entity.add<WorldSpace>().add<MapOwned>().set<LocalTransformation>(local);

    if (isSprite) {
        entity.set<SpriteInstance>({
            .sprite = path,
            .frame = "A",
            .facingYaw = yaw,
        });
    } else {
        const Model source = assets.getGeoModel(path);
        Model model = cloneGeoModelInstance(source);
        if (model.meshCount <= 0) {
            entity.destruct();
            return s7_f(sc);
        }
        entity.set<Model3D>({model, WHITE});
        entity.set<GeoSource>({path});
        if (model.skeleton.boneCount > 0) {
            attachGeoAnimationPlayer(entity, assets, path, {}, {}, true);
            entity.set<HiddenBones>(
                {std::vector<std::uint8_t>(static_cast<std::size_t>(model.skeleton.boneCount), 0), false});
            entity.set<ModelHitboxes>({});
        }
    }

    CharacterMotor motor{};
    motor.radius = radius > 0.0f ? radius : 0.3f;
    motor.height = height > 0.0f ? height : 1.1f;
    motor.moveSpeed = speed;
    motor.gravity = gravity;
    entity.set<NetSpawnRecipe>({NetSpawnKind::Actor, isSprite, path, tags, {}});
    entity.add<Actor>().set<CharacterMotor>(motor).set<NavigationAgent>(NavigationAgent{}).set<CollisionTags>(CollisionTags{std::move(tags)});

    PhysicsWorld* physics = physicsWorld();
    if (physics != nullptr) {
        physics->createCharacter(static_cast<std::uint64_t>(entity.id()), at.x, at.y, at.z, motor);
    }

    return s7_t(sc);
}

s7_pointer entityPosition(s7_scheme* sc, flecs::entity entity) {
    if (!entity.is_valid() || !entity.has<LocalTransformation>()) {
        return s7_f(sc);
    }
    return makeVec3List(sc, entity.get<LocalTransformation>().position);
}

s7_pointer entityYaw(s7_scheme* sc, flecs::entity entity) {
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    if (entity.has<SpriteInstance>()) {
        return s7_make_real(sc, entity.get<SpriteInstance>().facingYaw);
    }
    if (entity.has<LocalTransformation>()) {
        const Vector3 euler = QuaternionToEuler(entity.get<LocalTransformation>().rotation);
        return s7_make_real(sc, euler.y);
    }
    return s7_f(sc);
}

s7_pointer g_actor_pos(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-pos", args);
    return entityPosition(sc, lookupActor(in.string("id string")));
}

s7_pointer g_actor_yaw(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-yaw", args);
    return entityYaw(sc, lookupActor(in.string("id string")));
}

s7_pointer g_thing_pos(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "thing-pos", args);
    return entityPosition(sc, g_thingWorld->lookup(in.string("id string")));
}

s7_pointer g_thing_yaw(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "thing-yaw", args);
    return entityYaw(sc, g_thingWorld->lookup(in.string("id string")));
}

s7_pointer g_actor_set_corpse(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-set-corpse!", args);
    flecs::entity entity = lookupActor(in.string("id string"));
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    if (entity.has<ActorCorpse>()) {
        return s7_t(sc);
    }

    if (entity.has<CharacterMotor>()) {
        CharacterMotor& motor = entity.get_mut<CharacterMotor>();
        motor.wishX = 0.0f;
        motor.wishZ = 0.0f;
    }
    if (entity.has<ActorSight>()) {
        entity.remove<ActorSight>();
    }
    entity.add<ActorCorpse>();
    return s7_t(sc);
}

s7_pointer g_sprite_hide_part(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "sprite-hide-part!", args);
    const char* id = in.string("id string");
    const std::string partName = in.string("part-name string");
    flecs::entity entity = lookupActor(id);
    if (!entity.is_valid() || !entity.has<SpriteInstance>()) {
        return s7_f(sc);
    }
    if (!g_thingWorld->has<AssetServices>() || g_thingWorld->get<AssetServices>().store == nullptr) {
        return s7_f(sc);
    }
    AssetStore& assets = *g_thingWorld->get_mut<AssetServices>().store;
    const SpriteAsset* asset = assets.getSpriteAsset(entity.get<SpriteInstance>().sprite);
    if (asset == nullptr) {
        return s7_f(sc);
    }

    int partIndex = -1;
    for (std::size_t i = 0; i < asset->hitParts.size(); ++i) {
        if (asset->hitParts[i].name == partName) {
            partIndex = static_cast<int>(i);
            break;
        }
    }
    if (partIndex < 0 || partIndex >= 32) {
        return s7_f(sc);
    }

    std::uint32_t mask = entity.has<SpriteHiddenParts>() ? entity.get<SpriteHiddenParts>().mask : 0;
    mask |= (1u << static_cast<unsigned>(partIndex));
    entity.set<SpriteHiddenParts>({mask});
    return s7_t(sc);
}

s7_pointer g_actor_set_wish(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-set-wish", args);
    const char* id = in.string("id string");
    const float wx = in.realf("wx number");
    const float wz = in.realf("wz number");
    flecs::entity entity = lookupActor(id);
    if (!entity.is_valid() || !entity.has<CharacterMotor>()) {
        return s7_f(sc);
    }
    CharacterMotor& motor = entity.get_mut<CharacterMotor>();
    motor.wishX = wx;
    motor.wishZ = wz;
    motor.wishY = 0.0f;
    return s7_t(sc);
}

s7_pointer g_actor_set_wish_3d(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-set-wish-3d", args);
    const char* id = in.string("id string");
    const Vector3 wish = in.vec3("wx wy wz numbers");
    flecs::entity entity = lookupActor(id);
    if (!entity.is_valid() || !entity.has<CharacterMotor>()) {
        return s7_f(sc);
    }
    CharacterMotor& motor = entity.get_mut<CharacterMotor>();
    motor.wishX = wish.x;
    motor.wishY = wish.y;
    motor.wishZ = wish.z;
    return s7_t(sc);
}

s7_pointer g_actor_set_move_speed(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-set-move-speed!", args);
    const char* id = in.string("id string");
    const float speed = in.realf("speed number");
    flecs::entity entity = lookupActor(id);
    if (!entity.is_valid() || !entity.has<CharacterMotor>()) {
        return s7_f(sc);
    }
    CharacterMotor& motor = entity.get_mut<CharacterMotor>();
    motor.moveSpeed = speed;
    return s7_t(sc);
}

s7_pointer g_actor_set_yaw(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-set-yaw!", args);
    const char* id = in.string("id string");
    const float yaw = in.realf("yaw number");
    flecs::entity entity = lookupActor(id);
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    if (entity.has<SpriteInstance>()) {
        entity.get_mut<SpriteInstance>().facingYaw = yaw;
    }
    if (entity.has<LocalTransformation>()) {
        entity.get_mut<LocalTransformation>().rotation = QuaternionFromAxisAngle({0.0f, 1.0f, 0.0f}, yaw);
    }
    return s7_t(sc);
}

flecs::entity boneScaleActor(ArgReader& in, std::string& bone) {
    const char* id = in.string("id string");
    bone = in.string("bone name string");
    flecs::entity entity = lookupActor(id);
    if (!entity.is_valid() || !entity.has<Model3D>() ||
        findModelBoneIndex(entity.get<Model3D>().model, bone) < 0) {
        return {};
    }
    return entity;
}

s7_pointer g_actor_set_bone_scale(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-set-bone-scale!", args);
    std::string bone;
    flecs::entity entity = boneScaleActor(in, bone);
    BoneScale scale{};
    scale.size = in.realf("positive size");
    if (scale.size <= 0.0f) {
        return in.wrongTypeAt(3, s7_make_real(sc, scale.size), "positive size");
    }
    if (in.more()) {
        scale.length = in.realf("positive length");
        if (scale.length <= 0.0f) {
            return in.wrongTypeAt(4, s7_make_real(sc, scale.length), "positive length");
        }
    }
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    if (!entity.has<BoneScales>()) {
        entity.set<BoneScales>({});
    }
    BoneScales& scales = entity.get_mut<BoneScales>();
    auto existing = std::find_if(scales.named.begin(), scales.named.end(), [&](const auto& entry) {
        return entry.first == bone;
    });
    const bool identity = scale.size == 1.0f && scale.length == 1.0f;
    if (existing != scales.named.end()) {
        if (identity) {
            scales.named.erase(existing);
        } else {
            existing->second = scale;
        }
    } else if (!identity) {
        scales.named.emplace_back(bone, scale);
    }
    scales.dirty = true;
    return s7_t(sc);
}

s7_pointer g_actor_bone_scale(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-bone-scale", args);
    std::string bone;
    flecs::entity entity = boneScaleActor(in, bone);
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    BoneScale scale{};
    if (entity.has<BoneScales>()) {
        for (const auto& [name, value] : entity.get<BoneScales>().named) {
            if (name == bone) {
                scale = value;
            }
        }
    }
    return s7_list(sc, 2, s7_make_real(sc, scale.size), s7_make_real(sc, scale.length));
}

s7_pointer g_actor_clear_bone_scales(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-clear-bone-scales!", args);
    flecs::entity entity = lookupActor(in.string("id string"));
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    if (entity.has<BoneScales>()) {
        BoneScales& scales = entity.get_mut<BoneScales>();
        scales.named.clear();
        scales.dirty = true;
    }
    return s7_t(sc);
}

std::unordered_map<flecs::entity_t, ShaderParams> g_pendingShaderParams;

s7_pointer g_actor_set_shader_param(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-set-shader-param!", args);
    const char* id = in.string("id string");
    if (!in.nextIsString() || s7_string(in.peek())[0] == '\0') {
        return in.wrongType("uniform name string");
    }
    const std::string name = in.string();
    float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 4 && in.more(); ++i) {
        values[i] = in.realf("number");
    }
    flecs::entity entity = lookupActor(id);
    if (!entity.is_valid() || !entity.has<Model3D>()) {
        return s7_f(sc);
    }
    const Vector4 value{values[0], values[1], values[2], values[3]};
    const auto upsert = [&](ShaderParams& params) {
        auto existing = std::find_if(params.values.begin(), params.values.end(), [&](const auto& entry) {
            return entry.first == name;
        });
        if (existing != params.values.end()) {
            existing->second = value;
        } else {
            params.values.emplace_back(name, value);
        }
    };
    if (entity.has<ShaderParams>()) {
        g_pendingShaderParams.erase(entity.id());
        upsert(entity.get_mut<ShaderParams>());
    } else {
        ShaderParams& pending = g_pendingShaderParams[entity.id()];
        upsert(pending);
        entity.set<ShaderParams>(pending);
    }
    return s7_t(sc);
}

s7_pointer g_actor_clear_shader_params(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-clear-shader-params!", args);
    flecs::entity entity = lookupActor(in.string("id string"));
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    g_pendingShaderParams.erase(entity.id());
    entity.remove<ShaderParams>();
    return s7_t(sc);
}

constexpr int kMaxSeveredPieces = 48;

Matrix poseTransformMatrix(const Transform& pose) {
    return MatrixMultiply(
        MatrixMultiply(MatrixScale(pose.scale.x, pose.scale.y, pose.scale.z), QuaternionToMatrix(pose.rotation)),
        MatrixTranslate(pose.translation.x, pose.translation.y, pose.translation.z));
}

Transform transformFromPoseMatrix(const Matrix& matrix) {
    Transform pose{};
    MatrixDecompose(matrix, &pose.translation, &pose.rotation, &pose.scale);
    return pose;
}

flecs::entity riggedActor(ArgReader& in, std::string& bone) {
    const char* id = in.string("id string");
    bone = in.string("bone string");
    if (!in.ok()) {
        return {};
    }
    flecs::entity entity = lookupActor(id);
    if (!entity.is_valid() || !entity.has<Model3D>() || !entity.has<GlobalTransformation>()) {
        return {};
    }
    const Model& model = entity.get<Model3D>().model;
    if (model.skeleton.boneCount <= 0 || model.boneMatrices == nullptr || model.currentPose == nullptr) {
        return {};
    }
    sampleTickPose(entity);
    return entity;
}

HiddenBones* actorHiddenBones(flecs::entity entity, int boneCount) {
    if (!entity.has<HiddenBones>()) {
        return nullptr;
    }
    HiddenBones& hidden = entity.get_mut<HiddenBones>();
    if (hidden.mask.size() != static_cast<std::size_t>(boneCount)) {
        hidden.mask.assign(static_cast<std::size_t>(boneCount), 0);
    }
    return &hidden;
}

const std::vector<BoneHitbox>& actorHitboxes(
    flecs::entity entity,
    const Model& model,
    std::vector<BoneHitbox>& scratch) {
    sampleTickPose(entity);
    if (entity.has<ModelHitboxes>()) {
        return ensureModelHitboxes(model, entity.get_mut<ModelHitboxes>());
    }
    scratch = buildBoneHitboxes(model);
    return scratch;
}

const Matrix& refreshedGlobal(flecs::entity entity) {
    GlobalTransformation& global = entity.get_mut<GlobalTransformation>();
    if (entity.has<LocalTransformation>()) {
        updateTransform(entity, entity.get_mut<LocalTransformation>(), global);
    }
    return global.matrix;
}

void trimSeveredPieces(PhysicsWorld* physics) {
    std::vector<std::pair<float, flecs::entity>> pieces;
    g_thingWorld->each([&](flecs::entity entity, const DebrisBody& debris) {
        pieces.emplace_back(debris.age, entity);
    });
    if (static_cast<int>(pieces.size()) < kMaxSeveredPieces) {
        return;
    }
    std::sort(pieces.begin(), pieces.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    const std::size_t excess = pieces.size() - static_cast<std::size_t>(kMaxSeveredPieces) + 1;
    for (std::size_t i = 0; i < excess; ++i) {
        if (physics != nullptr) {
            physics->destroyDebris(static_cast<std::uint64_t>(pieces[i].second.id()));
        }
        destroyAttachmentTree(pieces[i].second);
    }
}

const Material& stumpMaterial(AssetStore& assets) {
    static AssetStore* owner = nullptr;
    static Material material{};
    if (owner != &assets) {
        material = assets.resolveMaterial("fx/stump");
        owner = &assets;
    }
    return material;
}

float randomSigned() {
    return static_cast<float>(GetRandomValue(-1000, 1000)) / 1000.0f;
}

flecs::entity severActorPartImpl(flecs::entity entity, const std::string& boneName, Vector3 push, float speed, float spin,
                             float lifetime, bool makePiece) {
    if (g_thingWorld == nullptr || !entity.is_valid() || !entity.has<GeoSource>() || !entity.has<Model3D>() ||
        !entity.has<GlobalTransformation>() || !g_thingWorld->has<AssetServices>() ||
        g_thingWorld->get<AssetServices>().store == nullptr) {
        return {};
    }
    {
        const Model& rig = entity.get<Model3D>().model;
        if (rig.skeleton.boneCount <= 0 || rig.boneMatrices == nullptr || rig.currentPose == nullptr) {
            return {};
        }
    }
    sampleTickPose(entity);
    const Model& model = entity.get<Model3D>().model;
    const int boneCount = model.skeleton.boneCount;
    const int boneIndex = findModelBoneIndex(model, boneName);
    if (boneIndex < 0) {
        return {};
    }
    HiddenBones* hiddenPtr = actorHiddenBones(entity, boneCount);
    if (hiddenPtr == nullptr || hiddenPtr->mask[static_cast<std::size_t>(boneIndex)] != 0) {
        return {};
    }
    HiddenBones& hidden = *hiddenPtr;
    std::vector<int> subtree;
    for (int bone : boneSubtree(model, boneIndex)) {
        if (hidden.mask[static_cast<std::size_t>(bone)] == 0) {
            subtree.push_back(bone);
        }
    }
    std::vector<BoneHitbox> scratch;
    const std::vector<BoneHitbox>& boxes = actorHitboxes(entity, model, scratch);
    const Matrix world = refreshedGlobal(entity);
    BoundingBox bounds{};
    if (!posedBonesBounds(model, boxes, world, subtree, bounds)) {
        return {};
    }
    const Vector3 center = Vector3Scale(Vector3Add(bounds.min, bounds.max), 0.5f);
    const Vector3 half = Vector3Scale(Vector3Subtract(bounds.max, bounds.min), 0.5f);

    AssetStore& assets = *g_thingWorld->get_mut<AssetServices>().store;
    std::vector<std::uint8_t> inside(static_cast<std::size_t>(boneCount), 0);
    for (int bone : subtree) {
        inside[static_cast<std::size_t>(bone)] = 1;
    }
    if (!makePiece) {
        for (int bone : subtree) {
            hidden.mask[static_cast<std::size_t>(bone)] = 1;
        }
        hidden.dirty = true;
        cutSkinnedModel(
            entity.get_mut<Model3D>().model,
            inside,
            false,
            std::max(model.skeleton.bones[boneIndex].parent, 0),
            stumpMaterial(assets));
        return entity;
    }
    const std::string geoPath = entity.get<GeoSource>().path;
    Model piece = cloneGeoModelInstance(model);
    if (piece.meshCount <= 0 || static_cast<int>(piece.skeleton.boneCount) != boneCount || piece.boneMatrices == nullptr) {
        unloadClonedGeoModelInstance(piece);
        return {};
    }
    cutSkinnedModel(piece, inside, true, boneIndex, stumpMaterial(assets));
    const Vector3 cutJoint = model.currentPose[boneIndex].translation;
    for (int other = 0; other < boneCount; ++other) {
        if (inside[static_cast<std::size_t>(other)] != 0) {
            piece.boneMatrices[other] = model.boneMatrices[other];
            piece.currentPose[other] = model.currentPose[other];
        } else {
            piece.boneMatrices[other] = collapsedBoneMatrix(cutJoint);
        }
    }

    Vector3 direction = push;
    if (Vector3LengthSqr(direction) > 1e-8f) {
        direction = Vector3Normalize(direction);
    }
    const Vector3 velocity = Vector3Add(Vector3Scale(direction, speed), Vector3{0.0f, speed * 0.35f, 0.0f});
    Vector3 angular{randomSigned() * spin * 0.35f, randomSigned() * spin * 0.35f, randomSigned() * spin * 0.35f};
    const Vector3 tumbleAxis = Vector3CrossProduct({0.0f, 1.0f, 0.0f}, direction);
    if (Vector3LengthSqr(tumbleAxis) > 1e-6f) {
        angular = Vector3Add(angular, Vector3Scale(Vector3Normalize(tumbleAxis), spin));
    } else {
        angular = Vector3Scale(angular, 1.0f / 0.35f);
    }

    PhysicsWorld* physics = physicsWorld();
    trimSeveredPieces(physics);

    const std::string pieceName = "severed-" + std::string(entity.name().c_str()) + "-" + boneName;
    LocalTransformation local{};
    MatrixDecompose(world, &local.position, &local.rotation, &local.scale);
    GlobalTransformation global{};
    global.matrix = world;
    Model3D pieceModel{piece, WHITE};
    pieceModel.boundsLift = -2.0f;
    flecs::entity pieceEntity = g_thingWorld->entity(pieceName.c_str())
                                    .add<WorldSpace>()
                                    .add<MapOwned>()
                                    .set<LocalTransformation>(local)
                                    .set<GlobalTransformation>(global)
                                    .set<Model3D>(pieceModel)
                                    .set<GeoSource>({geoPath})
                                    .set<DebrisBody>({lifetime, 0.0f});

    std::vector<flecs::entity> carried;
    entity.children([&](flecs::entity child) {
        if (!child.has<BoneAttachment>()) {
            return;
        }
        const BoneAttachment& attachment = child.get<BoneAttachment>();
        const int attachedBone = attachment.boneIndex >= 0 ? attachment.boneIndex
                                                           : findModelBoneIndex(model, attachment.bone);
        if (attachedBone >= 0 && inside[static_cast<std::size_t>(attachedBone)] != 0) {
            carried.push_back(child);
        }
    });
    for (flecs::entity child : carried) {
        child.child_of(pieceEntity);
    }

    for (int bone : subtree) {
        hidden.mask[static_cast<std::size_t>(bone)] = 1;
    }
    hidden.dirty = true;
    cutSkinnedModel(
        entity.get_mut<Model3D>().model,
        inside,
        false,
        std::max(model.skeleton.bones[boneIndex].parent, 0),
        stumpMaterial(assets));

    const bool ragdolled = physics != nullptr &&
        startEntityRagdoll(
            pieceEntity, piece, world, *physics, boxes, inside, inside, velocity, angular, -1, {}, lifetime);
    if (!ragdolled) {
        const Matrix recenter = MatrixMultiply(world, MatrixTranslate(-center.x, -center.y, -center.z));
        for (int bone : subtree) {
            piece.boneMatrices[bone] = MatrixMultiply(piece.boneMatrices[bone], recenter);
            piece.currentPose[bone] =
                transformFromPoseMatrix(MatrixMultiply(poseTransformMatrix(piece.currentPose[bone]), recenter));
        }
        LocalTransformation boxLocal{};
        boxLocal.position = center;
        pieceEntity.set<LocalTransformation>(boxLocal);
        pieceEntity.set<GlobalTransformation>({MatrixTranslate(center.x, center.y, center.z)});
        if (physics != nullptr) {
            physics->createDebrisBox(
                static_cast<std::uint64_t>(pieceEntity.id()), center, half, QuaternionIdentity(), velocity, angular);
        }
    }
    if (!modelUsesGpuSkinning(piece)) {
        updateSkinnedMeshVertexBuffers(piece);
    }
    return pieceEntity;
}


s7_pointer g_actor_sever_bone(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-sever-bone!", args);
    std::string boneName;
    flecs::entity entity = riggedActor(in, boneName);
    if (!in.ok()) {
        return in.error();
    }
    Vector3 push{0.0f, 0.0f, 0.0f};
    float speed = 3.0f;
    float spin = 6.0f;
    float lifetime = 20.0f;
    if (in.more()) {
        push = in.vec3("dx dy dz numbers");
    }
    if (in.more()) {
        speed = in.realf("speed number");
    }
    if (in.more()) {
        spin = in.realf("spin number");
    }
    if (in.more()) {
        lifetime = in.realf("lifetime number");
    }
    if (!in.ok()) {
        return in.error();
    }
    const flecs::entity piece = severActorPartImpl(entity, boneName, push, speed, spin, lifetime, true);
    if (!piece.is_valid()) {
        return s7_f(sc);
    }
    NetSevered severed = entity.has<NetSevered>() ? entity.get<NetSevered>() : NetSevered{};
    severed.bones.push_back(boneName);
    entity.set<NetSevered>(std::move(severed));
    queueNetEffect(*g_thingWorld, entity,
                   NetEffect{NetEffectKind::Sever, 0, boneName, {push.x, push.y, push.z}, speed, spin, lifetime});
    return s7_make_string(sc, piece.name().c_str());
}

bool hideActorBoneImpl(flecs::entity entity, const std::string& boneName) {
    if (!entity.is_valid() || !entity.has<Model3D>()) {
        return false;
    }
    const Model& model = entity.get<Model3D>().model;
    const int boneIndex = findModelBoneIndex(model, boneName);
    HiddenBones* hidden = actorHiddenBones(entity, model.skeleton.boneCount);
    if (boneIndex < 0 || hidden == nullptr || hidden->mask[static_cast<std::size_t>(boneIndex)] != 0) {
        return false;
    }
    for (int bone : boneSubtree(model, boneIndex)) {
        hidden->mask[static_cast<std::size_t>(bone)] = 1;
    }
    hidden->dirty = true;
    return true;
}

s7_pointer g_actor_hide_bone(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-hide-bone!", args);
    std::string boneName;
    flecs::entity entity = riggedActor(in, boneName);
    if (!in.ok()) {
        return in.error();
    }
    if (!hideActorBoneImpl(entity, boneName)) {
        return s7_f(sc);
    }
    NetSevered severed = entity.has<NetSevered>() ? entity.get<NetSevered>() : NetSevered{};
    severed.hidden.push_back(boneName);
    entity.set<NetSevered>(std::move(severed));
    queueNetEffect(*g_thingWorld, entity, NetEffect{NetEffectKind::Hide, 0, boneName, {}, 0.0f, 0.0f, 0.0f});
    return s7_t(sc);
}

bool ragdollActorBodyImpl(flecs::entity entity, Vector3 push, float speed, const std::string& hitBoneName, float lifetime) {
    PhysicsWorld* physics = physicsWorld();
    if (!entity.is_valid() || physics == nullptr || !entity.has<Model3D>() || !entity.has<GlobalTransformation>() ||
        entity.has<RagdollRig>()) {
        return false;
    }
    const Model& model = entity.get<Model3D>().model;
    const int boneCount = model.skeleton.boneCount;
    if (boneCount <= 0 || model.boneMatrices == nullptr || model.currentPose == nullptr) {
        return false;
    }
    std::vector<std::uint8_t> physical(static_cast<std::size_t>(boneCount), 1);
    if (const HiddenBones* hidden = actorHiddenBones(entity, boneCount)) {
        for (int bone = 0; bone < boneCount; ++bone) {
            physical[static_cast<std::size_t>(bone)] = hidden->mask[static_cast<std::size_t>(bone)] == 0 ? 1 : 0;
        }
    }
    const std::vector<std::uint8_t> follow(static_cast<std::size_t>(boneCount), 1);
    std::vector<BoneHitbox> scratch;
    const std::vector<BoneHitbox>& boxes = actorHitboxes(entity, model, scratch);
    const Matrix world = refreshedGlobal(entity);

    Vector3 direction = push;
    if (Vector3LengthSqr(direction) > 1e-8f) {
        direction = Vector3Normalize(direction);
    }
    const Vector3 velocity = Vector3Add(Vector3Scale(direction, speed * 0.35f), Vector3{0.0f, speed * 0.1f, 0.0f});
    const Vector3 hitVelocity = Vector3Scale(direction, speed);
    const int hitBone = hitBoneName.empty() ? -1 : findModelBoneIndex(model, hitBoneName);
    if (!startEntityRagdoll(
            entity, model, world, *physics, boxes, physical, follow, velocity, {0.0f, 0.0f, 0.0f}, hitBone,
            hitVelocity, lifetime)) {
        return false;
    }
    physics->destroyCharacter(static_cast<std::uint64_t>(entity.id()));
    if (entity.has<AnimationPlayer>()) {
        entity.remove<AnimationPlayer>();
    }
    if (entity.has<CharacterMotor>()) {
        CharacterMotor& motor = entity.get_mut<CharacterMotor>();
        motor.wishX = 0.0f;
        motor.wishZ = 0.0f;
    }
    if (!entity.has<ActorCorpse>()) {
        entity.add<ActorCorpse>();
    }
    return true;
}


s7_pointer g_actor_ragdoll(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-ragdoll!", args);
    flecs::entity entity = lookupActor(in.string("id string"));
    Vector3 push{0.0f, 0.0f, 0.0f};
    float speed = 3.0f;
    std::string hitBoneName;
    float lifetime = 30.0f;
    if (in.more()) {
        push = in.vec3("dx dy dz numbers");
    }
    if (in.more()) {
        speed = in.realf("speed number");
    }
    if (in.more()) {
        if (in.nextIsString()) {
            hitBoneName = in.string();
        } else if (in.peek() != s7_f(sc)) {
            return in.wrongType("hit bone string or #f");
        } else {
            in.skip();
        }
    }
    if (in.more()) {
        lifetime = in.realf("lifetime number");
    }
    if (!in.ok()) {
        return in.error();
    }
    if (!ragdollActorBodyImpl(entity, push, speed, hitBoneName, lifetime)) {
        return s7_f(sc);
    }
    queueNetEffect(*g_thingWorld, entity,
                   NetEffect{NetEffectKind::Ragdoll, 0, hitBoneName, {push.x, push.y, push.z}, speed, 0.0f, lifetime});
    return s7_t(sc);
}

std::vector<NetPose> captureCorpsePoseImpl(flecs::entity entity) {
    std::vector<NetPose> out;
    if (!entity.is_valid() || !entity.has<Model3D>()) {
        return out;
    }
    const Model& model = entity.get<Model3D>().model;
    if (model.skeleton.boneCount <= 0 || model.boneMatrices == nullptr) {
        return out;
    }
    out.reserve(static_cast<std::size_t>(model.skeleton.boneCount));
    for (int bone = 0; bone < model.skeleton.boneCount; ++bone) {
        Vector3 translation{};
        Quaternion rotation{};
        Vector3 scale{};
        MatrixDecompose(model.boneMatrices[bone], &translation, &rotation, &scale);
        const float position[3]{translation.x, translation.y, translation.z};
        const float turn[4]{rotation.x, rotation.y, rotation.z, rotation.w};
        NetPose pose;
        if (!packNetPose(position, turn, pose)) {
            return {};
        }
        out.push_back(pose);
    }
    return out;
}

bool applyCorpsePoseImpl(flecs::entity entity, const std::vector<NetPose>& bones) {
    if (!entity.is_valid() || !entity.has<Model3D>()) {
        return false;
    }
    Model& model = entity.get_mut<Model3D>().model;
    if (model.boneMatrices == nullptr || static_cast<std::size_t>(model.skeleton.boneCount) != bones.size()) {
        return false;
    }
    for (std::size_t bone = 0; bone < bones.size(); ++bone) {
        float position[3]{};
        float turn[4]{};
        unpackNetPose(bones[bone], position, turn);
        model.boneMatrices[bone] = MatrixMultiply(
            QuaternionToMatrix(QuaternionNormalize({turn[0], turn[1], turn[2], turn[3]})),
            MatrixTranslate(position[0], position[1], position[2]));
    }
    if (!modelUsesGpuSkinning(model)) {
        updateSkinnedMeshVertexBuffers(model);
    }
    if (PhysicsWorld* physics = physicsWorld()) {
        physics->destroyCharacter(static_cast<std::uint64_t>(entity.id()));
    }
    if (entity.has<AnimationPlayer>()) {
        entity.remove<AnimationPlayer>();
    }
    if (!entity.has<ActorCorpse>()) {
        entity.add<ActorCorpse>();
    }
    return true;
}

flecs::entity spawnNetPresentationImpl(flecs::world& world, const std::string& name, const NetSpawnRecipe& recipe) {
    if (!world.has<AssetServices>() || world.get<AssetServices>().store == nullptr || name.empty() ||
        world.lookup(name.c_str()).is_valid() || recipe.kind == NetSpawnKind::None) {
        return {};
    }
    AssetStore& assets = *world.get_mut<AssetServices>().store;
    if (recipe.sprite ? !assets.hasSprite(recipe.path) : !assets.hasGeo(recipe.path)) {
        return {};
    }
    float position[3]{};
    float rotation[4]{};
    unpackNetPose(recipe.pose, position, rotation);
    LocalTransformation local{};
    local.position = {position[0], position[1], position[2]};
    local.rotation = QuaternionNormalize({rotation[0], rotation[1], rotation[2], rotation[3]});
    local.scale = {1.0f, 1.0f, 1.0f};
    const float yaw = QuaternionToEuler(local.rotation).y;
    flecs::entity entity = world.entity(name.c_str());
    entity.add<WorldSpace>().add<MapOwned>().add<NetSpawned>().set<LocalTransformation>(local).set<GlobalTransformation>({});
    if (recipe.sprite) {
        SpriteInstance sprite{.sprite = recipe.path, .frame = "A", .facingYaw = yaw};
        const SpriteAnimBank* bank =
            recipe.kind == NetSpawnKind::Motored && assets.hasSpriteAnim(recipe.path) ? assets.getSpriteAnimBank(recipe.path) : nullptr;
        if (bank != nullptr && bank->clipIndexByName.contains("fly")) {
            SpriteAnimator animator{};
            animator.animPath = recipe.path;
            playSpriteAnim(animator, sprite, bank, "fly", true);
            entity.set<SpriteAnimator>(animator);
        }
        entity.set<SpriteInstance>(sprite);
    } else {
        Model model = cloneGeoModelInstance(assets.getGeoModel(recipe.path));
        if (model.meshCount <= 0) {
            entity.destruct();
            return {};
        }
        entity.set<Model3D>({model, WHITE});
        entity.set<GeoSource>({recipe.path});
        if (recipe.kind == NetSpawnKind::Actor && model.skeleton.boneCount > 0) {
            attachGeoAnimationPlayer(entity, assets, recipe.path, {}, {}, true);
            entity.set<HiddenBones>(
                {std::vector<std::uint8_t>(static_cast<std::size_t>(model.skeleton.boneCount), 0), false});
            entity.set<ModelHitboxes>({});
        }
    }
    if (recipe.kind == NetSpawnKind::Actor) {
        std::vector<std::string> tags = recipe.tags;
        if (tags.empty()) {
            tags.emplace_back("actor");
        }
        entity.add<Actor>().set<CollisionTags>(CollisionTags{std::move(tags)});
    }
    updateTransform(entity, entity.get_mut<LocalTransformation>(), entity.get_mut<GlobalTransformation>());
    return entity;
}

s7_pointer g_physics_blast(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "physics-blast!", args);
    const Vector3 center = in.vec3("x y z numbers");
    const float radius = in.realf("radius number");
    const float speed = in.realf("speed number");
    float lift = 0.0f;
    if (in.more()) {
        lift = in.realf("lift number");
    }
    PhysicsWorld* physics = physicsWorld();
    if (physics == nullptr) {
        return s7_make_integer(sc, 0);
    }
    return s7_make_integer(sc, physics->blast(center, radius, speed, lift));
}

s7_pointer g_actor_bone_severed(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-bone-severed?", args);
    std::string boneName;
    flecs::entity entity = riggedActor(in, boneName);
    if (!in.ok()) {
        return in.error();
    }
    if (!entity.is_valid() || !entity.has<HiddenBones>()) {
        return s7_f(sc);
    }
    const int boneIndex = findModelBoneIndex(entity.get<Model3D>().model, boneName);
    const std::vector<std::uint8_t>& mask = entity.get<HiddenBones>().mask;
    return s7_make_boolean(sc, boneHidden(&mask, boneIndex));
}

std::unordered_map<flecs::entity_t, BoneSprings> g_pendingBoneSprings;

s7_pointer g_actor_bone_springs(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-bone-springs!", args);
    const char* id = in.string("id string");
    s7_pointer specs = in.more() ? in.list("list of (bone param stiffness damping gain limit)") : s7_nil(sc);
    BoneSprings springs;
    for (s7_pointer entries = specs; s7_is_pair(entries); entries = s7_cdr(entries)) {
        s7_pointer entry = s7_car(entries);
        if (!s7_is_pair(entry) || !s7_is_string(s7_car(entry)) || !s7_is_pair(s7_cdr(entry)) ||
            !s7_is_string(s7_cadr(entry))) {
            return in.wrongTypeAt(2, entry, "(bone param stiffness damping gain limit)");
        }
        BoneSpringSpec spec;
        spec.bone = s7_string(s7_car(entry));
        spec.param = s7_string(s7_cadr(entry));
        s7_pointer rest = s7_cddr(entry);
        float* fields[4] = {&spec.stiffness, &spec.damping, &spec.gain, &spec.limit};
        for (int i = 0; i < 4; ++i) {
            if (!s7_is_pair(rest) || !readReal(sc, s7_car(rest), *fields[i])) {
                return in.wrongTypeAt(2, entry, "four spring numbers");
            }
            rest = s7_cdr(rest);
        }
        springs.specs.push_back(std::move(spec));
    }
    if (in.nextIsPair()) {
        s7_pointer rest = in.any();
        float* fields[3] = {&springs.tuning.step, &springs.tuning.maxDt, &springs.tuning.accelSmoothing};
        for (int i = 0; i < 3 && s7_is_pair(rest); ++i) {
            if (!readReal(sc, s7_car(rest), *fields[i])) {
                return in.wrongTypeAt(3, rest, "(step max-dt accel-smoothing)");
            }
            rest = s7_cdr(rest);
        }
    }
    flecs::entity entity = lookupActor(id);
    if (!entity.is_valid() || !entity.has<Model3D>()) {
        return s7_f(sc);
    }
    entity.set<BoneSprings>(springs);
    g_pendingBoneSprings[entity.id()] = std::move(springs);
    return s7_t(sc);
}

s7_pointer g_actor_bone_springs_active(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-bone-springs-active!", args);
    const char* id = in.string("id string");
    const bool active = in.more() && in.truthy();
    flecs::entity entity = lookupActor(id);
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    if (entity.has<BoneSprings>()) {
        g_pendingBoneSprings.erase(entity.id());
        entity.get_mut<BoneSprings>().active = active;
        return s7_t(sc);
    }
    const auto pending = g_pendingBoneSprings.find(entity.id());
    if (pending == g_pendingBoneSprings.end()) {
        return s7_f(sc);
    }
    pending->second.active = active;
    entity.set<BoneSprings>(pending->second);
    return s7_t(sc);
}

s7_pointer g_actor_bone_pos(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-bone-pos", args);
    std::string boneName;
    flecs::entity entity = riggedActor(in, boneName);
    if (!in.ok()) {
        return in.error();
    }
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    const Model& model = entity.get<Model3D>().model;
    const int boneIndex = findModelBoneIndex(model, boneName);
    if (boneIndex < 0) {
        return s7_f(sc);
    }
    const Matrix joint = MatrixMultiply(poseTransformMatrix(model.currentPose[boneIndex]), refreshedGlobal(entity));
    return makeVec3List(sc, joint.m12, joint.m13, joint.m14);
}

s7_pointer g_actor_restore_bones(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-restore-bones!", args);
    flecs::entity entity = lookupActor(in.string("id string"));
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    if (entity.has<HiddenBones>()) {
        HiddenBones& hidden = entity.get_mut<HiddenBones>();
        std::fill(hidden.mask.begin(), hidden.mask.end(), 0);
        hidden.dirty = true;
    }
    if (entity.has<Model3D>() && entity.has<GeoSource>() && g_thingWorld->has<AssetServices>() &&
        g_thingWorld->get<AssetServices>().store != nullptr) {
        AssetStore& assets = *g_thingWorld->get_mut<AssetServices>().store;
        restoreSkinnedModel(entity.get_mut<Model3D>().model, assets.getGeoModel(entity.get<GeoSource>().path));
        if (entity.has<ModelHitboxes>()) {
            entity.set<ModelHitboxes>({});
        }
    }
    return s7_t(sc);
}

s7_pointer g_actor_grounded(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-grounded?", args);
    flecs::entity entity = lookupActor(in.string("id string"));
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    PhysicsWorld* physics = physicsWorld();
    if (physics == nullptr) {
        return s7_f(sc);
    }
    return physics->characterSupported(static_cast<std::uint64_t>(entity.id())) ? s7_t(sc)
                                                                                : s7_f(sc);
}

s7_pointer g_actor_jump(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-jump!", args);
    const char* id = in.string("id string");
    const float speed = in.realf("speed number");
    flecs::entity entity = lookupActor(id);
    if (!entity.is_valid() || !entity.has<CharacterMotor>()) {
        return s7_f(sc);
    }
    PhysicsWorld* physics = physicsWorld();
    if (physics == nullptr) {
        return s7_f(sc);
    }
    return physics->launchCharacter(static_cast<std::uint64_t>(entity.id()), speed) ? s7_t(sc)
                                                                                   : s7_f(sc);
}

s7_pointer g_actor_submerged(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-submerged?", args);
    flecs::entity entity = lookupActor(in.string("id string"));
    if (!entity.is_valid() || !entity.has<CharacterMotor>()) {
        return s7_f(sc);
    }
    return entity.get<CharacterMotor>().submersion > 0.0f ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_actor_near_water_exit(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-near-water-exit?", args);
    flecs::entity entity = lookupActor(in.string("id string"));
    if (!entity.is_valid() || !entity.has<CharacterMotor>()) {
        return s7_f(sc);
    }
    const CharacterMotor& motor = entity.get<CharacterMotor>();
    return (motor.submersion > 0.0f && motor.nearWaterSurface) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_actor_water_exit_dir(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-water-exit-dir", args);
    flecs::entity entity = lookupActor(in.string("id string"));
    if (!entity.is_valid() || !entity.has<CharacterMotor>()) {
        return s7_f(sc);
    }
    const CharacterMotor& motor = entity.get<CharacterMotor>();
    if (motor.submersion <= 0.0f || !motor.nearWaterSurface) {
        return s7_f(sc);
    }
    PhysicsWorld* physics = physicsWorld();
    if (physics == nullptr) {
        return s7_f(sc);
    }
    float dirX = 0.0f;
    float dirZ = 0.0f;
    if (!physics->findWaterExitDirection(static_cast<std::uint64_t>(entity.id()), motor, dirX, dirZ)) {
        return s7_f(sc);
    }
    return s7_list(sc, 2, s7_make_real(sc, dirX), s7_make_real(sc, dirZ));
}

bool attachActorGeoImpl(flecs::entity host, const std::string& slot, const std::string& bone, const std::string& geo,
                        const float place[7]) {
    if (!host.is_valid() || !host.has<Model3D>()) {
        return false;
    }
    if (!g_thingWorld->has<AssetServices>() || g_thingWorld->get<AssetServices>().store == nullptr) {
        return false;
    }
    AssetStore& assets = *g_thingWorld->get_mut<AssetServices>().store;
    const Model& hostModel = host.get<Model3D>().model;
    const GeoAsset* hostGeo =
        host.has<GeoSource>() ? assets.getGeoAsset(host.get<GeoSource>().path) : nullptr;
    BoneAttachment attachment{};
    if (!resolveBoneAttachment(
            hostModel,
            hostGeo,
            bone,
            {place[0], place[1], place[2]},
            {place[3], place[4], place[5]},
            place[6],
            attachment)) {
        TraceLog(LOG_WARNING, "actor-attach-geo: '%s' has no bone or attachment '%s'", host.name().c_str(), bone.c_str());
        return false;
    }
    if (!assets.hasGeo(geo)) {
        TraceLog(LOG_WARNING, "actor-attach-geo: missing geo '%s'", geo.c_str());
        return false;
    }
    const Model source = assets.getGeoModel(geo);
    Model model = cloneGeoModelInstance(source);
    if (model.meshCount <= 0) {
        return false;
    }

    destroyAttachmentTree(host.lookup(slot.c_str()));

    LocalTransformation local{};
    local.scale = attachment.scale;
    flecs::entity child = g_thingWorld->entity().child_of(host);
    child.set_name(slot.c_str());
    child.add<WorldSpace>()
        .add<MapOwned>()
        .set<LocalTransformation>(local)
        .set<GlobalTransformation>(GlobalTransformation{})
        .set<Model3D>({model, WHITE})
        .set<GeoSource>({geo})
        .set<BoneAttachment>(attachment);
    if (model.skeleton.boneCount > 0) {
        attachGeoAnimationPlayer(child, assets, geo, {}, {}, true);
    }
    return true;
}

bool detachActorGeoImpl(flecs::entity host, const std::string& slot) {
    if (!host.is_valid()) {
        return false;
    }
    flecs::entity child = findAttachmentChild(host, slot);
    if (!child.is_valid()) {
        return false;
    }
    destroyAttachmentTree(child);
    return true;
}

s7_pointer g_actor_attach_geo(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-attach-geo", args);
    const char* names[4] = {"id", "name", "bone", "geo"};
    std::string values[4];
    for (int i = 0; i < 4; ++i) {
        values[i] = in.string(names[i]);
    }
    float numbers[7] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    for (float& value : numbers) {
        if (!in.nextIsNumber()) {
            break;
        }
        value = in.realf();
    }
    flecs::entity host = resolveActorPath(values[0]);
    if (!attachActorGeoImpl(host, values[1], values[2], values[3], numbers)) {
        return s7_f(sc);
    }
    if (!host.parent().is_valid()) {
        NetAttachment record;
        record.slot = values[1];
        record.bone = values[2];
        record.geo = values[3];
        std::copy(std::begin(numbers), std::end(numbers), std::begin(record.place));
        NetAttachments attachments = host.has<NetAttachments>() ? host.get<NetAttachments>() : NetAttachments{};
        std::erase_if(attachments.list, [&](const NetAttachment& other) { return other.slot == record.slot; });
        if (attachments.list.size() < kMaxNetAttachments) {
            attachments.list.push_back(record);
        }
        host.set<NetAttachments>(std::move(attachments));
        NetEffect effect;
        effect.kind = NetEffectKind::Attach;
        effect.attachment = std::move(record);
        queueNetEffect(*g_thingWorld, host, std::move(effect));
    }
    return s7_t(sc);
}

s7_pointer g_actor_detach(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-detach", args);
    const char* id = in.string("id string");
    const char* name = in.string("name string");
    if (!in.ok()) {
        return in.error();
    }
    flecs::entity host = resolveActorPath(id);
    if (!detachActorGeoImpl(host, name)) {
        return s7_f(sc);
    }
    if (!host.parent().is_valid()) {
        if (host.has<NetAttachments>()) {
            NetAttachments attachments = host.get<NetAttachments>();
            std::erase_if(attachments.list, [&](const NetAttachment& other) { return other.slot == name; });
            host.set<NetAttachments>(std::move(attachments));
        }
        NetEffect effect;
        effect.kind = NetEffectKind::Detach;
        effect.attachment.slot = name;
        queueNetEffect(*g_thingWorld, host, std::move(effect));
    }
    return s7_t(sc);
}

s7_pointer g_actor_anim_busy(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-anim-busy?", args);
    flecs::entity entity = resolveActorPath(in.string("id string"));
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    if (entity.has<AnimationPlayer>()) {
        const AnimationPlayer& player = entity.get<AnimationPlayer>();
        return (player.playing && !player.loop) ? s7_t(sc) : s7_f(sc);
    }
    if (entity.has<SpriteAnimator>()) {
        const SpriteAnimator& animator = entity.get<SpriteAnimator>();
        return (animator.playing && !animator.loop) ? s7_t(sc) : s7_f(sc);
    }
    return s7_f(sc);
}

s7_pointer g_actor_play_anim(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-play-anim", args);
    const char* id = in.string("id string");
    const char* clip = in.string("clip string");
    const bool loop = in.optFlag(true);

    flecs::entity entity = resolveActorPath(id);
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    if (entity.has<AnimationPlayer>()) {
        AnimationPlayer player = entity.get<AnimationPlayer>();
        const AnimBank* bank = nullptr;
        if (g_thingWorld->has<AssetServices>() &&
            g_thingWorld->get<AssetServices>().store != nullptr) {
            bank = g_thingWorld->get_mut<AssetServices>().store->getAnimBank(player.animBankPath);
        }
        if (bank == nullptr || bank->clipIndexByName.count(clip) == 0) {
            TraceLog(
                LOG_WARNING,
                "actor-play-anim: '%s' bank '%s' has no clip '%s'",
                id,
                player.animBankPath.c_str(),
                clip);
            return s7_f(sc);
        }
        player.play(clip, loop);
        entity.set<AnimationPlayer>(player);
        return s7_t(sc);
    }
    if (!entity.has<SpriteInstance>()) {
        return s7_f(sc);
    }

    SpriteInstance sprite = entity.get<SpriteInstance>();
    SpriteAnimator animator{};
    if (entity.has<SpriteAnimator>()) {
        animator = entity.get<SpriteAnimator>();
    } else {
        animator.animPath = sprite.sprite;
    }
    const SpriteAnimBank* bank = nullptr;
    if (g_thingWorld->has<AssetServices>() &&
        g_thingWorld->get<AssetServices>().store != nullptr) {
        bank = g_thingWorld->get_mut<AssetServices>().store->getSpriteAnimBank(animator.animPath);
    }
    playSpriteAnim(animator, sprite, bank, clip, loop);
    entity.set<SpriteInstance>(sprite);
    entity.set<SpriteAnimator>(animator);
    return s7_t(sc);
}

s7_pointer g_actor_tags(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-tags", args);
    flecs::entity entity = lookupActor(in.string("id string"));
    if (!entity.is_valid() || !entity.has<CollisionTags>()) {
        return s7_f(sc);
    }
    return makeStringList(sc, entity.get<CollisionTags>().tags);
}

s7_pointer g_actors_with_tag(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "actors-with-tag", args);
    const std::string tag = in.string("tag string");
    s7_pointer list = s7_nil(sc);
    g_thingWorld->each([&](flecs::entity entity, Actor, const CollisionTags& tags) {
        if (entity.has<NetHostStandIn>() || !actorHasTag(tags, tag)) {
            return;
        }
        list = s7_cons(sc, s7_make_string(sc, entityIdString(entity).c_str()), list);
    });
    return list;
}

s7_pointer g_actors_in_radius(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "actors-in-radius", args);
    const Vector3 center = in.vec3("x y z numbers");
    const float radius = in.realf("r number");
    const std::string tagFilter = in.optString("");

    const float radiusSq = radius * radius;
    s7_pointer list = s7_nil(sc);
    g_thingWorld->each([&](flecs::entity entity, Actor, const LocalTransformation& local) {
        if (entity.has<NetHostStandIn>()) {
            return;
        }
        if (!tagFilter.empty()) {
            if (!entity.has<CollisionTags>() || !actorHasTag(entity.get<CollisionTags>(), tagFilter)) {
                return;
            }
        }
        if (Vector3DistanceSqr(local.position, center) > radiusSq) {
            return;
        }
        list = s7_cons(sc, s7_make_string(sc, entityIdString(entity).c_str()), list);
    });
    return list;
}

bool isStandIn(flecs::entity entity) {
    for (; entity.is_valid(); entity = entity.parent()) {
        if (entity.has<NetStandIn>()) {
            return true;
        }
    }
    return false;
}

bool standInArg(s7_pointer args) {
    return s7_is_pair(args) && s7_is_string(s7_car(args)) && isStandIn(resolveActorPath(s7_string(s7_car(args))));
}

s7_pointer g_avatar_play_anim(s7_scheme* sc, s7_pointer args) {
    return standInArg(args) ? g_actor_play_anim(sc, args) : s7_f(sc);
}

s7_pointer g_avatar_attach_geo(s7_scheme* sc, s7_pointer args) {
    return standInArg(args) ? g_actor_attach_geo(sc, args) : s7_f(sc);
}

s7_pointer g_avatar_detach(s7_scheme* sc, s7_pointer args) {
    return standInArg(args) ? g_actor_detach(sc, args) : s7_f(sc);
}

s7_pointer g_avatar_nudge(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "avatar-nudge!", args);
    const flecs::entity entity = resolveActorPath(in.string("path string"));
    const Vector3 offset = in.vec3("x y z numbers");
    float euler[3] = {0.0f, 0.0f, 0.0f};
    for (float& value : euler) {
        if (!in.nextIsNumber()) {
            break;
        }
        value = in.realf();
    }
    if (!in.ok()) {
        return in.error();
    }
    if (!isStandIn(entity) || !entity.has<BoneAttachment>()) {
        return s7_f(sc);
    }
    BoneAttachment& attachment = entity.get_mut<BoneAttachment>();
    attachment.nudge = offset;
    attachment.nudgeRotation = QuaternionFromEuler(euler[0], euler[1], euler[2]);
    return s7_t(sc);
}

struct AvatarDowned {
    AnimationPlayer animation;
};

s7_pointer g_avatar_ragdoll(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "avatar-ragdoll!", args);
    const flecs::entity entity = resolveActorPath(in.string("id string"));
    Vector3 push{0.0f, 0.0f, 0.0f};
    float speed = 3.0f;
    if (in.more()) {
        push = in.vec3("dx dy dz numbers");
    }
    if (in.more()) {
        speed = in.realf("speed number");
    }
    if (!in.ok()) {
        return in.error();
    }
    if (!isStandIn(entity) || entity.has<RagdollRig>()) {
        return s7_f(sc);
    }
    const AnimationPlayer* animation = entity.try_get<AnimationPlayer>();
    const AnimationPlayer saved = animation != nullptr ? *animation : AnimationPlayer{};
    const bool animated = animation != nullptr;
    if (!ragdollActorBodyImpl(entity, push, speed, "", 30.0f)) {
        return s7_f(sc);
    }
    if (animated) {
        entity.set<AvatarDowned>({saved});
    }
    return s7_t(sc);
}

s7_pointer g_avatar_revive(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "avatar-revive!", args);
    flecs::entity entity = resolveActorPath(in.string("id string"));
    if (!in.ok()) {
        return in.error();
    }
    if (!isStandIn(entity) || !entity.has<RagdollRig>()) {
        return s7_f(sc);
    }
    const std::uint64_t id = static_cast<std::uint64_t>(entity.id());
    PhysicsWorld* physics = physicsWorld();
    if (physics != nullptr) {
        physics->destroyRagdoll(id);
    }
    entity.remove<RagdollRig>();
    entity.remove<ActorCorpse>();
    if (const AvatarDowned* downed = entity.try_get<AvatarDowned>()) {
        entity.set<AnimationPlayer>(downed->animation);
        entity.remove<AvatarDowned>();
    }
    LocalTransformation& local = entity.get_mut<LocalTransformation>();
    local.rotation = QuaternionIdentity();
    if (physics != nullptr && entity.has<CharacterMotor>() && !physics->hasCharacter(id)) {
        physics->createCharacter(id, local.position.x, local.position.y, local.position.z, entity.get<CharacterMotor>());
    }
    return s7_t(sc);
}

s7_pointer g_avatar_velocity(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "avatar-velocity", args);
    const flecs::entity entity = resolveActorPath(in.string("id string"));
    if (!isStandIn(entity) || !entity.has<SimPose>()) {
        return s7_f(sc);
    }
    const SimPose& pose = entity.get<SimPose>();
    const Vector3 velocity =
        Vector3Scale(Vector3Subtract(pose.cur.position, pose.prev.position), 1.0f / kSimTickDt);
    return s7_list(sc, 3, s7_make_real(sc, velocity.x), s7_make_real(sc, velocity.y), s7_make_real(sc, velocity.z));
}

s7_pointer g_avatars(s7_scheme* sc, s7_pointer) {
    std::vector<std::string> ids;
    g_thingWorld->each([&](flecs::entity entity, NetStandIn) {
        if (entity.name().length() > 0) {
            ids.emplace_back(entity.name().c_str());
        }
    });
    std::sort(ids.begin(), ids.end());
    s7_pointer list = s7_nil(sc);
    for (auto it = ids.rbegin(); it != ids.rend(); ++it) {
        list = s7_cons(sc, s7_make_string(sc, it->c_str()), list);
    }
    return list;
}

constexpr SchemeBinding kActorBindings[] = {
    {"actor-spawn", g_actor_spawn, 7, 5, false, "(actor-spawn id x y z yaw kind path [radius height speed gravity tags-list])", ScriptCap::WorldMutate},
    {"actor-pos", g_actor_pos, 1, 0, false, "(actor-pos id)", ScriptCap::ReadWorld},
    {"actor-yaw", g_actor_yaw, 1, 0, false, "(actor-yaw id)", ScriptCap::ReadWorld},
    {"actor-set-yaw!", g_actor_set_yaw, 2, 0, false, "(actor-set-yaw! id yaw)", ScriptCap::WorldMutate},
    {"actor-set-bone-scale!", g_actor_set_bone_scale, 3, 1, false, "(actor-set-bone-scale! id bone size [length])", ScriptCap::WorldMutate},
    {"actor-bone-scale", g_actor_bone_scale, 2, 0, false, "(actor-bone-scale id bone)", ScriptCap::ReadWorld},
    {"actor-sever-bone!", g_actor_sever_bone, 2, 6, false, "(actor-sever-bone! id bone [dx dy dz] [speed] [spin] [lifetime])", ScriptCap::WorldMutate},
    {"actor-hide-bone!", g_actor_hide_bone, 2, 0, false, "(actor-hide-bone! id bone)", ScriptCap::WorldMutate},
    {"actor-bone-severed?", g_actor_bone_severed, 2, 0, false, "(actor-bone-severed? id bone)", ScriptCap::ReadWorld},
    {"actor-ragdoll!", g_actor_ragdoll, 1, 6, false, "(actor-ragdoll! id [dx dy dz] [speed] [hit-bone] [lifetime])", ScriptCap::WorldMutate},
    {"physics-blast!", g_physics_blast, 5, 1, false, "(physics-blast! x y z radius speed [lift])", ScriptCap::WorldMutate},
    {"actor-bone-pos", g_actor_bone_pos, 2, 0, false, "(actor-bone-pos id bone)", ScriptCap::ReadWorld},
    {"actor-bone-springs!", g_actor_bone_springs, 2, 1, false, "(actor-bone-springs! id ((bone param stiffness damping gain limit) ...) [(step max-dt accel-smoothing)])", ScriptCap::WorldMutate},
    {"actor-bone-springs-active!", g_actor_bone_springs_active, 2, 0, false, "(actor-bone-springs-active! id active?)", ScriptCap::WorldMutate},
    {"actor-restore-bones!", g_actor_restore_bones, 1, 0, false, "(actor-restore-bones! id)", ScriptCap::WorldMutate},
    {"actor-clear-bone-scales!", g_actor_clear_bone_scales, 1, 0, false, "(actor-clear-bone-scales! id)", ScriptCap::WorldMutate},
    {"actor-set-shader-param!", g_actor_set_shader_param, 2, 4, false, "(actor-set-shader-param! id name [x y z w])", ScriptCap::WorldMutate},
    {"actor-clear-shader-params!", g_actor_clear_shader_params, 1, 0, false, "(actor-clear-shader-params! id)", ScriptCap::WorldMutate},
    {"thing-pos", g_thing_pos, 1, 0, false, "(thing-pos id)", ScriptCap::ReadWorld},
    {"thing-yaw", g_thing_yaw, 1, 0, false, "(thing-yaw id)", ScriptCap::ReadWorld},
    {"actor-set-wish", g_actor_set_wish, 3, 0, false, "(actor-set-wish id wx wz)", ScriptCap::WorldMutate},
    {"actor-set-wish-3d", g_actor_set_wish_3d, 4, 0, false, "(actor-set-wish-3d id wx wy wz)", ScriptCap::WorldMutate},
    {"actor-set-move-speed!", g_actor_set_move_speed, 2, 0, false, "(actor-set-move-speed! id speed)", ScriptCap::WorldMutate},
    {"actor-set-corpse!", g_actor_set_corpse, 1, 0, false, "(actor-set-corpse! id)", ScriptCap::WorldMutate},
    {"sprite-hide-part!", g_sprite_hide_part, 2, 0, false, "(sprite-hide-part! id part-name)", ScriptCap::WorldMutate},
    {"actor-grounded?", g_actor_grounded, 1, 0, false, "(actor-grounded? id)", ScriptCap::ReadWorld},
    {"actor-jump!", g_actor_jump, 2, 0, false, "(actor-jump! id speed)", ScriptCap::WorldMutate},
    {"actor-submerged?", g_actor_submerged, 1, 0, false, "(actor-submerged? id)", ScriptCap::ReadWorld},
    {"actor-near-water-exit?", g_actor_near_water_exit, 1, 0, false, "(actor-near-water-exit? id)", ScriptCap::ReadWorld},
    {"actor-water-exit-dir", g_actor_water_exit_dir, 1, 0, false, "(actor-water-exit-dir id)", ScriptCap::ReadWorld},
    {"actor-play-anim", g_actor_play_anim, 2, 1, false, "(actor-play-anim path clip [loop])", ScriptCap::WorldMutate},
    {"actor-anim-busy?", g_actor_anim_busy, 1, 0, false, "(actor-anim-busy? path)", ScriptCap::ReadWorld},
    {"actor-attach-geo", g_actor_attach_geo, 4, 7, false, "(actor-attach-geo path name point geo [x y z] [pitch yaw roll] [scale])", ScriptCap::WorldMutate},
    {"actor-detach", g_actor_detach, 2, 0, false, "(actor-detach path name)", ScriptCap::WorldMutate},
    {"avatars", g_avatars, 0, 0, false, "(avatars)", ScriptCap::ReadWorld},
    {"avatar-velocity", g_avatar_velocity, 1, 0, false, "(avatar-velocity id)", ScriptCap::ReadWorld},
    {"avatar-play-anim", g_avatar_play_anim, 2, 1, false, "(avatar-play-anim path clip [loop])", ScriptCap::FpPresent},
    {"avatar-attach-geo", g_avatar_attach_geo, 4, 7, false, "(avatar-attach-geo path name point geo [x y z] [pitch yaw roll] [scale])", ScriptCap::FpPresent},
    {"avatar-detach", g_avatar_detach, 2, 0, false, "(avatar-detach path name)", ScriptCap::FpPresent},
    {"avatar-nudge!", g_avatar_nudge, 4, 3, false, "(avatar-nudge! path x y z [pitch yaw roll])", ScriptCap::FpPresent},
    {"avatar-ragdoll!", g_avatar_ragdoll, 1, 4, false, "(avatar-ragdoll! id [dx dy dz] [speed])", ScriptCap::FpPresent},
    {"avatar-revive!", g_avatar_revive, 1, 0, false, "(avatar-revive! id)", ScriptCap::FpPresent},
    {"actor-tags", g_actor_tags, 1, 0, false, "(actor-tags id)", ScriptCap::ReadWorld},
    {"actors-with-tag", g_actors_with_tag, 1, 0, false, "(actors-with-tag tag)", ScriptCap::ReadWorld},
    {"actors-in-radius", g_actors_in_radius, 4, 1, false, "(actors-in-radius x y z r [tag])", ScriptCap::ReadWorld},
};

constexpr std::size_t kMaxDebugLines = 2048;

DebugLinePool& debugLinePoolMut() {
    if (!g_thingWorld->has<DebugLinePool>()) {
        g_thingWorld->set<DebugLinePool>({});
    }
    return g_thingWorld->get_mut<DebugLinePool>();
}

s7_pointer g_debug_line_add(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "debug-line-add!", args);
    const Vector3 from = in.vec3("x0 y0 z0 numbers");
    const Vector3 to = in.vec3("x1 y1 z1 numbers");
    const float r = in.realf("r number");
    const float g = in.realf("g number");
    const float b = in.realf("b number");
    const float a = in.optRealf(255.0f);

    DebugLinePool& pool = debugLinePoolMut();
    if (pool.lines.size() >= kMaxDebugLines) {
        pool.lines.erase(pool.lines.begin());
    }
    pool.lines.push_back({
        .from = from,
        .to = to,
        .color = {
            static_cast<std::uint8_t>(std::clamp(r, 0.0f, 255.0f)),
            static_cast<std::uint8_t>(std::clamp(g, 0.0f, 255.0f)),
            static_cast<std::uint8_t>(std::clamp(b, 0.0f, 255.0f)),
            static_cast<std::uint8_t>(std::clamp(a, 0.0f, 255.0f)),
        },
    });
    return s7_t(sc);
}

s7_pointer g_debug_lines_clear(s7_scheme* sc, s7_pointer) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (g_thingWorld->has<DebugLinePool>()) {
        g_thingWorld->get_mut<DebugLinePool>().lines.clear();
    }
    return s7_t(sc);
}

bool lineOfSightClear(Vector3 from, Vector3 to) {
    PhysicsWorld* physics = physicsWorld();
    if (physics == nullptr) {
        return false;
    }
    const Vector3 delta = Vector3Subtract(to, from);
    const float distance = Vector3Length(delta);
    if (distance <= 1.0e-6f) {
        return true;
    }
    const Vector3 dir = Vector3Scale(delta, 1.0f / distance);
    return !physics->castRay(from, dir, distance, BrushBlock::Los).has_value();
}

s7_pointer g_los(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "los?", args);
    const Vector3 from = in.vec3("x0 y0 z0 numbers");
    const Vector3 to = in.vec3("x1 y1 z1 numbers");
    return s7_make_boolean(sc, lineOfSightClear(from, to));
}

constexpr float kVolumeProbeDefaultDist = 64.0f;

s7_pointer g_volume_floor_at(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "volume-floor-at", args);
    const Vector3 at = in.vec3("x y z numbers");
    const float maxDist = in.optRealf(kVolumeProbeDefaultDist);
    PhysicsWorld* physics = physicsWorld();
    if (physics == nullptr) {
        return s7_f(sc);
    }
    const auto hit = physics->castRay(at, {0.0f, -1.0f, 0.0f}, maxDist, BrushBlock::Actor);
    if (!hit.has_value()) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(hit->point.y));
}

s7_pointer g_volume_ceiling_at(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "volume-ceiling-at", args);
    const Vector3 at = in.vec3("x y z numbers");
    const float maxDist = in.optRealf(kVolumeProbeDefaultDist);
    PhysicsWorld* physics = physicsWorld();
    if (physics == nullptr) {
        return s7_f(sc);
    }
    const auto hit = physics->castRay(at, {0.0f, 1.0f, 0.0f}, maxDist, BrushBlock::Actor);
    if (!hit.has_value()) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(hit->point.y));
}

s7_pointer g_volume_clearance(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "volume-clearance", args);
    const Vector3 at = in.vec3("x y z numbers");
    const float halfHeight = in.realf("half-height number");
    const float maxDist = in.optRealf(kVolumeProbeDefaultDist);
    PhysicsWorld* physics = physicsWorld();
    if (physics == nullptr) {
        return s7_f(sc);
    }
    const auto floorHit = physics->castRay(at, {0.0f, -1.0f, 0.0f}, maxDist, BrushBlock::Actor);
    if (!floorHit.has_value()) {
        return s7_f(sc);
    }
    const auto ceilingHit = physics->castRay(at, {0.0f, 1.0f, 0.0f}, maxDist, BrushBlock::Actor);
    if (!ceilingHit.has_value()) {
        return s7_f(sc);
    }
    const float floorY = floorHit->point.y;
    const float ceilingY = ceilingHit->point.y;
    const float usable = ceilingY - floorY - 2.0f * halfHeight;
    return makeVec3List(sc, floorY, ceilingY, usable);
}

s7_pointer g_linescan_world(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "linescan-world", args);
    const Vector3 origin = in.vec3("ox oy oz numbers");
    const Vector3 rawDir = in.vec3("dx dy dz numbers");
    const float maxDistance = in.realf("max-distance number");
    if (maxDistance <= 0.0f) {
        return s7_f(sc);
    }

    const float dirLen = Vector3Length(rawDir);
    if (dirLen <= 1.0e-6f) {
        return s7_f(sc);
    }
    const Vector3 dir = Vector3Scale(rawDir, 1.0f / dirLen);

    PhysicsWorld* physics = physicsWorld();
    if (physics == nullptr) {
        return s7_f(sc);
    }
    const auto hit = physics->castRay(origin, dir, maxDistance, BrushBlock::Linescan);
    if (!hit.has_value()) {
        return s7_f(sc);
    }
    return s7_list(
        sc,
        7,
        s7_make_real(sc, static_cast<double>(hit->point.x)),
        s7_make_real(sc, static_cast<double>(hit->point.y)),
        s7_make_real(sc, static_cast<double>(hit->point.z)),
        s7_make_real(sc, static_cast<double>(hit->normal.x)),
        s7_make_real(sc, static_cast<double>(hit->normal.y)),
        s7_make_real(sc, static_cast<double>(hit->normal.z)),
        s7_make_real(sc, static_cast<double>(hit->fraction * maxDistance)));
}

s7_pointer g_hitscan_actors(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "hitscan-actors", args);
    const Vector3 origin = in.vec3("ox oy oz numbers");
    const Vector3 rawDir = in.vec3("dx dy dz numbers");
    const float maxDistance = in.realf("max-distance number");
    const std::string tagFilter = in.optString("");
    const bool xzOnly = in.optBoolean(false);

    if (maxDistance <= 0.0f) {
        return s7_f(sc);
    }

    const float dirLen = Vector3Length(rawDir);
    if (dirLen <= 1.0e-6f) {
        return s7_f(sc);
    }
    const Vector3 dir = Vector3Scale(rawDir, 1.0f / dirLen);

    float range = maxDistance;
    if (PhysicsWorld* physics = physicsWorld()) {
        if (const auto wall = physics->castRay(origin, dir, range, BrushBlock::Linescan)) {
            range = wall->fraction * range;
        }
    }
    if (range <= 0.0f) {
        return s7_f(sc);
    }

    if (!g_thingWorld->has<AssetServices>() || g_thingWorld->get<AssetServices>().store == nullptr) {
        return s7_f(sc);
    }
    AssetStore& assets = *g_thingWorld->get_mut<AssetServices>().store;

    const Ray ray{origin, dir};
    flecs::entity bestEntity{};
    SpriteBillboardHit bestHit{};
    float bestDistance = range;

    flecs::entity player = g_thingWorld->lookup("Player");
    const Lens* lens = (player.is_valid() && player.has<Lens>()) ? &player.get<Lens>() : nullptr;

    g_thingWorld->each([&](flecs::entity entity,
                           Actor,
                           SpriteInstance& sprite,
                           GlobalTransformation& global) {
        if (!entity.has<WorldSpace>() || entity.has<ViewSprite>() || entity.has<ActorCorpse>() ||
            entity.has<NetHostStandIn>()) {
            return;
        }
        if (!tagFilter.empty()) {
            if (!entity.has<CollisionTags>() || !actorHasTag(entity.get<CollisionTags>(), tagFilter)) {
                return;
            }
        }
        if (entity.has<LocalTransformation>()) {
            updateTransform(entity, entity.get_mut<LocalTransformation>(), global);
        }

        SpriteAnimTween tween{};
        const SpriteAnimTween* tweenPtr = nullptr;
        if (entity.has<SpriteAnimator>()) {
            const SpriteAnimator& animator = entity.get<SpriteAnimator>();
            if (animator.hasTween() && !animator.nextFrame.empty()) {
                tween.nextFrame = animator.nextFrame;
                tween.blend = animator.transformBlend;
                tween.tweenRotation = animator.tweenRotation;
                tween.tweenScale = animator.tweenScale;
                tween.tweenTranslate = animator.tweenTranslate;
                tweenPtr = &tween;
            }
        }

        std::optional<SpriteBillboard> billboard;
        if (lens != nullptr) {
            billboard = resolveSpriteBillboard(sprite, global, *lens, assets, tweenPtr);
        } else {
            billboard = resolveSpriteBillboard(
                sprite,
                global,
                origin,
                horizontalCameraYaw(origin, Vector3Add(origin, dir)),
                assets,
                tweenPtr);
        }
        if (!billboard) {
            return;
        }
        const auto hit = xzOnly ? raycastSpriteBillboardXZ(ray, *billboard, bestDistance)
                                 : raycastSpriteBillboard(ray, *billboard, bestDistance);
        if (!hit) {
            return;
        }
        bestDistance = hit->distance;
        bestHit = *hit;
        bestEntity = entity;
    });

    g_thingWorld->each([&](flecs::entity entity, Actor, Model3D& model3d, GlobalTransformation& global) {
        if (!entity.has<WorldSpace>() || entity.has<ActorCorpse>() || entity.has<NetHostStandIn>()) {
            return;
        }
        const Model& model = model3d.model;
        if (model.skeleton.boneCount <= 0 || model.boneMatrices == nullptr) {
            return;
        }
        if (!tagFilter.empty()) {
            if (!entity.has<CollisionTags>() || !actorHasTag(entity.get<CollisionTags>(), tagFilter)) {
                return;
            }
        }
        if (entity.has<LocalTransformation>()) {
            updateTransform(entity, entity.get_mut<LocalTransformation>(), global);
        }
        const Vector3 toActor = Vector3Subtract(Vector3{global.matrix.m12, global.matrix.m13, global.matrix.m14}, origin);
        const float along = Clamp(Vector3DotProduct(toActor, dir), 0.0f, bestDistance);
        constexpr float kCullRadius = 4.0f;
        if (Vector3Length(Vector3Subtract(toActor, Vector3Scale(dir, along))) > kCullRadius) {
            return;
        }
        std::vector<BoneHitbox> scratch;
        const std::vector<BoneHitbox>& boxes = actorHitboxes(entity, model, scratch);
        const std::vector<std::uint8_t>* hidden =
            entity.has<HiddenBones>() ? &entity.get<HiddenBones>().mask : nullptr;
        const auto hit = raycastModelBones(model, boxes, global.matrix, hidden, ray, bestDistance);
        if (!hit || hit->distance >= bestDistance) {
            return;
        }
        bestDistance = hit->distance;
        bestHit = SpriteBillboardHit{};
        bestHit.distance = hit->distance;
        bestHit.point = hit->point;
        bestHit.partName = model.skeleton.bones[hit->bone].name;
        bestEntity = entity;
    });

    if (!bestEntity.is_valid()) {
        return s7_f(sc);
    }

    return s7_list(
        sc,
        6,
        s7_make_string(sc, entityIdString(bestEntity).c_str()),
        s7_make_string(sc, bestHit.partName.c_str()),
        s7_make_real(sc, bestHit.distance),
        s7_make_real(sc, bestHit.point.x),
        s7_make_real(sc, bestHit.point.y),
        s7_make_real(sc, bestHit.point.z));
}

s7_pointer g_actor_los(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "actor-los?", args);
    const char* fromId = in.string("from-id string");
    const char* toId = in.string("to-id string");

    flecs::entity from = lookupActor(fromId);
    flecs::entity to = lookupActor(toId);
    if (!from.is_valid() || !to.is_valid() || !from.has<LocalTransformation>() ||
        !to.has<LocalTransformation>() || !from.has<CharacterMotor>() || !to.has<CharacterMotor>()) {
        return s7_f(sc);
    }

    const CharacterMotor& fromMotor = from.get<CharacterMotor>();
    const CharacterMotor& toMotor = to.get<CharacterMotor>();
    const Vector3 fromFeet = from.get<LocalTransformation>().position;
    const Vector3 toFeet = to.get<LocalTransformation>().position;
    const float fromEye = fromMotor.height + fromMotor.radius;
    const float toEye = toMotor.height + toMotor.radius;

    const Vector3 fromEyePos{fromFeet.x, fromFeet.y + fromEye * 0.75f, fromFeet.z};
    const Vector3 toEyePos{toFeet.x, toFeet.y + toEye * 0.75f, toFeet.z};
    return s7_make_boolean(sc, lineOfSightClear(fromEyePos, toEyePos));
}

s7_pointer g_pvs_can_see(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "pvs-can-see", args);
    const Vector3 from = in.vec3("x0 y0 z0 numbers");
    const Vector3 to = in.vec3("x1 y1 z1 numbers");
    if (g_thingWorld == nullptr || !g_thingWorld->has<MapPvs>() || !g_thingWorld->has<MapBsp>()) {
        return s7_t(sc);
    }
    const AreaGate gate = areaGateFromPoint(*g_thingWorld, from);
    return s7_make_boolean(
        sc,
        pvsVisiblePoints(g_thingWorld->get<MapBsp>().tree, g_thingWorld->get<MapPvs>().pvs, from, to, &gate));
}

bool isDoorPortalOpenForSound(const std::string& doorBrushId) {
    if (g_thingWorld == nullptr) {
        return true;
    }
    flecs::entity door = g_thingWorld->lookup(doorBrushId.c_str());
    if (!door.is_valid() || !door.has<RigidMover>()) {
        return true;
    }
    return door.get<RigidMover>().target >= 0.5f;
}

s7_pointer g_sound_emit(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_nil(sc);
    }
    ArgReader in(sc, "sound-emit!", args);
    const Vector3 at = in.vec3("x y z numbers");
    const float loudness = in.realf("loudness number");
    const float falloffPerUnit = in.optRealf(1.0f);
    if (loudness <= 0.0f || falloffPerUnit < 0.0f) {
        return s7_nil(sc);
    }

    if (!g_thingWorld->has<MapNavigation>() || !g_thingWorld->has<MapBsp>()) {
        return s7_nil(sc);
    }
    const MapNavigation& nav = g_thingWorld->get<MapNavigation>();
    const BspTree& tree = g_thingWorld->get<MapBsp>().tree;
    const std::int32_t originLeaf = sampleNavLeaf(nav, tree, at);
    if (originLeaf < 0) {
        return s7_nil(sc);
    }

    const std::vector<float> perceived =
        floodSound(nav, originLeaf, loudness, falloffPerUnit, isDoorPortalOpenForSound);

    s7_pointer list = s7_nil(sc);
    g_thingWorld->each([&](flecs::entity entity, Actor, const LocalTransformation& local) {
        if (entity.has<NetHostStandIn>()) {
            return;
        }
        const std::int32_t leaf = sampleNavLeaf(nav, tree, local.position);
        if (leaf < 0 || leaf >= static_cast<std::int32_t>(perceived.size())) {
            return;
        }
        const float heard = perceived[static_cast<std::size_t>(leaf)];
        if (heard <= 0.0f) {
            return;
        }
        list = s7_cons(
            sc,
            s7_list(
                sc,
                2,
                s7_make_string(sc, entityIdString(entity).c_str()),
                s7_make_real(sc, heard)),
            list);
    });
    return list;
}

constexpr SchemeBinding kWorldQueryBindings[] = {
    {"los?", g_los, 6, 0, false, "(los? x0 y0 z0 x1 y1 z1)", ScriptCap::ReadWorld},
    {"volume-floor-at", g_volume_floor_at, 3, 1, false, "(volume-floor-at x y z [max-dist])", ScriptCap::ReadWorld},
    {"volume-ceiling-at", g_volume_ceiling_at, 3, 1, false, "(volume-ceiling-at x y z [max-dist])", ScriptCap::ReadWorld},
    {"volume-clearance", g_volume_clearance, 4, 1, false, "(volume-clearance x y z half-height [max-dist])", ScriptCap::ReadWorld},
    {"debug-line-add!", g_debug_line_add, 9, 1, false, "(debug-line-add! x0 y0 z0 x1 y1 z1 r g b [a])", ScriptCap::WorldMutate},
    {"debug-lines-clear!", g_debug_lines_clear, 0, 0, false, "(debug-lines-clear!)", ScriptCap::WorldMutate},
    {"pvs-can-see", g_pvs_can_see, 6, 0, false, "(pvs-can-see x0 y0 z0 x1 y1 z1)", ScriptCap::ReadWorld},
    {"sound-emit!", g_sound_emit, 4, 1, false, "(sound-emit! x y z loudness [falloff-per-unit]) -> ((id loudness) ...)", ScriptCap::ReadWorld},
    {"actor-los?", g_actor_los, 2, 0, false, "(actor-los? from-id to-id)", ScriptCap::ReadWorld},
    {"hitscan-actors", g_hitscan_actors, 7, 2, false, "(hitscan-actors ox oy oz dx dy dz max-distance [tag] [xz-only?]) -> " "(id part distance x y z)", ScriptCap::ReadWorld},
    {"linescan-world", g_linescan_world, 7, 0, false, "(linescan-world ox oy oz dx dy dz max-distance) -> (x y z nx ny nz distance) or #f", ScriptCap::ReadWorld},
};

flecs::entity moverFromArgs(ArgReader& in, const char* descr) {
    const char* id = in.string(descr);
    if (g_thingWorld == nullptr) {
        return {};
    }
    return g_thingWorld->lookup(id);
}

const RigidMover* moverComponent(flecs::entity entity) {
    if (!entity.is_valid() || !entity.has<RigidMover>()) {
        return nullptr;
    }
    return &entity.get<RigidMover>();
}

s7_pointer g_mover_open(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "mover-open", args);
    return s7_make_boolean(sc, moverRequestOpen(moverFromArgs(in, "id string")));
}

s7_pointer g_mover_close(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "mover-close", args);
    return s7_make_boolean(sc, moverRequestClose(moverFromArgs(in, "id string")));
}

s7_pointer g_mover_toggle(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "mover-toggle", args);
    return s7_make_boolean(sc, moverRequestToggle(moverFromArgs(in, "id string")));
}

s7_pointer g_mover_open_group(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "mover-open-group", args);
    const char* group = in.string("group string");
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    moverRequestOpenGroup(*g_thingWorld, group);
    return s7_t(sc);
}

s7_pointer g_mover_close_group(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "mover-close-group", args);
    const char* group = in.string("group string");
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    moverRequestCloseGroup(*g_thingWorld, group);
    return s7_t(sc);
}

s7_pointer g_mover_toggle_group(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "mover-toggle-group", args);
    const char* group = in.string("group string");
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    moverRequestToggleGroup(*g_thingWorld, group);
    return s7_t(sc);
}

s7_pointer g_mover_set_locked(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "mover-set-locked", args);
    flecs::entity entity = moverFromArgs(in, "id bool");
    const bool locked = in.truthy("id bool");
    if (moverComponent(entity) == nullptr) {
        return s7_f(sc);
    }
    entity.get_mut<RigidMover>().locked = locked;
    return s7_t(sc);
}

s7_pointer g_mover_locked_p(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "mover-locked?", args);
    const RigidMover* mover = moverComponent(moverFromArgs(in, "id string"));
    if (mover == nullptr) {
        return s7_f(sc);
    }
    return s7_make_boolean(sc, mover->locked);
}

s7_pointer g_mover_progress(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "mover-progress", args);
    const RigidMover* mover = moverComponent(moverFromArgs(in, "id string"));
    if (mover == nullptr) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(mover->progress));
}

s7_pointer g_mover_state(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "mover-state", args);
    const RigidMover* mover = moverComponent(moverFromArgs(in, "id string"));
    if (mover == nullptr) {
        return s7_f(sc);
    }
    const bool open = mover->target >= 0.5f || mover->progress >= 0.5f;
    return makeAlist(
        sc,
        {
            {"open?", s7_make_boolean(sc, open)},
            {"progress", s7_make_real(sc, mover->progress)},
            {"locked?", s7_make_boolean(sc, mover->locked)},
        });
}

s7_pointer g_mover_ids(s7_scheme* sc, s7_pointer) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    std::vector<std::string> ids;
    g_thingWorld->each([&](flecs::entity entity, const RigidMover&) {
        const char* name = entity.name();
        if (name != nullptr && name[0] != '\0') {
            ids.emplace_back(name);
        }
    });
    return makeStringList(sc, ids);
}

s7_pointer g_mover_set_state(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "mover-set-state", args);
    flecs::entity entity = moverFromArgs(in, "id open? progress [locked?]");
    const bool open = in.truthy("id open? progress [locked?]");
    const float progress = in.realf("progress number");
    bool setLocked = false;
    bool locked = false;
    if (in.more()) {
        setLocked = true;
        locked = in.truthy();
    }
    if (moverComponent(entity) == nullptr) {
        return s7_f(sc);
    }
    moverApplyState(entity, open, progress, setLocked, locked);
    return s7_t(sc);
}

constexpr SchemeBinding kMoverBindings[] = {
    {"mover-open", g_mover_open, 1, 0, false, "(mover-open id)", ScriptCap::WorldMutate},
    {"mover-close", g_mover_close, 1, 0, false, "(mover-close id)", ScriptCap::WorldMutate},
    {"mover-toggle", g_mover_toggle, 1, 0, false, "(mover-toggle id)", ScriptCap::WorldMutate},
    {"mover-open-group", g_mover_open_group, 1, 0, false, "(mover-open-group group)", ScriptCap::WorldMutate},
    {"mover-close-group", g_mover_close_group, 1, 0, false, "(mover-close-group group)", ScriptCap::WorldMutate},
    {"mover-toggle-group", g_mover_toggle_group, 1, 0, false, "(mover-toggle-group group)", ScriptCap::WorldMutate},
    {"mover-set-locked", g_mover_set_locked, 2, 0, false, "(mover-set-locked id bool)", ScriptCap::WorldMutate},
    {"mover-locked?", g_mover_locked_p, 1, 0, false, "(mover-locked? id)", ScriptCap::ReadWorld},
    {"mover-progress", g_mover_progress, 1, 0, false, "(mover-progress id)", ScriptCap::ReadWorld},
    {"mover-state", g_mover_state, 1, 0, false, "(mover-state id)", ScriptCap::ReadWorld},
    {"mover-ids", g_mover_ids, 0, 0, false, "(mover-ids)", ScriptCap::ReadWorld},
    {"mover-set-state", g_mover_set_state, 3, 1, false, "(mover-set-state id open? progress [locked?])", ScriptCap::WorldMutate},
};

bool readSightTagList(s7_scheme* sc, s7_pointer value, std::vector<std::string>& out) {
    (void)sc;
    out.clear();
    if (s7_is_string(value) || s7_is_symbol(value)) {
        const char* tag = s7_is_string(value) ? s7_string(value) : s7_symbol_name(value);
        if (tag != nullptr && tag[0] != '\0') {
            out.emplace_back(tag);
        }
        return true;
    }
    for (s7_pointer cursor = value; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer cell = s7_car(cursor);
        if (s7_is_string(cell)) {
            out.emplace_back(s7_string(cell));
        } else if (s7_is_symbol(cell)) {
            out.emplace_back(s7_symbol_name(cell));
        }
    }
    return true;
}

bool applySightAlist(s7_scheme* sc, s7_pointer alist, ActorSight& sight) {
    if (!s7_is_pair(alist) && !s7_is_null(sc, alist)) {
        return false;
    }
    return rejectedAlistEntry(alist, [&](const char* key, s7_pointer value) {
        if (std::strcmp(key, "enabled") == 0) {
            if (!readFlag(sc, value, sight.enabled)) {
                return false;
            }
        } else if (std::strcmp(key, "range") == 0) {
            if (!readReal(sc, value, sight.range)) {
                return false;
            }
        } else if (std::strcmp(key, "fov") == 0) {
            if (!readReal(sc, value, sight.fovDegrees)) {
                return false;
            }
        } else if (std::strcmp(key, "eye-lift") == 0) {
            if (!readReal(sc, value, sight.eyeLift)) {
                return false;
            }
        } else if (std::strcmp(key, "filter") == 0) {
            if (value == s7_f(sc) || s7_is_null(sc, value)) {
                sight.filterProc.clear();
            } else if (s7_is_string(value)) {
                sight.filterProc = s7_string(value);
            } else if (s7_is_symbol(value)) {
                sight.filterProc = s7_symbol_name(value);
            } else {
                return false;
            }
        } else if (std::strcmp(key, "see-tags") == 0) {
            if (!readSightTagList(sc, value, sight.seeTags)) {
                return false;
            }
        } else if (std::strcmp(key, "ignore-tags") == 0) {
            if (!readSightTagList(sc, value, sight.ignoreTags)) {
                return false;
            }
        } else {
            return false;
        }
        return true;
    }) == nullptr;
}

s7_pointer g_actor_sight_set(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "actor-sight-set!", args);
    const char* id = in.string("id string");
    s7_pointer alist = in.any("alist");
    flecs::entity entity = g_thingWorld->lookup(id);
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    ActorSight sight = entity.has<ActorSight>() ? entity.get<ActorSight>() : ActorSight{};
    if (!applySightAlist(sc, alist, sight)) {
        return in.wrongTypeAt(
            2, alist, "alist with enabled/range/fov/eye-lift/see-tags/ignore-tags/filter");
    }
    entity.set<ActorSight>(std::move(sight));
    return s7_t(sc);
}

s7_pointer g_actor_sight_get(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "actor-sight-get", args);
    flecs::entity entity = g_thingWorld->lookup(in.string("id string"));
    if (!entity.is_valid() || !entity.has<ActorSight>()) {
        return s7_f(sc);
    }
    const ActorSight& sight = entity.get<ActorSight>();
    return makeAlist(
        sc,
        {
            {"enabled", s7_make_boolean(sc, sight.enabled)},
            {"range", s7_make_real(sc, sight.range)},
            {"fov", s7_make_real(sc, sight.fovDegrees)},
            {"eye-lift", s7_make_real(sc, sight.eyeLift)},
            {"see-tags", makeStringList(sc, sight.seeTags)},
            {"ignore-tags", makeStringList(sc, sight.ignoreTags)},
            {"filter",
             sight.filterProc.empty() ? s7_f(sc) : s7_make_string(sc, sight.filterProc.c_str())},
        });
}

s7_pointer g_actor_can_see(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "actor-can-see?", args);
    const char* fromId = in.string("from-id string");
    const char* toId = in.string("to-id string");
    return s7_make_boolean(sc, actorCanSee(*g_thingWorld, fromId, toId));
}

s7_pointer g_sight_budget(s7_scheme* sc, s7_pointer) {
    if (g_thingWorld == nullptr || !g_thingWorld->has<SightScanState>()) {
        return s7_f(sc);
    }
    return s7_make_integer(sc, g_thingWorld->get<SightScanState>().maxLosPerFrame);
}

s7_pointer g_sight_budget_set(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "sight-budget-set!", args);
    const int budget = static_cast<int>(in.exactInteger("integer"));
    if (!g_thingWorld->has<SightScanState>()) {
        g_thingWorld->set<SightScanState>(SightScanState{});
    }
    SightScanState& state = g_thingWorld->get_mut<SightScanState>();
    state.maxLosPerFrame = std::max(budget, 0);
    return s7_t(sc);
}

constexpr SchemeBinding kSightBindings[] = {
    {"actor-sight-set!", g_actor_sight_set, 2, 0, false, "(actor-sight-set! id alist)", ScriptCap::WorldMutate},
    {"actor-sight-get", g_actor_sight_get, 1, 0, false, "(actor-sight-get id)", ScriptCap::ReadWorld},
    {"actor-can-see?", g_actor_can_see, 2, 0, false, "(actor-can-see? from-id to-id)", ScriptCap::ReadWorld},
    {"sight-budget", g_sight_budget, 0, 0, false, "(sight-budget)", ScriptCap::ReadWorld},
    {"sight-budget-set!", g_sight_budget_set, 1, 0, false, "(sight-budget-set! n)", ScriptCap::WorldMutate},
};

s7_pointer g_thing_type(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "thing-type", args);
    flecs::entity entity = g_thingWorld->lookup(in.string("id string"));
    if (!entity.is_valid() || !entity.has<ThingTypeRef>()) {
        return s7_f(sc);
    }
    const std::string& type = entity.get<ThingTypeRef>().type;
    if (type.empty()) {
        return s7_f(sc);
    }
    return s7_make_string(sc, type.c_str());
}

s7_pointer g_thing_def_health(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "thing-def-health", args);
    const ThingDef* def = thingDefRegistry().find(in.string("type string"));
    if (def == nullptr || !def->health.has_value()) {
        return s7_f(sc);
    }
    return s7_make_integer(sc, *def->health);
}

s7_pointer g_thing_def_idle_anim(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "thing-def-idle-anim", args);
    const ThingDef* def = thingDefRegistry().find(in.string("type string"));
    if (def == nullptr || def->idleAnim.empty()) {
        return s7_f(sc);
    }
    return s7_make_string(sc, def->idleAnim.c_str());
}

s7_pointer g_thing_def_behavior(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "thing-def-behavior", args);
    const ThingDef* def = thingDefRegistry().find(in.string("type string"));
    if (def == nullptr || def->behavior.empty()) {
        return s7_f(sc);
    }
    return s7_make_string(sc, def->behavior.c_str());
}

const char* moveModeName(CharacterMoveMode mode) {
    switch (mode) {
    case CharacterMoveMode::TryMove:
        return "try-move";
    case CharacterMoveMode::Fly:
        return "fly";
    default:
        return "slide";
    }
}

s7_pointer g_thing_def_move_mode(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "thing-def-move-mode", args);
    const ThingDef* def = thingDefRegistry().find(in.string("type string"));
    if (def == nullptr || !def->haveMotor) {
        return s7_f(sc);
    }
    return s7_make_string(sc, moveModeName(def->motorMoveMode));
}

s7_pointer g_thing_def_gravity(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "thing-def-gravity", args);
    const ThingDef* def = thingDefRegistry().find(in.string("type string"));
    if (def == nullptr || !def->haveMotor) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(def->motorGravity));
}

s7_pointer g_thing_def_vertical_speed(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "thing-def-vertical-speed", args);
    const ThingDef* def = thingDefRegistry().find(in.string("type string"));
    if (def == nullptr || !def->haveMotor) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(def->motorVerticalSpeed));
}

s7_pointer g_thing_def_hover_height(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "thing-def-hover-height", args);
    const ThingDef* def = thingDefRegistry().find(in.string("type string"));
    if (def == nullptr || !def->haveMotor) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(def->motorHoverHeight));
}

s7_pointer g_thing_def_radius(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "thing-def-radius", args);
    const ThingDef* def = thingDefRegistry().find(in.string("type string"));
    if (def == nullptr || !def->haveMotor) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(def->motorRadius));
}

const ThingDefBehavior* findThingDefBehavior(const ThingDef* def, const char* name) {
    if (def == nullptr) {
        return nullptr;
    }
    for (const auto& behavior : def->behaviors) {
        if (behavior.name == name) {
            return &behavior;
        }
    }
    return nullptr;
}

s7_pointer thingDefBehaviorParamToScheme(s7_scheme* sc, const ThingDefBehaviorParam& param) {
    switch (param.kind) {
        case ThingDefBehaviorParam::Kind::String:
            return s7_make_string(sc, param.s.c_str());
        case ThingDefBehaviorParam::Kind::Bool:
            return param.b ? s7_t(sc) : s7_f(sc);
        case ThingDefBehaviorParam::Kind::Float:
        default:
            return s7_make_real(sc, static_cast<double>(param.f));
    }
}

s7_pointer g_thing_def_behavior_names(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "thing-def-behavior-names", args);
    const ThingDef* def = thingDefRegistry().find(in.string("type string"));
    std::vector<std::string> names;
    if (def != nullptr) {
        for (const auto& behavior : def->behaviors) {
            names.push_back(behavior.name);
        }
    }
    return makeStringList(sc, names);
}

s7_pointer g_thing_def_behavior_has(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "thing-def-behavior-has?", args);
    const ThingDef* def = thingDefRegistry().find(in.string("type string"));
    const char* behaviorName = in.string("name string");
    return s7_make_boolean(sc, findThingDefBehavior(def, behaviorName) != nullptr);
}

s7_pointer g_thing_def_behavior_params(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "thing-def-behavior-params", args);
    const ThingDef* def = thingDefRegistry().find(in.string("type string"));
    const char* behaviorName = in.string("name string");
    const ThingDefBehavior* behavior = findThingDefBehavior(def, behaviorName);
    if (behavior == nullptr) {
        return s7_f(sc);
    }
    s7_pointer alist = s7_nil(sc);
    for (auto it = behavior->params.rbegin(); it != behavior->params.rend(); ++it) {
        s7_pointer pair = s7_cons(
            sc,
            s7_make_symbol(sc, it->first.c_str()),
            thingDefBehaviorParamToScheme(sc, it->second));
        alist = s7_cons(sc, pair, alist);
    }
    return alist;
}

s7_pointer g_thing_def_speed(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "thing-def-speed", args);
    const ThingDef* def = thingDefRegistry().find(in.string("type string"));
    if (def == nullptr || !def->haveMotor) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(def->motorSpeed));
}

s7_pointer g_thing_def_pain_chance(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "thing-def-pain-chance", args);
    const ThingDef* def = thingDefRegistry().find(in.string("type string"));
    if (def == nullptr || !def->painChance.has_value()) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(*def->painChance));
}

s7_pointer g_thing_def_pain_threshold(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "thing-def-pain-threshold", args);
    const ThingDef* def = thingDefRegistry().find(in.string("type string"));
    if (def == nullptr || !def->painThreshold.has_value()) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(*def->painThreshold));
}

constexpr SchemeBinding kThingDefBindings[] = {
    {"thing-type", g_thing_type, 1, 0, false, "(thing-type id)", ScriptCap::ReadWorld},
    {"thing-def-health", g_thing_def_health, 1, 0, false, "(thing-def-health type)", ScriptCap::ReadWorld},
    {"thing-def-idle-anim", g_thing_def_idle_anim, 1, 0, false, "(thing-def-idle-anim type)", ScriptCap::ReadWorld},
    {"thing-def-behavior", g_thing_def_behavior, 1, 0, false, "(thing-def-behavior type)", ScriptCap::ReadWorld},
    {"thing-def-move-mode", g_thing_def_move_mode, 1, 0, false, "(thing-def-move-mode type)", ScriptCap::ReadWorld},
    {"thing-def-gravity", g_thing_def_gravity, 1, 0, false, "(thing-def-gravity type)", ScriptCap::ReadWorld},
    {"thing-def-vertical-speed", g_thing_def_vertical_speed, 1, 0, false, "(thing-def-vertical-speed type)", ScriptCap::ReadWorld},
    {"thing-def-hover-height", g_thing_def_hover_height, 1, 0, false, "(thing-def-hover-height type)", ScriptCap::ReadWorld},
    {"thing-def-radius", g_thing_def_radius, 1, 0, false, "(thing-def-radius type)", ScriptCap::ReadWorld},
    {"thing-def-behavior-names", g_thing_def_behavior_names, 1, 0, false, "(thing-def-behavior-names type)", ScriptCap::ReadWorld},
    {"thing-def-behavior-has?", g_thing_def_behavior_has, 2, 0, false, "(thing-def-behavior-has? type name)", ScriptCap::ReadWorld},
    {"thing-def-behavior-params", g_thing_def_behavior_params, 2, 0, false, "(thing-def-behavior-params type name)", ScriptCap::ReadWorld},
    {"thing-def-speed", g_thing_def_speed, 1, 0, false, "(thing-def-speed type)", ScriptCap::ReadWorld},
    {"thing-def-pain-chance", g_thing_def_pain_chance, 1, 0, false, "(thing-def-pain-chance type)", ScriptCap::ReadWorld},
    {"thing-def-pain-threshold", g_thing_def_pain_threshold, 1, 0, false, "(thing-def-pain-threshold type)", ScriptCap::ReadWorld},
};

} // namespace

flecs::entity severActorPart(flecs::entity entity, const std::string& boneName, Vector3 push, float speed, float spin,
                             float lifetime, bool makePiece) {
    return severActorPartImpl(entity, boneName, push, speed, spin, lifetime, makePiece);
}

bool ragdollActorBody(flecs::entity entity, Vector3 push, float speed, const std::string& hitBoneName, float lifetime) {
    return ragdollActorBodyImpl(entity, push, speed, hitBoneName, lifetime);
}

flecs::entity spawnNetPresentation(flecs::world& world, const std::string& name, const NetSpawnRecipe& recipe) {
    return spawnNetPresentationImpl(world, name, recipe);
}

std::vector<NetPose> captureCorpsePose(flecs::entity entity) {
    return captureCorpsePoseImpl(entity);
}

bool applyCorpsePose(flecs::entity entity, const std::vector<NetPose>& bones) {
    return applyCorpsePoseImpl(entity, bones);
}

bool hideActorBone(flecs::entity entity, const std::string& boneName) {
    return hideActorBoneImpl(entity, boneName);
}

bool attachActorGeo(flecs::entity host, const NetAttachment& attachment) {
    return attachActorGeoImpl(host, attachment.slot, attachment.bone, attachment.geo, attachment.place);
}

bool detachActorGeo(flecs::entity host, const std::string& slot) {
    return detachActorGeoImpl(host, slot);
}

void queueThingDespawn(flecs::world& world, std::string_view id) {
    if (id.empty() || isProtectedThingId(id)) {
        return;
    }
    if (!world.has<ThingDespawnQueue>()) {
        world.set<ThingDespawnQueue>({});
    }
    world.get_mut<ThingDespawnQueue>().ids.emplace_back(std::string(id));
}

void bindThingRuntimeApi(flecs::world& world, s7_scheme* scheme) {
    g_thingWorld = &world;
    if (!world.has<ThingDespawnQueue>()) {
        world.set<ThingDespawnQueue>({});
    }
    world.component<DebugLinePool>();
    if (!world.has<DebugLinePool>()) {
        world.set<DebugLinePool>({});
    }
    registerFlushThingDespawns(world);
    registerTimedDespawnSystem(world);

    if (scheme == nullptr) {
        return;
    }

    registerBindings(scheme, kSpawnBindings);
    registerBindings(scheme, kActorBindings);
    registerBindings(scheme, kWorldQueryBindings);
    registerBindings(scheme, kMoverBindings);
    registerBindings(scheme, kSightBindings);
    registerBindings(scheme, kThingDefBindings);
}

}
