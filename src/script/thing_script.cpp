#include "script/thing_script.hpp"
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
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr || !s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-despawn", 1, args, "thing-id string");
    }

    const char* id = s7_string(s7_car(args));
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
        });
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
            const float dt = GetFrameTime();
            if (dt <= 0.0f) {
                return;
            }
            timed.age += dt;
            if (timed.lifetime > 0.0f && timed.age >= timed.lifetime) {
                queueThingDespawn(world, entityIdString(entity));
            }
        });
}

bool readNumberArg(s7_scheme* sc, s7_pointer& args, float& out, const char* name, int index) {
    if (!s7_is_pair(args) || !s7_is_number(s7_car(args))) {
        return false;
    }
    out = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
    args = s7_cdr(args);
    (void)name;
    (void)index;
    return true;
}

s7_pointer g_motored_spawn(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "motored-spawn", 1, args, "id string");
    }
    const std::string id = s7_string(s7_car(args));
    args = s7_cdr(args);

    float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0;
    if (!readNumberArg(sc, args, x, "motored-spawn", 2) ||
        !readNumberArg(sc, args, y, "motored-spawn", 3) ||
        !readNumberArg(sc, args, z, "motored-spawn", 4) ||
        !readNumberArg(sc, args, vx, "motored-spawn", 5) ||
        !readNumberArg(sc, args, vy, "motored-spawn", 6) ||
        !readNumberArg(sc, args, vz, "motored-spawn", 7)) {
        return s7_wrong_type_arg_error(sc, "motored-spawn", 2, args, "x y z vx vy vz numbers");
    }

    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "motored-spawn", 8, args, "kind string");
    }
    const std::string kind = s7_string(s7_car(args));
    args = s7_cdr(args);

    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "motored-spawn", 9, args, "path string");
    }
    const std::string path = s7_string(s7_car(args));
    args = s7_cdr(args);

    float radius = 0.12f;
    float gravity = 0.0f;
    float lifetime = 8.0f;
    std::string onImpact;
    std::string ignoreId;
    if (s7_is_pair(args) && s7_is_number(s7_car(args))) {
        radius = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
        args = s7_cdr(args);
    }
    if (s7_is_pair(args) && s7_is_number(s7_car(args))) {
        gravity = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
        args = s7_cdr(args);
    }
    if (s7_is_pair(args) && s7_is_number(s7_car(args))) {
        lifetime = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
        args = s7_cdr(args);
    }
    if (s7_is_pair(args) && s7_is_string(s7_car(args))) {
        onImpact = s7_string(s7_car(args));
        args = s7_cdr(args);
    }
    if (s7_is_pair(args) && s7_is_string(s7_car(args))) {
        ignoreId = s7_string(s7_car(args));
        args = s7_cdr(args);
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
        TraceLog(LOG_WARNING, "motored-spawn: missing sprite '%s'", path.c_str());
        return s7_f(sc);
    }
    if (isGeo && !assets.hasGeo(path)) {
        TraceLog(LOG_WARNING, "motored-spawn: missing geo '%s'", path.c_str());
        return s7_f(sc);
    }

    flecs::entity entity = g_thingWorld->entity(id.c_str());
    LocalTransformation local{};
    local.position = {x, y, z};
    local.scale = {1.0f, 1.0f, 1.0f};
    local.rotation = isGeo ? motoredFacing({vx, vy, vz}) : QuaternionIdentity();

    const float horiz = std::sqrt(vx * vx + vz * vz);
    const float facingYaw = horiz > 1.0e-4f ? std::atan2(vx, vz) : 0.0f;

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

    MotoredBody body{};
    body.velocity = {vx, vy, vz};
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

FxLocalLight makeFxLocalLight(float r, float g, float b, float intensity, float range) {
    FxLocalLight light{};
    light.color.space = DynamicLightColorSpace::Rgb;
    light.color.value = {r, g, b};
    light.intensity = intensity;
    light.range = range;
    return light;
}

DynamicLight makePointDynamicLight(float r, float g, float b, float intensity, float range) {
    DynamicLight light{};
    light.kind = DynamicLightKind::Point;
    setDynamicLightRgb(light, {r, g, b});
    light.intensity = intensity;
    light.range = range;
    light.castShadows = false;
    return light;
}

s7_pointer g_fx_light_attach(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "fx-light-attach", 1, args, "id string");
    }
    const std::string id = s7_string(s7_car(args));
    args = s7_cdr(args);

    float r = 1;
    float g = 1;
    float b = 1;
    float intensity = 1;
    float range = 3;
    if (!readNumberArg(sc, args, r, "fx-light-attach", 2) ||
        !readNumberArg(sc, args, g, "fx-light-attach", 3) ||
        !readNumberArg(sc, args, b, "fx-light-attach", 4) ||
        !readNumberArg(sc, args, intensity, "fx-light-attach", 5) ||
        !readNumberArg(sc, args, range, "fx-light-attach", 6)) {
        return s7_wrong_type_arg_error(
            sc,
            "fx-light-attach",
            2,
            args,
            "r g b intensity range numbers");
    }

    flecs::entity entity = lookupThing(id);
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    entity.set<FxLocalLight>(makeFxLocalLight(r, g, b, intensity, range));
    return s7_t(sc);
}

s7_pointer g_dyn_light_attach(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "dyn-light-attach", 1, args, "id string");
    }
    const std::string id = s7_string(s7_car(args));
    args = s7_cdr(args);

    float r = 1;
    float g = 1;
    float b = 1;
    float intensity = 1;
    float range = 3;
    if (!readNumberArg(sc, args, r, "dyn-light-attach", 2) ||
        !readNumberArg(sc, args, g, "dyn-light-attach", 3) ||
        !readNumberArg(sc, args, b, "dyn-light-attach", 4) ||
        !readNumberArg(sc, args, intensity, "dyn-light-attach", 5) ||
        !readNumberArg(sc, args, range, "dyn-light-attach", 6)) {
        return s7_wrong_type_arg_error(
            sc,
            "dyn-light-attach",
            2,
            args,
            "r g b intensity range numbers");
    }

    flecs::entity entity = g_thingWorld->lookup(id.c_str());
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    const DynamicLight light = makePointDynamicLight(r, g, b, intensity, range);
    entity.set<DynamicLight>(light);
    return s7_t(sc);
}

s7_pointer g_dyn_light_set_pos(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "dyn-light-set-pos!", 1, args, "id string");
    }
    const std::string id = s7_string(s7_car(args));
    args = s7_cdr(args);

    float x = 0;
    float y = 0;
    float z = 0;
    if (!readNumberArg(sc, args, x, "dyn-light-set-pos!", 2) ||
        !readNumberArg(sc, args, y, "dyn-light-set-pos!", 3) ||
        !readNumberArg(sc, args, z, "dyn-light-set-pos!", 4)) {
        return s7_wrong_type_arg_error(sc, "dyn-light-set-pos!", 2, args, "x y z numbers");
    }

    flecs::entity entity = g_thingWorld->lookup(id.c_str());
    if (!entity.is_valid() || !entity.has<LocalTransformation>() ||
        !entity.has<GlobalTransformation>()) {
        return s7_f(sc);
    }
    LocalTransformation& local = entity.get_mut<LocalTransformation>();
    local.position = {x, y, z};
    updateTransform(entity, local, entity.get_mut<GlobalTransformation>());
    return s7_t(sc);
}

s7_pointer g_dyn_light_set_hsv(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "dyn-light-set-hsv!", 1, args, "id string");
    }
    const std::string id = s7_string(s7_car(args));
    args = s7_cdr(args);

    float h = 0;
    float s = 1;
    float v = 1;
    if (!readNumberArg(sc, args, h, "dyn-light-set-hsv!", 2) ||
        !readNumberArg(sc, args, s, "dyn-light-set-hsv!", 3) ||
        !readNumberArg(sc, args, v, "dyn-light-set-hsv!", 4)) {
        return s7_wrong_type_arg_error(sc, "dyn-light-set-hsv!", 2, args, "h s v numbers");
    }

    flecs::entity entity = g_thingWorld->lookup(id.c_str());
    if (!entity.is_valid() || !entity.has<DynamicLight>()) {
        return s7_f(sc);
    }
    DynamicLight light = entity.get<DynamicLight>();
    setDynamicLightHsv(light, {h, s, v});
    entity.set<DynamicLight>(light);
    return s7_t(sc);
}

s7_pointer g_dyn_light_spawn(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "dyn-light-spawn", 1, args, "id string");
    }
    const std::string id = s7_string(s7_car(args));
    args = s7_cdr(args);

    float x = 0;
    float y = 0;
    float z = 0;
    float r = 1;
    float g = 1;
    float b = 1;
    float intensity = 1;
    float range = 3;
    if (!readNumberArg(sc, args, x, "dyn-light-spawn", 2) ||
        !readNumberArg(sc, args, y, "dyn-light-spawn", 3) ||
        !readNumberArg(sc, args, z, "dyn-light-spawn", 4) ||
        !readNumberArg(sc, args, r, "dyn-light-spawn", 5) ||
        !readNumberArg(sc, args, g, "dyn-light-spawn", 6) ||
        !readNumberArg(sc, args, b, "dyn-light-spawn", 7) ||
        !readNumberArg(sc, args, intensity, "dyn-light-spawn", 8) ||
        !readNumberArg(sc, args, range, "dyn-light-spawn", 9)) {
        return s7_wrong_type_arg_error(
            sc,
            "dyn-light-spawn",
            2,
            args,
            "x y z r g b intensity range numbers");
    }

    float lifetime = 0.0f;
    if (s7_is_pair(args) && s7_is_number(s7_car(args))) {
        lifetime = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
        args = s7_cdr(args);
    }

    if (id.empty() || isProtectedThingId(id)) {
        return s7_f(sc);
    }
    if (g_thingWorld->lookup(id.c_str()).is_valid()) {
        return s7_f(sc);
    }

    DynamicLight light = makePointDynamicLight(r, g, b, intensity, range);
    if (lifetime > 0.0f) {
        light.castShadows = false;
    }
    flecs::entity entity = spawnDynamicLight(
        *g_thingWorld,
        id.c_str(),
        {x, y, z},
        QuaternionIdentity(),
        light);
    entity.add<MapOwned>();
    if (lifetime > 0.0f) {
        entity.set<TimedDespawn>({.age = 0.0f, .lifetime = lifetime});
    }
    return s7_t(sc);
}

s7_pointer g_fx_light_spawn(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "fx-light-spawn", 1, args, "id string");
    }
    const std::string id = s7_string(s7_car(args));
    args = s7_cdr(args);

    float x = 0;
    float y = 0;
    float z = 0;
    float r = 1;
    float g = 1;
    float b = 1;
    float intensity = 1;
    float range = 3;
    if (!readNumberArg(sc, args, x, "fx-light-spawn", 2) ||
        !readNumberArg(sc, args, y, "fx-light-spawn", 3) ||
        !readNumberArg(sc, args, z, "fx-light-spawn", 4) ||
        !readNumberArg(sc, args, r, "fx-light-spawn", 5) ||
        !readNumberArg(sc, args, g, "fx-light-spawn", 6) ||
        !readNumberArg(sc, args, b, "fx-light-spawn", 7) ||
        !readNumberArg(sc, args, intensity, "fx-light-spawn", 8) ||
        !readNumberArg(sc, args, range, "fx-light-spawn", 9)) {
        return s7_wrong_type_arg_error(
            sc,
            "fx-light-spawn",
            2,
            args,
            "x y z r g b intensity range numbers");
    }

    float lifetime = 0.0f;
    if (s7_is_pair(args) && s7_is_number(s7_car(args))) {
        lifetime = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
        args = s7_cdr(args);
    }

    if (id.empty() || isProtectedThingId(id)) {
        return s7_f(sc);
    }
    if (g_thingWorld->lookup(id.c_str()).is_valid()) {
        return s7_f(sc);
    }

    flecs::entity entity = spawnFxLocalLight(
        *g_thingWorld,
        id.c_str(),
        {x, y, z},
        makeFxLocalLight(r, g, b, intensity, range));
    entity.add<MapOwned>();
    if (lifetime > 0.0f) {
        entity.set<TimedDespawn>({.age = 0.0f, .lifetime = lifetime});
    }
    return s7_t(sc);
}

s7_pointer g_sprite_spawn(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "sprite-spawn", 1, args, "id string");
    }
    const std::string id = s7_string(s7_car(args));
    args = s7_cdr(args);

    float x = 0, y = 0, z = 0;
    if (!readNumberArg(sc, args, x, "sprite-spawn", 2) ||
        !readNumberArg(sc, args, y, "sprite-spawn", 3) ||
        !readNumberArg(sc, args, z, "sprite-spawn", 4)) {
        return s7_wrong_type_arg_error(sc, "sprite-spawn", 2, args, "x y z numbers");
    }

    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "sprite-spawn", 5, args, "path string");
    }
    const std::string path = s7_string(s7_car(args));
    args = s7_cdr(args);

    std::string clip;
    float lifetime = 0.5f;
    if (s7_is_pair(args) && s7_is_string(s7_car(args))) {
        clip = s7_string(s7_car(args));
        args = s7_cdr(args);
    }
    if (s7_is_pair(args) && s7_is_number(s7_car(args))) {
        lifetime = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
        args = s7_cdr(args);
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
    if (!assets.hasSprite(path)) {
        TraceLog(LOG_WARNING, "sprite-spawn: missing sprite '%s'", path.c_str());
        return s7_f(sc);
    }

    flecs::entity entity = g_thingWorld->entity(id.c_str());
    LocalTransformation local{};
    local.position = {x, y, z};
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
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "particle-spawn", 1, args, "id string");
    }
    const std::string id = s7_string(s7_car(args));
    args = s7_cdr(args);

    float x = 0, y = 0, z = 0;
    if (!readNumberArg(sc, args, x, "particle-spawn", 2) ||
        !readNumberArg(sc, args, y, "particle-spawn", 3) ||
        !readNumberArg(sc, args, z, "particle-spawn", 4)) {
        return s7_wrong_type_arg_error(sc, "particle-spawn", 2, args, "x y z numbers");
    }

    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "particle-spawn", 5, args, "path string");
    }
    const std::string path = s7_string(s7_car(args));
    args = s7_cdr(args);

    bool hasYaw = false;
    bool hasAim = false;
    float yaw = 0.0f;
    float dx = 0.0f;
    float dy = 1.0f;
    float dz = 0.0f;
    if (s7_is_pair(args) && s7_is_number(s7_car(args))) {
        const float first = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
        args = s7_cdr(args);
        if (s7_is_pair(args) && s7_is_number(s7_car(args))) {
            dx = first;
            if (!readNumberArg(sc, args, dy, "particle-spawn", 7) ||
                !readNumberArg(sc, args, dz, "particle-spawn", 8)) {
                return s7_wrong_type_arg_error(sc, "particle-spawn", 6, args, "dx dy dz numbers");
            }
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
            {x, y, z},
            {dx, dy, dz},
            path,
            true,
            true);
    } else {
        entity = spawnParticleSystem(
            *g_thingWorld,
            assets,
            id.c_str(),
            {x, y, z},
            hasYaw ? yaw : 0.0f,
            path,
            true,
            true);
    }
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    if (entity.has<ParticleSystemInstance>() && entity.has<GlobalTransformation>()) {
        ParticleSystemInstance& instance = entity.get_mut<ParticleSystemInstance>();
        tickParticleSystemInstance(
            instance, assets, entity.get<GlobalTransformation>().matrix, 1.0e-3f, {});
    }
    return s7_t(sc);
}

s7_pointer g_particle_spawn_fp(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "particle-spawn-fp", 1, args, "id string");
    }
    const std::string id = s7_string(s7_car(args));
    args = s7_cdr(args);

    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "particle-spawn-fp", 2, args, "socket string");
    }
    const std::string socket = s7_string(s7_car(args));
    args = s7_cdr(args);

    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "particle-spawn-fp", 3, args, "path string");
    }
    const std::string path = s7_string(s7_car(args));
    args = s7_cdr(args);

    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "particle-spawn-fp", 4, args, "attach string");
    }
    const std::string attach = s7_string(s7_car(args));
    args = s7_cdr(args);

    float depth = 0.35f;
    if (s7_is_pair(args) && s7_is_number(s7_car(args))) {
        depth = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
        args = s7_cdr(args);
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
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "particle-spawn-attach", 1, args, "id string");
    }
    const std::string id = s7_string(s7_car(args));
    args = s7_cdr(args);

    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "particle-spawn-attach", 2, args, "target string");
    }
    const std::string target = s7_string(s7_car(args));
    args = s7_cdr(args);

    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "particle-spawn-attach", 3, args, "path string");
    }
    const std::string path = s7_string(s7_car(args));
    args = s7_cdr(args);

    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "particle-spawn-attach", 4, args, "attach string");
    }
    const std::string attach = s7_string(s7_car(args));

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
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    std::string fields[3];
    const char* expected[3] = {"id string", "target string", "path string"};
    for (int index = 0; index < 3; ++index) {
        if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
            return s7_wrong_type_arg_error(sc, "particle-spawn-follow", index + 1, args, expected[index]);
        }
        fields[index] = s7_string(s7_car(args));
        args = s7_cdr(args);
    }
    float back = 0.0f;
    if (s7_is_pair(args) && !readNumberArg(sc, args, back, "particle-spawn-follow", 4)) {
        return s7_wrong_type_arg_error(sc, "particle-spawn-follow", 4, args, "back number");
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
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    std::string fields[4];
    const char* expected[4] = {"id string", "target string", "bone string", "path string"};
    for (int index = 0; index < 4; ++index) {
        if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
            return s7_wrong_type_arg_error(sc, "particle-spawn-bone", index + 1, args, expected[index]);
        }
        fields[index] = s7_string(s7_car(args));
        args = s7_cdr(args);
    }
    bool fromParent = false;
    if (s7_is_pair(args)) {
        s7_pointer mode = s7_car(args);
        const char* name = s7_is_symbol(mode) ? s7_symbol_name(mode) : (s7_is_string(mode) ? s7_string(mode) : nullptr);
        if (name != nullptr && std::strcmp(name, "stump") == 0) {
            fromParent = true;
        } else if (name == nullptr || std::strcmp(name, "limb") != 0) {
            return s7_wrong_type_arg_error(sc, "particle-spawn-bone", 5, args, "'limb or 'stump");
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
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "trail-spawn", 1, args, "id string");
    }
    const std::string id = s7_string(s7_car(args));
    args = s7_cdr(args);

    float sx = 0, sy = 0, sz = 0, ex = 0, ey = 0, ez = 0;
    if (!readNumberArg(sc, args, sx, "trail-spawn", 2) ||
        !readNumberArg(sc, args, sy, "trail-spawn", 3) ||
        !readNumberArg(sc, args, sz, "trail-spawn", 4) ||
        !readNumberArg(sc, args, ex, "trail-spawn", 5) ||
        !readNumberArg(sc, args, ey, "trail-spawn", 6) ||
        !readNumberArg(sc, args, ez, "trail-spawn", 7)) {
        return s7_wrong_type_arg_error(sc, "trail-spawn", 2, args, "sx sy sz ex ey ez numbers");
    }

    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "trail-spawn", 8, args, "path string");
    }
    const std::string path = s7_string(s7_car(args));
    args = s7_cdr(args);

    float lifetime = 0.12f;
    if (!readNumberArg(sc, args, lifetime, "trail-spawn", 9)) {
        return s7_wrong_type_arg_error(sc, "trail-spawn", 9, args, "lifetime number");
    }

    float width = 0.08f;
    if (s7_is_pair(args) && s7_is_number(s7_car(args))) {
        width = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
        args = s7_cdr(args);
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
    if (!assets.hasTexture(path)) {
        TraceLog(LOG_WARNING, "trail-spawn: missing texture '%s'", path.c_str());
        return s7_f(sc);
    }

    flecs::entity entity = spawnTrail(
        *g_thingWorld,
        id.c_str(),
        Vector3{sx, sy, sz},
        Vector3{ex, ey, ez},
        path,
        lifetime,
        width,
        true);
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
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    float sx = 0, sy = 0, sz = 0, ex = 0, ey = 0, ez = 0;
    if (!readNumberArg(sc, args, sx, "tracer-spawn", 1) ||
        !readNumberArg(sc, args, sy, "tracer-spawn", 2) ||
        !readNumberArg(sc, args, sz, "tracer-spawn", 3) ||
        !readNumberArg(sc, args, ex, "tracer-spawn", 4) ||
        !readNumberArg(sc, args, ey, "tracer-spawn", 5) ||
        !readNumberArg(sc, args, ez, "tracer-spawn", 6)) {
        return s7_wrong_type_arg_error(sc, "tracer-spawn", 1, args, "sx sy sz ex ey ez numbers");
    }
    TracerStyle style{};
    float lead = 0.0f;
    if (s7_is_pair(args)) {
        s7_pointer alist = s7_car(args);
        if (!s7_is_pair(alist) && !s7_is_null(sc, alist)) {
            return s7_wrong_type_arg_error(sc, "tracer-spawn", 7, alist, "style alist");
        }
        for (s7_pointer cursor = alist; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
            s7_pointer entry = s7_car(cursor);
            if (!s7_is_pair(entry) || !s7_is_symbol(s7_car(entry)) ||
                !applyTracerStyleEntry(sc, s7_symbol_name(s7_car(entry)), s7_cdr(entry), style, lead)) {
                return s7_wrong_type_arg_error(sc, "tracer-spawn", 7, entry, "style alist");
            }
        }
    }
    spawnTracer(*g_thingWorld, Vector3{sx, sy, sz}, Vector3{ex, ey, ez}, style, lead);
    return s7_t(sc);
}

s7_pointer g_trail_spawn_fp(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "trail-spawn-fp", 1, args, "id string");
    }
    const std::string id = s7_string(s7_car(args));
    args = s7_cdr(args);

    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "trail-spawn-fp", 2, args, "socket string");
    }
    const std::string socket = s7_string(s7_car(args));
    args = s7_cdr(args);

    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "trail-spawn-fp", 3, args, "path string");
    }
    const std::string path = s7_string(s7_car(args));
    args = s7_cdr(args);

    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "trail-spawn-fp", 4, args, "attach string");
    }
    const std::string attach = s7_string(s7_car(args));
    args = s7_cdr(args);

    float ex = 0, ey = 0, ez = 0, lifetime = 0.12f;
    if (!readNumberArg(sc, args, ex, "trail-spawn-fp", 5) ||
        !readNumberArg(sc, args, ey, "trail-spawn-fp", 6) ||
        !readNumberArg(sc, args, ez, "trail-spawn-fp", 7) ||
        !readNumberArg(sc, args, lifetime, "trail-spawn-fp", 8)) {
        return s7_wrong_type_arg_error(sc, "trail-spawn-fp", 5, args, "ex ey ez lifetime numbers");
    }

    float width = 0.08f;
    if (s7_is_pair(args) && s7_is_number(s7_car(args))) {
        width = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
        args = s7_cdr(args);
    }
    float depth = 0.35f;
    if (s7_is_pair(args) && s7_is_number(s7_car(args))) {
        depth = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
        args = s7_cdr(args);
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
        Vector3{ex, ey, ez},
        path,
        lifetime,
        width,
        true);
    return entity.is_valid() ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_particle_play(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr || !s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_f(sc);
    }
    flecs::entity entity = g_thingWorld->lookup(s7_string(s7_car(args)));
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
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr || !s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_f(sc);
    }
    flecs::entity entity = g_thingWorld->lookup(s7_string(s7_car(args)));
    if (!entity.is_valid() || !entity.has<ParticleSystemInstance>()) {
        return s7_f(sc);
    }
    entity.get_mut<ParticleSystemInstance>().playing = false;
    return s7_t(sc);
}

s7_pointer g_particle_despawn(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr || !s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_f(sc);
    }
    const std::string id = s7_string(s7_car(args));
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

s7_pointer g_actor_spawn(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-spawn", 1, args, "id string");
    }
    const std::string id = s7_string(s7_car(args));
    args = s7_cdr(args);

    float x = 0, y = 0, z = 0, yaw = 0;
    if (!readNumberArg(sc, args, x, "actor-spawn", 2) ||
        !readNumberArg(sc, args, y, "actor-spawn", 3) ||
        !readNumberArg(sc, args, z, "actor-spawn", 4) ||
        !readNumberArg(sc, args, yaw, "actor-spawn", 5)) {
        return s7_wrong_type_arg_error(sc, "actor-spawn", 2, args, "x y z yaw numbers");
    }

    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-spawn", 6, args, "kind string");
    }
    const std::string kind = s7_string(s7_car(args));
    args = s7_cdr(args);

    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-spawn", 7, args, "path string");
    }
    const std::string path = s7_string(s7_car(args));
    args = s7_cdr(args);

    float radius = 0.3f;
    float height = 1.1f;
    float speed = 6.0f;
    float gravity = 9.81f;
    if (s7_is_pair(args) && s7_is_number(s7_car(args))) {
        radius = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
        args = s7_cdr(args);
    }
    if (s7_is_pair(args) && s7_is_number(s7_car(args))) {
        height = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
        args = s7_cdr(args);
    }
    if (s7_is_pair(args) && s7_is_number(s7_car(args))) {
        speed = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
        args = s7_cdr(args);
    }
    if (s7_is_pair(args) && s7_is_number(s7_car(args))) {
        gravity = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
        args = s7_cdr(args);
    }

    std::vector<std::string> tags;
    if (s7_is_pair(args) && s7_is_list(sc, s7_car(args))) {
        for (s7_pointer cursor = s7_car(args); s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
            if (s7_is_string(s7_car(cursor))) {
                tags.emplace_back(s7_string(s7_car(cursor)));
            }
        }
        args = s7_cdr(args);
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
    local.position = {x, y, z};
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
    entity.add<Actor>().set<CharacterMotor>(motor).set<NavigationAgent>(NavigationAgent{}).set<CollisionTags>(CollisionTags{std::move(tags)});

    PhysicsWorld* physics = physicsWorld();
    if (physics != nullptr) {
        physics->createCharacter(static_cast<std::uint64_t>(entity.id()), x, y, z, motor);
    }

    return s7_t(sc);
}

s7_pointer g_actor_pos(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-pos", 1, args, "id string");
    }
    flecs::entity entity = lookupActor(s7_string(s7_car(args)));
    if (!entity.is_valid() || !entity.has<LocalTransformation>()) {
        return s7_f(sc);
    }
    const Vector3 pos = entity.get<LocalTransformation>().position;
    return s7_list(
        sc,
        3,
        s7_make_real(sc, pos.x),
        s7_make_real(sc, pos.y),
        s7_make_real(sc, pos.z));
}

s7_pointer g_actor_yaw(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-yaw", 1, args, "id string");
    }
    flecs::entity entity = lookupActor(s7_string(s7_car(args)));
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

s7_pointer g_thing_pos(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr || !s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-pos", 1, args, "id string");
    }
    flecs::entity entity = g_thingWorld->lookup(s7_string(s7_car(args)));
    if (!entity.is_valid() || !entity.has<LocalTransformation>()) {
        return s7_f(sc);
    }
    const Vector3 pos = entity.get<LocalTransformation>().position;
    return s7_list(
        sc,
        3,
        s7_make_real(sc, pos.x),
        s7_make_real(sc, pos.y),
        s7_make_real(sc, pos.z));
}

s7_pointer g_thing_yaw(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr || !s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-yaw", 1, args, "id string");
    }
    flecs::entity entity = g_thingWorld->lookup(s7_string(s7_car(args)));
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

s7_pointer g_actor_set_corpse(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-set-corpse!", 1, args, "id string");
    }
    flecs::entity entity = lookupActor(s7_string(s7_car(args)));
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
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "sprite-hide-part!", 1, args, "id string");
    }
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest) || !s7_is_string(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "sprite-hide-part!", 2, rest, "part-name string");
    }
    flecs::entity entity = lookupActor(s7_string(s7_car(args)));
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

    const std::string partName = s7_string(s7_car(rest));
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
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-set-wish", 1, args, "id string");
    }
    const char* id = s7_string(s7_car(args));
    s7_pointer rest = s7_cdr(args);
    float wx = 0.0f;
    float wz = 0.0f;
    if (!readNumberArg(sc, rest, wx, "actor-set-wish", 2) ||
        !readNumberArg(sc, rest, wz, "actor-set-wish", 3)) {
        return s7_wrong_type_arg_error(sc, "actor-set-wish", 2, rest, "wx wz numbers");
    }
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
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-set-wish-3d", 1, args, "id string");
    }
    const char* id = s7_string(s7_car(args));
    s7_pointer rest = s7_cdr(args);
    float wx = 0.0f;
    float wy = 0.0f;
    float wz = 0.0f;
    if (!readNumberArg(sc, rest, wx, "actor-set-wish-3d", 2) ||
        !readNumberArg(sc, rest, wy, "actor-set-wish-3d", 3) ||
        !readNumberArg(sc, rest, wz, "actor-set-wish-3d", 4)) {
        return s7_wrong_type_arg_error(sc, "actor-set-wish-3d", 2, rest, "wx wy wz numbers");
    }
    flecs::entity entity = lookupActor(id);
    if (!entity.is_valid() || !entity.has<CharacterMotor>()) {
        return s7_f(sc);
    }
    CharacterMotor& motor = entity.get_mut<CharacterMotor>();
    motor.wishX = wx;
    motor.wishY = wy;
    motor.wishZ = wz;
    return s7_t(sc);
}

s7_pointer g_actor_set_move_speed(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-set-move-speed!", 1, args, "id string");
    }
    const char* id = s7_string(s7_car(args));
    s7_pointer rest = s7_cdr(args);
    float speed = 0.0f;
    if (!readNumberArg(sc, rest, speed, "actor-set-move-speed!", 2)) {
        return s7_wrong_type_arg_error(sc, "actor-set-move-speed!", 2, rest, "speed number");
    }
    flecs::entity entity = lookupActor(id);
    if (!entity.is_valid() || !entity.has<CharacterMotor>()) {
        return s7_f(sc);
    }
    CharacterMotor& motor = entity.get_mut<CharacterMotor>();
    motor.moveSpeed = speed;
    return s7_t(sc);
}

s7_pointer g_actor_set_yaw(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-set-yaw!", 1, args, "id string");
    }
    const char* id = s7_string(s7_car(args));
    s7_pointer rest = s7_cdr(args);
    float yaw = 0.0f;
    if (!readNumberArg(sc, rest, yaw, "actor-set-yaw!", 2)) {
        return s7_wrong_type_arg_error(sc, "actor-set-yaw!", 2, rest, "yaw number");
    }
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

s7_pointer boneScaleArgsError(s7_scheme* sc, s7_pointer args, const char* name) {
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, name, 1, args, "id string");
    }
    if (!s7_is_pair(s7_cdr(args)) || !s7_is_string(s7_cadr(args))) {
        return s7_wrong_type_arg_error(sc, name, 2, s7_cdr(args), "bone name string");
    }
    return nullptr;
}

flecs::entity boneScaleActor(s7_pointer args, std::string& bone) {
    bone = s7_string(s7_cadr(args));
    flecs::entity entity = lookupActor(s7_string(s7_car(args)));
    if (!entity.is_valid() || !entity.has<Model3D>() ||
        findModelBoneIndex(entity.get<Model3D>().model, bone) < 0) {
        return {};
    }
    return entity;
}

s7_pointer g_actor_set_bone_scale(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (s7_pointer error = boneScaleArgsError(sc, args, "actor-set-bone-scale!")) {
        return error;
    }
    std::string bone;
    flecs::entity entity = boneScaleActor(args, bone);
    s7_pointer rest = s7_cddr(args);
    BoneScale scale{};
    if (!readNumberArg(sc, rest, scale.size, "actor-set-bone-scale!", 3) || scale.size <= 0.0f) {
        return s7_wrong_type_arg_error(sc, "actor-set-bone-scale!", 3, rest, "positive size");
    }
    if (s7_is_pair(rest) && (!readNumberArg(sc, rest, scale.length, "actor-set-bone-scale!", 4) || scale.length <= 0.0f)) {
        return s7_wrong_type_arg_error(sc, "actor-set-bone-scale!", 4, rest, "positive length");
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
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (s7_pointer error = boneScaleArgsError(sc, args, "actor-bone-scale")) {
        return error;
    }
    std::string bone;
    flecs::entity entity = boneScaleActor(args, bone);
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
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-clear-bone-scales!", 1, args, "id string");
    }
    flecs::entity entity = lookupActor(s7_string(s7_car(args)));
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
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-set-shader-param!", 1, args, "id string");
    }
    if (!s7_is_pair(s7_cdr(args)) || !s7_is_string(s7_cadr(args)) || s7_string(s7_cadr(args))[0] == '\0') {
        return s7_wrong_type_arg_error(sc, "actor-set-shader-param!", 2, s7_cdr(args), "uniform name string");
    }
    float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    s7_pointer rest = s7_cddr(args);
    for (int i = 0; i < 4 && s7_is_pair(rest); ++i) {
        if (!readNumberArg(sc, rest, values[i], "actor-set-shader-param!", 3 + i)) {
            return s7_wrong_type_arg_error(sc, "actor-set-shader-param!", 3 + i, rest, "number");
        }
    }
    flecs::entity entity = lookupActor(s7_string(s7_car(args)));
    if (!entity.is_valid() || !entity.has<Model3D>()) {
        return s7_f(sc);
    }
    const std::string name = s7_string(s7_cadr(args));
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
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-clear-shader-params!", 1, args, "id string");
    }
    flecs::entity entity = lookupActor(s7_string(s7_car(args)));
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    g_pendingShaderParams.erase(entity.id());
    entity.remove<ShaderParams>();
    return s7_t(sc);
}

constexpr int kMaxSeveredPieces = 48;
std::uint64_t g_severedPieceCounter = 0;

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

flecs::entity riggedActor(s7_scheme* sc, s7_pointer args, const char* caller, std::string& bone, s7_pointer& error) {
    error = nullptr;
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        error = s7_wrong_type_arg_error(sc, caller, 1, args, "id string");
        return {};
    }
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest) || !s7_is_string(s7_car(rest))) {
        error = s7_wrong_type_arg_error(sc, caller, 2, rest, "bone string");
        return {};
    }
    bone = s7_string(s7_car(rest));
    flecs::entity entity = lookupActor(s7_string(s7_car(args)));
    if (!entity.is_valid() || !entity.has<Model3D>() || !entity.has<GlobalTransformation>()) {
        return {};
    }
    const Model& model = entity.get<Model3D>().model;
    if (model.skeleton.boneCount <= 0 || model.boneMatrices == nullptr || model.currentPose == nullptr) {
        return {};
    }
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

s7_pointer g_actor_sever_bone(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    std::string boneName;
    s7_pointer error = nullptr;
    flecs::entity entity = riggedActor(sc, args, "actor-sever-bone!", boneName, error);
    if (error != nullptr) {
        return error;
    }
    s7_pointer rest = s7_cddr(args);
    Vector3 push{0.0f, 0.0f, 0.0f};
    float speed = 3.0f;
    float spin = 6.0f;
    float lifetime = 20.0f;
    if (s7_is_pair(rest) &&
        (!readNumberArg(sc, rest, push.x, "actor-sever-bone!", 3) ||
         !readNumberArg(sc, rest, push.y, "actor-sever-bone!", 4) ||
         !readNumberArg(sc, rest, push.z, "actor-sever-bone!", 5))) {
        return s7_wrong_type_arg_error(sc, "actor-sever-bone!", 3, rest, "dx dy dz numbers");
    }
    if (s7_is_pair(rest) && !readNumberArg(sc, rest, speed, "actor-sever-bone!", 6)) {
        return s7_wrong_type_arg_error(sc, "actor-sever-bone!", 6, rest, "speed number");
    }
    if (s7_is_pair(rest) && !readNumberArg(sc, rest, spin, "actor-sever-bone!", 7)) {
        return s7_wrong_type_arg_error(sc, "actor-sever-bone!", 7, rest, "spin number");
    }
    if (s7_is_pair(rest) && !readNumberArg(sc, rest, lifetime, "actor-sever-bone!", 8)) {
        return s7_wrong_type_arg_error(sc, "actor-sever-bone!", 8, rest, "lifetime number");
    }
    if (!entity.is_valid() || !entity.has<GeoSource>() || !g_thingWorld->has<AssetServices>() ||
        g_thingWorld->get<AssetServices>().store == nullptr) {
        return s7_f(sc);
    }
    const Model& model = entity.get<Model3D>().model;
    const int boneCount = model.skeleton.boneCount;
    const int boneIndex = findModelBoneIndex(model, boneName);
    if (boneIndex < 0) {
        return s7_f(sc);
    }
    HiddenBones* hiddenPtr = actorHiddenBones(entity, boneCount);
    if (hiddenPtr == nullptr || hiddenPtr->mask[static_cast<std::size_t>(boneIndex)] != 0) {
        return s7_f(sc);
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
        return s7_f(sc);
    }
    const Vector3 center = Vector3Scale(Vector3Add(bounds.min, bounds.max), 0.5f);
    const Vector3 half = Vector3Scale(Vector3Subtract(bounds.max, bounds.min), 0.5f);

    AssetStore& assets = *g_thingWorld->get_mut<AssetServices>().store;
    const std::string geoPath = entity.get<GeoSource>().path;
    Model piece = cloneGeoModelInstance(model);
    if (piece.meshCount <= 0 || piece.skeleton.boneCount != boneCount || piece.boneMatrices == nullptr) {
        unloadClonedGeoModelInstance(piece);
        return s7_f(sc);
    }
    std::vector<std::uint8_t> inside(static_cast<std::size_t>(boneCount), 0);
    for (int bone : subtree) {
        inside[static_cast<std::size_t>(bone)] = 1;
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

    const std::string pieceName = "severed-" + std::to_string(++g_severedPieceCounter);
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
    return s7_make_string(sc, pieceName.c_str());
}

s7_pointer g_actor_hide_bone(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    std::string boneName;
    s7_pointer error = nullptr;
    flecs::entity entity = riggedActor(sc, args, "actor-hide-bone!", boneName, error);
    if (error != nullptr) {
        return error;
    }
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    const Model& model = entity.get<Model3D>().model;
    const int boneIndex = findModelBoneIndex(model, boneName);
    HiddenBones* hidden = actorHiddenBones(entity, model.skeleton.boneCount);
    if (boneIndex < 0 || hidden == nullptr || hidden->mask[static_cast<std::size_t>(boneIndex)] != 0) {
        return s7_f(sc);
    }
    for (int bone : boneSubtree(model, boneIndex)) {
        hidden->mask[static_cast<std::size_t>(bone)] = 1;
    }
    hidden->dirty = true;
    return s7_t(sc);
}

s7_pointer g_actor_ragdoll(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-ragdoll!", 1, args, "id string");
    }
    flecs::entity entity = lookupActor(s7_string(s7_car(args)));
    s7_pointer rest = s7_cdr(args);
    Vector3 push{0.0f, 0.0f, 0.0f};
    float speed = 3.0f;
    std::string hitBoneName;
    float lifetime = 30.0f;
    if (s7_is_pair(rest) &&
        (!readNumberArg(sc, rest, push.x, "actor-ragdoll!", 2) ||
         !readNumberArg(sc, rest, push.y, "actor-ragdoll!", 3) ||
         !readNumberArg(sc, rest, push.z, "actor-ragdoll!", 4))) {
        return s7_wrong_type_arg_error(sc, "actor-ragdoll!", 2, rest, "dx dy dz numbers");
    }
    if (s7_is_pair(rest) && !readNumberArg(sc, rest, speed, "actor-ragdoll!", 5)) {
        return s7_wrong_type_arg_error(sc, "actor-ragdoll!", 5, rest, "speed number");
    }
    if (s7_is_pair(rest)) {
        if (s7_is_string(s7_car(rest))) {
            hitBoneName = s7_string(s7_car(rest));
        } else if (s7_car(rest) != s7_f(sc)) {
            return s7_wrong_type_arg_error(sc, "actor-ragdoll!", 6, rest, "hit bone string or #f");
        }
        rest = s7_cdr(rest);
    }
    if (s7_is_pair(rest) && !readNumberArg(sc, rest, lifetime, "actor-ragdoll!", 7)) {
        return s7_wrong_type_arg_error(sc, "actor-ragdoll!", 7, rest, "lifetime number");
    }
    PhysicsWorld* physics = physicsWorld();
    if (!entity.is_valid() || physics == nullptr || !entity.has<Model3D>() || !entity.has<GlobalTransformation>() ||
        entity.has<RagdollRig>()) {
        return s7_f(sc);
    }
    const Model& model = entity.get<Model3D>().model;
    const int boneCount = model.skeleton.boneCount;
    if (boneCount <= 0 || model.boneMatrices == nullptr || model.currentPose == nullptr) {
        return s7_f(sc);
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
        return s7_f(sc);
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
    return s7_t(sc);
}

s7_pointer g_physics_blast(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    Vector3 center{0.0f, 0.0f, 0.0f};
    float radius = 0.0f;
    float speed = 0.0f;
    float lift = 0.0f;
    if (!readNumberArg(sc, args, center.x, "physics-blast!", 1) ||
        !readNumberArg(sc, args, center.y, "physics-blast!", 2) ||
        !readNumberArg(sc, args, center.z, "physics-blast!", 3) ||
        !readNumberArg(sc, args, radius, "physics-blast!", 4) ||
        !readNumberArg(sc, args, speed, "physics-blast!", 5)) {
        return s7_wrong_type_arg_error(sc, "physics-blast!", 1, args, "x y z radius speed numbers");
    }
    if (s7_is_pair(args) && !readNumberArg(sc, args, lift, "physics-blast!", 6)) {
        return s7_wrong_type_arg_error(sc, "physics-blast!", 6, args, "lift number");
    }
    PhysicsWorld* physics = physicsWorld();
    if (physics == nullptr) {
        return s7_make_integer(sc, 0);
    }
    return s7_make_integer(sc, physics->blast(center, radius, speed, lift));
}

s7_pointer g_actor_bone_severed(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    std::string boneName;
    s7_pointer error = nullptr;
    flecs::entity entity = riggedActor(sc, args, "actor-bone-severed?", boneName, error);
    if (error != nullptr) {
        return error;
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
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-bone-springs!", 1, args, "id string");
    }
    s7_pointer specs = s7_is_pair(s7_cdr(args)) ? s7_cadr(args) : s7_nil(sc);
    if (!s7_is_list(sc, specs)) {
        return s7_wrong_type_arg_error(
            sc, "actor-bone-springs!", 2, s7_cdr(args), "list of (bone param stiffness damping gain limit)");
    }
    BoneSprings springs;
    for (s7_pointer entries = specs; s7_is_pair(entries); entries = s7_cdr(entries)) {
        s7_pointer entry = s7_car(entries);
        if (!s7_is_pair(entry) || !s7_is_string(s7_car(entry)) || !s7_is_pair(s7_cdr(entry)) ||
            !s7_is_string(s7_cadr(entry))) {
            return s7_wrong_type_arg_error(
                sc, "actor-bone-springs!", 2, entry, "(bone param stiffness damping gain limit)");
        }
        BoneSpringSpec spec;
        spec.bone = s7_string(s7_car(entry));
        spec.param = s7_string(s7_cadr(entry));
        s7_pointer rest = s7_cddr(entry);
        float* fields[4] = {&spec.stiffness, &spec.damping, &spec.gain, &spec.limit};
        for (int i = 0; i < 4; ++i) {
            if (!readNumberArg(sc, rest, *fields[i], "actor-bone-springs!", 3 + i)) {
                return s7_wrong_type_arg_error(sc, "actor-bone-springs!", 2, entry, "four spring numbers");
            }
        }
        springs.specs.push_back(std::move(spec));
    }
    if (s7_is_pair(s7_cdr(args)) && s7_is_pair(s7_cddr(args)) && s7_is_pair(s7_caddr(args))) {
        s7_pointer rest = s7_caddr(args);
        float* fields[3] = {&springs.tuning.step, &springs.tuning.maxDt, &springs.tuning.accelSmoothing};
        for (int i = 0; i < 3 && s7_is_pair(rest); ++i) {
            if (!readNumberArg(sc, rest, *fields[i], "actor-bone-springs!", 3)) {
                return s7_wrong_type_arg_error(sc, "actor-bone-springs!", 3, rest, "(step max-dt accel-smoothing)");
            }
        }
    }
    flecs::entity entity = lookupActor(s7_string(s7_car(args)));
    if (!entity.is_valid() || !entity.has<Model3D>()) {
        return s7_f(sc);
    }
    entity.set<BoneSprings>(springs);
    g_pendingBoneSprings[entity.id()] = std::move(springs);
    return s7_t(sc);
}

s7_pointer g_actor_bone_springs_active(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-bone-springs-active!", 1, args, "id string");
    }
    const bool active = s7_is_pair(s7_cdr(args)) && s7_cadr(args) != s7_f(sc);
    flecs::entity entity = lookupActor(s7_string(s7_car(args)));
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
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    std::string boneName;
    s7_pointer error = nullptr;
    flecs::entity entity = riggedActor(sc, args, "actor-bone-pos", boneName, error);
    if (error != nullptr) {
        return error;
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
    return s7_list(
        sc, 3, s7_make_real(sc, joint.m12), s7_make_real(sc, joint.m13), s7_make_real(sc, joint.m14));
}

s7_pointer g_actor_restore_bones(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-restore-bones!", 1, args, "id string");
    }
    flecs::entity entity = lookupActor(s7_string(s7_car(args)));
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
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-grounded?", 1, args, "id string");
    }
    flecs::entity entity = lookupActor(s7_string(s7_car(args)));
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
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-jump!", 1, args, "id string");
    }
    const char* id = s7_string(s7_car(args));
    s7_pointer rest = s7_cdr(args);
    float speed = 0.0f;
    if (!readNumberArg(sc, rest, speed, "actor-jump!", 2)) {
        return s7_wrong_type_arg_error(sc, "actor-jump!", 2, rest, "speed number");
    }
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
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-submerged?", 1, args, "id string");
    }
    flecs::entity entity = lookupActor(s7_string(s7_car(args)));
    if (!entity.is_valid() || !entity.has<CharacterMotor>()) {
        return s7_f(sc);
    }
    return entity.get<CharacterMotor>().submersion > 0.0f ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_actor_near_water_exit(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-near-water-exit?", 1, args, "id string");
    }
    flecs::entity entity = lookupActor(s7_string(s7_car(args)));
    if (!entity.is_valid() || !entity.has<CharacterMotor>()) {
        return s7_f(sc);
    }
    const CharacterMotor& motor = entity.get<CharacterMotor>();
    return (motor.submersion > 0.0f && motor.nearWaterSurface) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_actor_water_exit_dir(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-water-exit-dir", 1, args, "id string");
    }
    flecs::entity entity = lookupActor(s7_string(s7_car(args)));
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

s7_pointer g_actor_attach_geo(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    const char* names[4] = {"id", "name", "bone", "geo"};
    std::string values[4];
    for (int i = 0; i < 4; ++i) {
        if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
            return s7_wrong_type_arg_error(sc, "actor-attach-geo", i + 1, args, names[i]);
        }
        values[i] = s7_string(s7_car(args));
        args = s7_cdr(args);
    }
    float numbers[7] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    for (float& value : numbers) {
        if (!s7_is_pair(args) || !s7_is_number(s7_car(args))) {
            break;
        }
        value = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
        args = s7_cdr(args);
    }

    flecs::entity host = resolveActorPath(values[0]);
    if (!host.is_valid() || !host.has<Model3D>()) {
        return s7_f(sc);
    }
    if (!g_thingWorld->has<AssetServices>() || g_thingWorld->get<AssetServices>().store == nullptr) {
        return s7_f(sc);
    }
    AssetStore& assets = *g_thingWorld->get_mut<AssetServices>().store;
    const Model& hostModel = host.get<Model3D>().model;
    const GeoAsset* hostGeo =
        host.has<GeoSource>() ? assets.getGeoAsset(host.get<GeoSource>().path) : nullptr;
    BoneAttachment attachment{};
    if (!resolveBoneAttachment(
            hostModel,
            hostGeo,
            values[2],
            {numbers[0], numbers[1], numbers[2]},
            {numbers[3], numbers[4], numbers[5]},
            numbers[6],
            attachment)) {
        TraceLog(
            LOG_WARNING,
            "actor-attach-geo: '%s' has no bone or attachment '%s'",
            values[0].c_str(),
            values[2].c_str());
        return s7_f(sc);
    }
    if (!assets.hasGeo(values[3])) {
        TraceLog(LOG_WARNING, "actor-attach-geo: missing geo '%s'", values[3].c_str());
        return s7_f(sc);
    }
    const Model source = assets.getGeoModel(values[3]);
    Model model = cloneGeoModelInstance(source);
    if (model.meshCount <= 0) {
        return s7_f(sc);
    }

    destroyAttachmentTree(host.lookup(values[1].c_str()));

    LocalTransformation local{};
    local.scale = attachment.scale;
    flecs::entity child = g_thingWorld->entity().child_of(host);
    child.set_name(values[1].c_str());
    child.add<WorldSpace>()
        .add<MapOwned>()
        .set<LocalTransformation>(local)
        .set<GlobalTransformation>(GlobalTransformation{})
        .set<Model3D>({model, WHITE})
        .set<GeoSource>({values[3]})
        .set<BoneAttachment>(attachment);
    if (model.skeleton.boneCount > 0) {
        attachGeoAnimationPlayer(child, assets, values[3], {}, {}, true);
    }
    return s7_t(sc);
}

s7_pointer g_actor_detach(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-detach", 1, args, "id string");
    }
    const char* id = s7_string(s7_car(args));
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest) || !s7_is_string(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "actor-detach", 2, rest, "name string");
    }
    flecs::entity host = resolveActorPath(id);
    if (!host.is_valid()) {
        return s7_f(sc);
    }
    flecs::entity child = findAttachmentChild(host, s7_string(s7_car(rest)));
    if (!child.is_valid()) {
        return s7_f(sc);
    }
    destroyAttachmentTree(child);
    return s7_t(sc);
}

s7_pointer g_actor_anim_busy(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-anim-busy?", 1, args, "id string");
    }
    flecs::entity entity = resolveActorPath(s7_string(s7_car(args)));
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
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-play-anim", 1, args, "id string");
    }
    const char* id = s7_string(s7_car(args));
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest) || !s7_is_string(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "actor-play-anim", 2, rest, "clip string");
    }
    const char* clip = s7_string(s7_car(rest));
    rest = s7_cdr(rest);
    bool loop = true;
    if (s7_is_pair(rest)) {
        if (s7_is_boolean(s7_car(rest))) {
            loop = s7_boolean(sc, s7_car(rest));
        } else if (s7_is_integer(s7_car(rest))) {
            loop = s7_integer(s7_car(rest)) != 0;
        }
    }

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
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-tags", 1, args, "id string");
    }
    flecs::entity entity = lookupActor(s7_string(s7_car(args)));
    if (!entity.is_valid() || !entity.has<CollisionTags>()) {
        return s7_f(sc);
    }
    s7_pointer list = s7_nil(sc);
    const CollisionTags& tags = entity.get<CollisionTags>();
    for (auto it = tags.tags.rbegin(); it != tags.tags.rend(); ++it) {
        list = s7_cons(sc, s7_make_string(sc, it->c_str()), list);
    }
    return list;
}

s7_pointer g_actors_with_tag(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr || !s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actors-with-tag", 1, args, "tag string");
    }
    const std::string tag = s7_string(s7_car(args));
    s7_pointer list = s7_nil(sc);
    g_thingWorld->each([&](flecs::entity entity, Actor, const CollisionTags& tags) {
        if (!actorHasTag(tags, tag)) {
            return;
        }
        list = s7_cons(sc, s7_make_string(sc, entityIdString(entity).c_str()), list);
    });
    return list;
}

s7_pointer g_actors_in_radius(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    float x = 0, y = 0, z = 0, radius = 0;
    if (!readNumberArg(sc, args, x, "actors-in-radius", 1) ||
        !readNumberArg(sc, args, y, "actors-in-radius", 2) ||
        !readNumberArg(sc, args, z, "actors-in-radius", 3) ||
        !readNumberArg(sc, args, radius, "actors-in-radius", 4)) {
        return s7_wrong_type_arg_error(sc, "actors-in-radius", 1, args, "x y z r numbers");
    }
    std::string tagFilter;
    if (s7_is_pair(args) && s7_is_string(s7_car(args))) {
        tagFilter = s7_string(s7_car(args));
    }

    const float radiusSq = radius * radius;
    s7_pointer list = s7_nil(sc);
    g_thingWorld->each([&](flecs::entity entity, Actor, const LocalTransformation& local) {
        if (!tagFilter.empty()) {
            if (!entity.has<CollisionTags>() || !actorHasTag(entity.get<CollisionTags>(), tagFilter)) {
                return;
            }
        }
        const float dx = local.position.x - x;
        const float dy = local.position.y - y;
        const float dz = local.position.z - z;
        if (dx * dx + dy * dy + dz * dz > radiusSq) {
            return;
        }
        list = s7_cons(sc, s7_make_string(sc, entityIdString(entity).c_str()), list);
    });
    return list;
}

constexpr std::size_t kMaxDebugLines = 2048;

DebugLinePool& debugLinePoolMut() {
    if (!g_thingWorld->has<DebugLinePool>()) {
        g_thingWorld->set<DebugLinePool>({});
    }
    return g_thingWorld->get_mut<DebugLinePool>();
}

s7_pointer g_debug_line_add(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }

    float x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0;
    float r = 255.0f;
    float g = 255.0f;
    float b = 255.0f;
    float a = 255.0f;
    if (!readNumberArg(sc, args, x0, "debug-line-add!", 1) ||
        !readNumberArg(sc, args, y0, "debug-line-add!", 2) ||
        !readNumberArg(sc, args, z0, "debug-line-add!", 3) ||
        !readNumberArg(sc, args, x1, "debug-line-add!", 4) ||
        !readNumberArg(sc, args, y1, "debug-line-add!", 5) ||
        !readNumberArg(sc, args, z1, "debug-line-add!", 6) ||
        !readNumberArg(sc, args, r, "debug-line-add!", 7) ||
        !readNumberArg(sc, args, g, "debug-line-add!", 8) ||
        !readNumberArg(sc, args, b, "debug-line-add!", 9)) {
        return s7_wrong_type_arg_error(
            sc, "debug-line-add!", 1, args, "x0 y0 z0 x1 y1 z1 r g b [a] numbers");
    }
    if (s7_is_pair(args) && s7_is_number(s7_car(args))) {
        a = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
    }

    DebugLinePool& pool = debugLinePoolMut();
    if (pool.lines.size() >= kMaxDebugLines) {
        pool.lines.erase(pool.lines.begin());
    }
    pool.lines.push_back({
        .from = {x0, y0, z0},
        .to = {x1, y1, z1},
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
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (g_thingWorld->has<DebugLinePool>()) {
        g_thingWorld->get_mut<DebugLinePool>().lines.clear();
    }
    return s7_t(sc);
}

s7_pointer g_los(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    float x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0;
    if (!readNumberArg(sc, args, x0, "los?", 1) || !readNumberArg(sc, args, y0, "los?", 2) ||
        !readNumberArg(sc, args, z0, "los?", 3) || !readNumberArg(sc, args, x1, "los?", 4) ||
        !readNumberArg(sc, args, y1, "los?", 5) || !readNumberArg(sc, args, z1, "los?", 6)) {
        return s7_wrong_type_arg_error(sc, "los?", 1, args, "x0 y0 z0 x1 y1 z1 numbers");
    }

    PhysicsWorld* physics = physicsWorld();
    if (physics == nullptr) {
        return s7_f(sc);
    }

    const Vector3 origin = {x0, y0, z0};
    const Vector3 delta = {x1 - x0, y1 - y0, z1 - z0};
    const float distance = Vector3Length(delta);
    if (distance <= 1.0e-6f) {
        return s7_t(sc);
    }
    const Vector3 dir = Vector3Scale(delta, 1.0f / distance);
    const auto hit = physics->castRay(origin, dir, distance, BrushBlock::Los);
    return hit.has_value() ? s7_f(sc) : s7_t(sc);
}

namespace {

constexpr float kVolumeProbeDefaultDist = 64.0f;

float readOptionalMaxDist(s7_scheme* sc, s7_pointer args, float fallback) {
    if (!s7_is_pair(args) || !s7_is_number(s7_car(args))) {
        return fallback;
    }
    return static_cast<float>(s7_number_to_real(sc, s7_car(args)));
}

} // namespace

s7_pointer g_volume_floor_at(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    float x = 0, y = 0, z = 0;
    if (!readNumberArg(sc, args, x, "volume-floor-at", 1) ||
        !readNumberArg(sc, args, y, "volume-floor-at", 2) ||
        !readNumberArg(sc, args, z, "volume-floor-at", 3)) {
        return s7_wrong_type_arg_error(sc, "volume-floor-at", 1, args, "x y z [max-dist]");
    }
    const float maxDist = readOptionalMaxDist(sc, args, kVolumeProbeDefaultDist);
    PhysicsWorld* physics = physicsWorld();
    if (physics == nullptr) {
        return s7_f(sc);
    }
    const auto hit = physics->castRay({x, y, z}, {0.0f, -1.0f, 0.0f}, maxDist, BrushBlock::Actor);
    if (!hit.has_value()) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(hit->point.y));
}

s7_pointer g_volume_ceiling_at(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    float x = 0, y = 0, z = 0;
    if (!readNumberArg(sc, args, x, "volume-ceiling-at", 1) ||
        !readNumberArg(sc, args, y, "volume-ceiling-at", 2) ||
        !readNumberArg(sc, args, z, "volume-ceiling-at", 3)) {
        return s7_wrong_type_arg_error(sc, "volume-ceiling-at", 1, args, "x y z [max-dist]");
    }
    const float maxDist = readOptionalMaxDist(sc, args, kVolumeProbeDefaultDist);
    PhysicsWorld* physics = physicsWorld();
    if (physics == nullptr) {
        return s7_f(sc);
    }
    const auto hit = physics->castRay({x, y, z}, {0.0f, 1.0f, 0.0f}, maxDist, BrushBlock::Actor);
    if (!hit.has_value()) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(hit->point.y));
}

s7_pointer g_volume_clearance(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    float x = 0, y = 0, z = 0, halfHeight = 0;
    if (!readNumberArg(sc, args, x, "volume-clearance", 1) ||
        !readNumberArg(sc, args, y, "volume-clearance", 2) ||
        !readNumberArg(sc, args, z, "volume-clearance", 3) ||
        !readNumberArg(sc, args, halfHeight, "volume-clearance", 4)) {
        return s7_wrong_type_arg_error(
            sc, "volume-clearance", 1, args, "x y z half-height [max-dist]");
    }
    const float maxDist = readOptionalMaxDist(sc, args, kVolumeProbeDefaultDist);
    PhysicsWorld* physics = physicsWorld();
    if (physics == nullptr) {
        return s7_f(sc);
    }
    const auto floorHit =
        physics->castRay({x, y, z}, {0.0f, -1.0f, 0.0f}, maxDist, BrushBlock::Actor);
    if (!floorHit.has_value()) {
        return s7_f(sc);
    }
    const auto ceilingHit =
        physics->castRay({x, y, z}, {0.0f, 1.0f, 0.0f}, maxDist, BrushBlock::Actor);
    if (!ceilingHit.has_value()) {
        return s7_f(sc);
    }
    const float floorY = floorHit->point.y;
    const float ceilingY = ceilingHit->point.y;
    const float usable = ceilingY - floorY - 2.0f * halfHeight;
    return s7_list(
        sc,
        3,
        s7_make_real(sc, static_cast<double>(floorY)),
        s7_make_real(sc, static_cast<double>(ceilingY)),
        s7_make_real(sc, static_cast<double>(usable)));
}

s7_pointer g_linescan_world(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }

    float ox = 0, oy = 0, oz = 0, dx = 0, dy = 0, dz = 0, maxDistance = 0;
    if (!readNumberArg(sc, args, ox, "linescan-world", 1) ||
        !readNumberArg(sc, args, oy, "linescan-world", 2) ||
        !readNumberArg(sc, args, oz, "linescan-world", 3) ||
        !readNumberArg(sc, args, dx, "linescan-world", 4) ||
        !readNumberArg(sc, args, dy, "linescan-world", 5) ||
        !readNumberArg(sc, args, dz, "linescan-world", 6) ||
        !readNumberArg(sc, args, maxDistance, "linescan-world", 7)) {
        return s7_wrong_type_arg_error(
            sc, "linescan-world", 1, args, "ox oy oz dx dy dz max-distance numbers");
    }
    if (maxDistance <= 0.0f) {
        return s7_f(sc);
    }

    const Vector3 rawDir{dx, dy, dz};
    const float dirLen = Vector3Length(rawDir);
    if (dirLen <= 1.0e-6f) {
        return s7_f(sc);
    }
    const Vector3 dir = Vector3Scale(rawDir, 1.0f / dirLen);

    PhysicsWorld* physics = physicsWorld();
    if (physics == nullptr) {
        return s7_f(sc);
    }
    const auto hit = physics->castRay({ox, oy, oz}, dir, maxDistance, BrushBlock::Linescan);
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
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }

    float ox = 0, oy = 0, oz = 0, dx = 0, dy = 0, dz = 0, maxDistance = 0;
    if (!readNumberArg(sc, args, ox, "hitscan-actors", 1) ||
        !readNumberArg(sc, args, oy, "hitscan-actors", 2) ||
        !readNumberArg(sc, args, oz, "hitscan-actors", 3) ||
        !readNumberArg(sc, args, dx, "hitscan-actors", 4) ||
        !readNumberArg(sc, args, dy, "hitscan-actors", 5) ||
        !readNumberArg(sc, args, dz, "hitscan-actors", 6) ||
        !readNumberArg(sc, args, maxDistance, "hitscan-actors", 7)) {
        return s7_wrong_type_arg_error(
            sc, "hitscan-actors", 1, args, "ox oy oz dx dy dz max-distance numbers");
    }

    std::string tagFilter;
    if (s7_is_pair(args) && s7_is_string(s7_car(args))) {
        tagFilter = s7_string(s7_car(args));
        args = s7_cdr(args);
    }
    bool xzOnly = false;
    if (s7_is_pair(args) && s7_is_boolean(s7_car(args))) {
        xzOnly = s7_boolean(sc, s7_car(args));
    }

    if (maxDistance <= 0.0f) {
        return s7_f(sc);
    }

    const Vector3 origin{ox, oy, oz};
    const Vector3 rawDir{dx, dy, dz};
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
        if (!entity.has<WorldSpace>() || entity.has<ViewSprite>() || entity.has<ActorCorpse>()) {
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
        if (!entity.has<WorldSpace>() || entity.has<ActorCorpse>()) {
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
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-los?", 1, args, "from-id string");
    }
    const char* fromId = s7_string(s7_car(args));
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest) || !s7_is_string(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "actor-los?", 2, rest, "to-id string");
    }
    const char* toId = s7_string(s7_car(rest));

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

    s7_pointer losArgs = s7_list(
        sc,
        6,
        s7_make_real(sc, fromFeet.x),
        s7_make_real(sc, fromFeet.y + fromEye * 0.75f),
        s7_make_real(sc, fromFeet.z),
        s7_make_real(sc, toFeet.x),
        s7_make_real(sc, toFeet.y + toEye * 0.75f),
        s7_make_real(sc, toFeet.z));
    return g_los(sc, losArgs);
}

s7_pointer g_pvs_can_see(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    float x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0;
    if (!readNumberArg(sc, args, x0, "pvs-can-see", 1) ||
        !readNumberArg(sc, args, y0, "pvs-can-see", 2) ||
        !readNumberArg(sc, args, z0, "pvs-can-see", 3) ||
        !readNumberArg(sc, args, x1, "pvs-can-see", 4) ||
        !readNumberArg(sc, args, y1, "pvs-can-see", 5) ||
        !readNumberArg(sc, args, z1, "pvs-can-see", 6)) {
        return s7_wrong_type_arg_error(
            sc, "pvs-can-see", 1, args, "x0 y0 z0 x1 y1 z1 numbers");
    }
    if (g_thingWorld == nullptr || !g_thingWorld->has<MapPvs>() || !g_thingWorld->has<MapBsp>()) {
        return s7_t(sc);
    }
    const AreaGate gate = areaGateFromPoint(*g_thingWorld, {x0, y0, z0});
    return pvsVisiblePoints(
               g_thingWorld->get<MapBsp>().tree,
               g_thingWorld->get<MapPvs>().pvs,
               {x0, y0, z0},
               {x1, y1, z1},
               &gate)
        ? s7_t(sc)
        : s7_f(sc);
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
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr) {
        return s7_nil(sc);
    }
    float x = 0, y = 0, z = 0, loudness = 0;
    if (!readNumberArg(sc, args, x, "sound-emit!", 1) ||
        !readNumberArg(sc, args, y, "sound-emit!", 2) ||
        !readNumberArg(sc, args, z, "sound-emit!", 3) ||
        !readNumberArg(sc, args, loudness, "sound-emit!", 4)) {
        return s7_wrong_type_arg_error(
            sc, "sound-emit!", 1, args, "x y z loudness [falloff-per-unit]");
    }
    float falloffPerUnit = 1.0f;
    if (s7_is_pair(args) && s7_is_number(s7_car(args))) {
        falloffPerUnit = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
    }
    if (loudness <= 0.0f || falloffPerUnit < 0.0f) {
        return s7_nil(sc);
    }

    if (!g_thingWorld->has<MapNavigation>() || !g_thingWorld->has<MapBsp>()) {
        return s7_nil(sc);
    }
    const MapNavigation& nav = g_thingWorld->get<MapNavigation>();
    const BspTree& tree = g_thingWorld->get<MapBsp>().tree;
    const std::int32_t originLeaf = sampleNavLeaf(nav, tree, {x, y, z});
    if (originLeaf < 0) {
        return s7_nil(sc);
    }

    const std::vector<float> perceived =
        floodSound(nav, originLeaf, loudness, falloffPerUnit, isDoorPortalOpenForSound);

    s7_pointer list = s7_nil(sc);
    g_thingWorld->each([&](flecs::entity entity, Actor, const LocalTransformation& local) {
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

} // namespace

void queueThingDespawn(flecs::world& world, std::string_view id) {
    if (id.empty() || isProtectedThingId(id)) {
        return;
    }
    if (!world.has<ThingDespawnQueue>()) {
        world.set<ThingDespawnQueue>({});
    }
    world.get_mut<ThingDespawnQueue>().ids.emplace_back(std::string(id));
}

s7_pointer g_mover_open(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr || !s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "mover-open", 1, args, "id string");
    }
    flecs::entity entity = g_thingWorld->lookup(s7_string(s7_car(args)));
    return moverRequestOpen(entity) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_mover_close(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr || !s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "mover-close", 1, args, "id string");
    }
    flecs::entity entity = g_thingWorld->lookup(s7_string(s7_car(args)));
    return moverRequestClose(entity) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_mover_toggle(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr || !s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "mover-toggle", 1, args, "id string");
    }
    flecs::entity entity = g_thingWorld->lookup(s7_string(s7_car(args)));
    return moverRequestToggle(entity) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_mover_open_group(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr || !s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "mover-open-group", 1, args, "group string");
    }
    moverRequestOpenGroup(*g_thingWorld, s7_string(s7_car(args)));
    return s7_t(sc);
}

s7_pointer g_mover_close_group(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr || !s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "mover-close-group", 1, args, "group string");
    }
    moverRequestCloseGroup(*g_thingWorld, s7_string(s7_car(args)));
    return s7_t(sc);
}

s7_pointer g_mover_toggle_group(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr || !s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "mover-toggle-group", 1, args, "group string");
    }
    moverRequestToggleGroup(*g_thingWorld, s7_string(s7_car(args)));
    return s7_t(sc);
}

s7_pointer g_mover_set_locked(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr || !s7_is_pair(args) || !s7_is_string(s7_car(args)) ||
        !s7_is_pair(s7_cdr(args))) {
        return s7_wrong_type_arg_error(sc, "mover-set-locked", 1, args, "id bool");
    }
    flecs::entity entity = g_thingWorld->lookup(s7_string(s7_car(args)));
    if (!entity.is_valid() || !entity.has<RigidMover>()) {
        return s7_f(sc);
    }
    const bool locked = s7_boolean(sc, s7_cadr(args));
    entity.get_mut<RigidMover>().locked = locked;
    return s7_t(sc);
}

s7_pointer g_mover_locked_p(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr || !s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "mover-locked?", 1, args, "id string");
    }
    flecs::entity entity = g_thingWorld->lookup(s7_string(s7_car(args)));
    if (!entity.is_valid() || !entity.has<RigidMover>()) {
        return s7_f(sc);
    }
    return entity.get<RigidMover>().locked ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_mover_progress(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr || !s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "mover-progress", 1, args, "id string");
    }
    flecs::entity entity = g_thingWorld->lookup(s7_string(s7_car(args)));
    if (!entity.is_valid() || !entity.has<RigidMover>()) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(entity.get<RigidMover>().progress));
}

s7_pointer g_mover_state(s7_scheme* sc, s7_pointer args) {
    if (g_thingWorld == nullptr || !s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "mover-state", 1, args, "id string");
    }
    flecs::entity entity = g_thingWorld->lookup(s7_string(s7_car(args)));
    if (!entity.is_valid() || !entity.has<RigidMover>()) {
        return s7_f(sc);
    }
    const RigidMover& mover = entity.get<RigidMover>();
    const bool open = mover.target >= 0.5f || mover.progress >= 0.5f;
    s7_pointer openPair = s7_cons(sc, s7_make_symbol(sc, "open?"), open ? s7_t(sc) : s7_f(sc));
    s7_pointer progressPair =
        s7_cons(sc, s7_make_symbol(sc, "progress"), s7_make_real(sc, mover.progress));
    s7_pointer lockedPair =
        s7_cons(sc, s7_make_symbol(sc, "locked?"), mover.locked ? s7_t(sc) : s7_f(sc));
    return s7_list(sc, 3, openPair, progressPair, lockedPair);
}

s7_pointer g_mover_ids(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::ReadWorld) || g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    std::vector<std::string> ids;
    g_thingWorld->each([&](flecs::entity entity, const RigidMover&) {
        const char* name = entity.name();
        if (name != nullptr && name[0] != '\0') {
            ids.emplace_back(name);
        }
    });
    s7_pointer result = s7_nil(sc);
    for (auto it = ids.rbegin(); it != ids.rend(); ++it) {
        result = s7_cons(sc, s7_make_string(sc, it->c_str()), result);
    }
    return result;
}

s7_pointer g_mover_set_state(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr || !s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(
            sc, "mover-set-state", 1, args, "id open? progress [locked?]");
    }
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest) || !s7_is_pair(s7_cdr(rest))) {
        return s7_wrong_type_arg_error(
            sc, "mover-set-state", 2, args, "id open? progress [locked?]");
    }
    flecs::entity entity = g_thingWorld->lookup(s7_string(s7_car(args)));
    if (!entity.is_valid() || !entity.has<RigidMover>()) {
        return s7_f(sc);
    }
    const bool open = s7_boolean(sc, s7_car(rest));
    rest = s7_cdr(rest);
    if (!s7_is_number(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "mover-set-state", 3, args, "progress number");
    }
    const float progress = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
    rest = s7_cdr(rest);
    bool setLocked = false;
    bool locked = false;
    if (s7_is_pair(rest)) {
        setLocked = true;
        locked = s7_boolean(sc, s7_car(rest));
    }
    moverApplyState(entity, open, progress, setLocked, locked);
    return s7_t(sc);
}

s7_pointer stringListFromTags(s7_scheme* sc, const std::vector<std::string>& tags) {
    s7_pointer list = s7_nil(sc);
    for (auto it = tags.rbegin(); it != tags.rend(); ++it) {
        list = s7_cons(sc, s7_make_string(sc, it->c_str()), list);
    }
    return list;
}

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
    for (s7_pointer cursor = alist; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer entry = s7_car(cursor);
        if (!s7_is_pair(entry) || !s7_is_symbol(s7_car(entry))) {
            return false;
        }
        const char* key = s7_symbol_name(s7_car(entry));
        s7_pointer value = s7_cdr(entry);
        if (std::strcmp(key, "enabled") == 0) {
            if (s7_is_boolean(value)) {
                sight.enabled = s7_boolean(sc, value);
            } else if (s7_is_integer(value)) {
                sight.enabled = s7_integer(value) != 0;
            } else {
                return false;
            }
        } else if (std::strcmp(key, "range") == 0) {
            if (!s7_is_number(value)) {
                return false;
            }
            sight.range = static_cast<float>(s7_number_to_real(sc, value));
        } else if (std::strcmp(key, "fov") == 0) {
            if (!s7_is_number(value)) {
                return false;
            }
            sight.fovDegrees = static_cast<float>(s7_number_to_real(sc, value));
        } else if (std::strcmp(key, "eye-lift") == 0) {
            if (!s7_is_number(value)) {
                return false;
            }
            sight.eyeLift = static_cast<float>(s7_number_to_real(sc, value));
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
    }
    return true;
}

s7_pointer g_actor_sight_set(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr || !s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-sight-set!", 1, args, "id string");
    }
    const char* id = s7_string(s7_car(args));
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest)) {
        return s7_wrong_type_arg_error(sc, "actor-sight-set!", 2, rest, "alist");
    }
    flecs::entity entity = g_thingWorld->lookup(id);
    if (!entity.is_valid()) {
        return s7_f(sc);
    }
    ActorSight sight = entity.has<ActorSight>() ? entity.get<ActorSight>() : ActorSight{};
    if (!applySightAlist(sc, s7_car(rest), sight)) {
        return s7_wrong_type_arg_error(
            sc,
            "actor-sight-set!",
            2,
            s7_car(rest),
            "alist with enabled/range/fov/eye-lift/see-tags/ignore-tags/filter");
    }
    entity.set<ActorSight>(std::move(sight));
    return s7_t(sc);
}

s7_pointer g_actor_sight_get(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr || !s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-sight-get", 1, args, "id string");
    }
    flecs::entity entity = g_thingWorld->lookup(s7_string(s7_car(args)));
    if (!entity.is_valid() || !entity.has<ActorSight>()) {
        return s7_f(sc);
    }
    const ActorSight& sight = entity.get<ActorSight>();
    s7_pointer enabledPair =
        s7_cons(sc, s7_make_symbol(sc, "enabled"), sight.enabled ? s7_t(sc) : s7_f(sc));
    s7_pointer rangePair =
        s7_cons(sc, s7_make_symbol(sc, "range"), s7_make_real(sc, sight.range));
    s7_pointer fovPair =
        s7_cons(sc, s7_make_symbol(sc, "fov"), s7_make_real(sc, sight.fovDegrees));
    s7_pointer eyePair =
        s7_cons(sc, s7_make_symbol(sc, "eye-lift"), s7_make_real(sc, sight.eyeLift));
    s7_pointer seePair =
        s7_cons(sc, s7_make_symbol(sc, "see-tags"), stringListFromTags(sc, sight.seeTags));
    s7_pointer ignorePair =
        s7_cons(sc, s7_make_symbol(sc, "ignore-tags"), stringListFromTags(sc, sight.ignoreTags));
    s7_pointer filterPair = s7_cons(
        sc,
        s7_make_symbol(sc, "filter"),
        sight.filterProc.empty() ? s7_f(sc) : s7_make_string(sc, sight.filterProc.c_str()));
    return s7_list(
        sc, 7, enabledPair, rangePair, fovPair, eyePair, seePair, ignorePair, filterPair);
}

s7_pointer g_actor_can_see(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr || !s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "actor-can-see?", 1, args, "from-id string");
    }
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest) || !s7_is_string(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "actor-can-see?", 2, rest, "to-id string");
    }
    return actorCanSee(*g_thingWorld, s7_string(s7_car(args)), s7_string(s7_car(rest)))
        ? s7_t(sc)
        : s7_f(sc);
}

s7_pointer g_sight_budget(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr || !g_thingWorld->has<SightScanState>()) {
        return s7_f(sc);
    }
    return s7_make_integer(sc, g_thingWorld->get<SightScanState>().maxLosPerFrame);
}

s7_pointer g_sight_budget_set(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::WorldMutate)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_integer(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "sight-budget-set!", 1, args, "integer");
    }
    if (!g_thingWorld->has<SightScanState>()) {
        g_thingWorld->set<SightScanState>(SightScanState{});
    }
    SightScanState& state = g_thingWorld->get_mut<SightScanState>();
    state.maxLosPerFrame = static_cast<int>(s7_integer(s7_car(args)));
    if (state.maxLosPerFrame < 0) {
        state.maxLosPerFrame = 0;
    }
    return s7_t(sc);
}

s7_pointer g_thing_type(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (g_thingWorld == nullptr || !s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-type", 1, args, "id string");
    }
    flecs::entity entity = g_thingWorld->lookup(s7_string(s7_car(args)));
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
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-def-health", 1, args, "type string");
    }
    const ThingDef* def = thingDefRegistry().find(s7_string(s7_car(args)));
    if (def == nullptr || !def->health.has_value()) {
        return s7_f(sc);
    }
    return s7_make_integer(sc, *def->health);
}

s7_pointer g_thing_def_idle_anim(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-def-idle-anim", 1, args, "type string");
    }
    const ThingDef* def = thingDefRegistry().find(s7_string(s7_car(args)));
    if (def == nullptr || def->idleAnim.empty()) {
        return s7_f(sc);
    }
    return s7_make_string(sc, def->idleAnim.c_str());
}

s7_pointer g_thing_def_behavior(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-def-behavior", 1, args, "type string");
    }
    const ThingDef* def = thingDefRegistry().find(s7_string(s7_car(args)));
    if (def == nullptr || def->behavior.empty()) {
        return s7_f(sc);
    }
    return s7_make_string(sc, def->behavior.c_str());
}

namespace {

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

} // namespace

s7_pointer g_thing_def_move_mode(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-def-move-mode", 1, args, "type string");
    }
    const ThingDef* def = thingDefRegistry().find(s7_string(s7_car(args)));
    if (def == nullptr || !def->haveMotor) {
        return s7_f(sc);
    }
    return s7_make_string(sc, moveModeName(def->motorMoveMode));
}

s7_pointer g_thing_def_gravity(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-def-gravity", 1, args, "type string");
    }
    const ThingDef* def = thingDefRegistry().find(s7_string(s7_car(args)));
    if (def == nullptr || !def->haveMotor) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(def->motorGravity));
}

s7_pointer g_thing_def_vertical_speed(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-def-vertical-speed", 1, args, "type string");
    }
    const ThingDef* def = thingDefRegistry().find(s7_string(s7_car(args)));
    if (def == nullptr || !def->haveMotor) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(def->motorVerticalSpeed));
}

s7_pointer g_thing_def_hover_height(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-def-hover-height", 1, args, "type string");
    }
    const ThingDef* def = thingDefRegistry().find(s7_string(s7_car(args)));
    if (def == nullptr || !def->haveMotor) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(def->motorHoverHeight));
}

s7_pointer g_thing_def_radius(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-def-radius", 1, args, "type string");
    }
    const ThingDef* def = thingDefRegistry().find(s7_string(s7_car(args)));
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
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-def-behavior-names", 1, args, "type string");
    }
    const ThingDef* def = thingDefRegistry().find(s7_string(s7_car(args)));
    s7_pointer list = s7_nil(sc);
    if (def == nullptr) {
        return list;
    }
    for (auto it = def->behaviors.rbegin(); it != def->behaviors.rend(); ++it) {
        list = s7_cons(sc, s7_make_string(sc, it->name.c_str()), list);
    }
    return list;
}

s7_pointer g_thing_def_behavior_has(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-def-behavior-has?", 1, args, "type string");
    }
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest) || !s7_is_string(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "thing-def-behavior-has?", 2, rest, "name string");
    }
    const ThingDef* def = thingDefRegistry().find(s7_string(s7_car(args)));
    return findThingDefBehavior(def, s7_string(s7_car(rest))) != nullptr ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_thing_def_behavior_params(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-def-behavior-params", 1, args, "type string");
    }
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest) || !s7_is_string(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "thing-def-behavior-params", 2, rest, "name string");
    }
    const ThingDef* def = thingDefRegistry().find(s7_string(s7_car(args)));
    const ThingDefBehavior* behavior = findThingDefBehavior(def, s7_string(s7_car(rest)));
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
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-def-speed", 1, args, "type string");
    }
    const ThingDef* def = thingDefRegistry().find(s7_string(s7_car(args)));
    if (def == nullptr || !def->haveMotor) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(def->motorSpeed));
}

s7_pointer g_thing_def_pain_chance(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-def-pain-chance", 1, args, "type string");
    }
    const ThingDef* def = thingDefRegistry().find(s7_string(s7_car(args)));
    if (def == nullptr || !def->painChance.has_value()) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(*def->painChance));
}

s7_pointer g_thing_def_pain_threshold(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-def-pain-threshold", 1, args, "type string");
    }
    const ThingDef* def = thingDefRegistry().find(s7_string(s7_car(args)));
    if (def == nullptr || !def->painThreshold.has_value()) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(*def->painThreshold));
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

    s7_define_function(
        scheme,
        "thing-despawn",
        g_thing_despawn,
        1,
        0,
        false,
        "(thing-despawn id)");
    s7_define_function(scheme, "thing-type", g_thing_type, 1, 0, false, "(thing-type id)");
    s7_define_function(
        scheme, "thing-def-health", g_thing_def_health, 1, 0, false, "(thing-def-health type)");
    s7_define_function(
        scheme,
        "thing-def-idle-anim",
        g_thing_def_idle_anim,
        1,
        0,
        false,
        "(thing-def-idle-anim type)");
    s7_define_function(
        scheme,
        "thing-def-behavior",
        g_thing_def_behavior,
        1,
        0,
        false,
        "(thing-def-behavior type)");
    s7_define_function(
        scheme,
        "thing-def-move-mode",
        g_thing_def_move_mode,
        1,
        0,
        false,
        "(thing-def-move-mode type)");
    s7_define_function(
        scheme,
        "thing-def-gravity",
        g_thing_def_gravity,
        1,
        0,
        false,
        "(thing-def-gravity type)");
    s7_define_function(
        scheme,
        "thing-def-vertical-speed",
        g_thing_def_vertical_speed,
        1,
        0,
        false,
        "(thing-def-vertical-speed type)");
    s7_define_function(
        scheme,
        "thing-def-hover-height",
        g_thing_def_hover_height,
        1,
        0,
        false,
        "(thing-def-hover-height type)");
    s7_define_function(
        scheme, "thing-def-radius", g_thing_def_radius, 1, 0, false, "(thing-def-radius type)");
    s7_define_function(
        scheme,
        "thing-def-behavior-names",
        g_thing_def_behavior_names,
        1,
        0,
        false,
        "(thing-def-behavior-names type)");
    s7_define_function(
        scheme,
        "thing-def-behavior-has?",
        g_thing_def_behavior_has,
        2,
        0,
        false,
        "(thing-def-behavior-has? type name)");
    s7_define_function(
        scheme,
        "thing-def-behavior-params",
        g_thing_def_behavior_params,
        2,
        0,
        false,
        "(thing-def-behavior-params type name)");
    s7_define_function(
        scheme, "thing-def-speed", g_thing_def_speed, 1, 0, false, "(thing-def-speed type)");
    s7_define_function(
        scheme,
        "thing-def-pain-chance",
        g_thing_def_pain_chance,
        1,
        0,
        false,
        "(thing-def-pain-chance type)");
    s7_define_function(
        scheme,
        "thing-def-pain-threshold",
        g_thing_def_pain_threshold,
        1,
        0,
        false,
        "(thing-def-pain-threshold type)");
    s7_define_function(
        scheme,
        "motored-spawn",
        g_motored_spawn,
        9,
        5,
        false,
        "(motored-spawn id x y z vx vy vz kind path [radius gravity lifetime on-impact ignore])");
    s7_define_function(
        scheme,
        "sprite-spawn",
        g_sprite_spawn,
        5,
        2,
        false,
        "(sprite-spawn id x y z path [clip] [lifetime])");
    s7_define_function(
        scheme,
        "particle-spawn",
        g_particle_spawn,
        5,
        3,
        false,
        "(particle-spawn id x y z path [yaw | dx dy dz])");
    s7_define_function(
        scheme,
        "particle-spawn-fp",
        g_particle_spawn_fp,
        4,
        1,
        false,
        "(particle-spawn-fp id socket path attach [depth])");
    s7_define_function(
        scheme,
        "particle-spawn-attach",
        g_particle_spawn_attach,
        4,
        0,
        false,
        "(particle-spawn-attach id target path attach)");
    s7_define_function(
        scheme,
        "particle-spawn-follow",
        g_particle_spawn_follow,
        3,
        1,
        false,
        "(particle-spawn-follow id target path [back])");
    s7_define_function(
        scheme,
        "particle-spawn-bone",
        g_particle_spawn_bone,
        4,
        1,
        false,
        "(particle-spawn-bone id target bone path ['limb|'stump])");
    s7_define_function(
        scheme,
        "trail-spawn-fp",
        g_trail_spawn_fp,
        8,
        2,
        false,
        "(trail-spawn-fp id socket path attach ex ey ez lifetime [width] [depth])");
    s7_define_function(
        scheme,
        "trail-spawn",
        g_trail_spawn,
        9,
        1,
        false,
        "(trail-spawn id sx sy sz ex ey ez path lifetime [width])");
    s7_define_function(
        scheme,
        "tracer-spawn",
        g_tracer_spawn,
        6,
        1,
        false,
        "(tracer-spawn sx sy sz ex ey ez [style])");
    s7_define_function(
        scheme,
        "particle-play",
        g_particle_play,
        1,
        0,
        false,
        "(particle-play id)");
    s7_define_function(
        scheme,
        "particle-stop",
        g_particle_stop,
        1,
        0,
        false,
        "(particle-stop id)");
    s7_define_function(
        scheme,
        "particle-despawn",
        g_particle_despawn,
        1,
        0,
        false,
        "(particle-despawn id)");
    s7_define_function(
        scheme,
        "fx-light-spawn",
        g_fx_light_spawn,
        9,
        1,
        false,
        "(fx-light-spawn id x y z r g b intensity range [lifetime])");
    s7_define_function(
        scheme,
        "fx-light-attach",
        g_fx_light_attach,
        6,
        0,
        false,
        "(fx-light-attach id r g b intensity range)");
    s7_define_function(
        scheme,
        "dyn-light-spawn",
        g_dyn_light_spawn,
        9,
        1,
        false,
        "(dyn-light-spawn id x y z r g b intensity range [lifetime])");
    s7_define_function(
        scheme,
        "dyn-light-attach",
        g_dyn_light_attach,
        6,
        3,
        false,
        "(dyn-light-attach id r g b intensity range)");
    s7_define_function(
        scheme,
        "dyn-light-set-pos!",
        g_dyn_light_set_pos,
        4,
        0,
        false,
        "(dyn-light-set-pos! id x y z)");
    s7_define_function(
        scheme,
        "dyn-light-set-hsv!",
        g_dyn_light_set_hsv,
        4,
        0,
        false,
        "(dyn-light-set-hsv! id h s v)");
    s7_define_function(
        scheme,
        "actor-spawn",
        g_actor_spawn,
        7,
        5,
        false,
        "(actor-spawn id x y z yaw kind path [radius height speed gravity tags-list])");
    s7_define_function(scheme, "actor-pos", g_actor_pos, 1, 0, false, "(actor-pos id)");
    s7_define_function(scheme, "actor-yaw", g_actor_yaw, 1, 0, false, "(actor-yaw id)");
    s7_define_function(scheme, "actor-set-yaw!", g_actor_set_yaw, 2, 0, false, "(actor-set-yaw! id yaw)");
    s7_define_function(
        scheme,
        "actor-set-bone-scale!",
        g_actor_set_bone_scale,
        3,
        1,
        false,
        "(actor-set-bone-scale! id bone size [length])");
    s7_define_function(
        scheme, "actor-bone-scale", g_actor_bone_scale, 2, 0, false, "(actor-bone-scale id bone)");
    s7_define_function(
        scheme,
        "actor-sever-bone!",
        g_actor_sever_bone,
        2,
        6,
        false,
        "(actor-sever-bone! id bone [dx dy dz] [speed] [spin] [lifetime])");
    s7_define_function(
        scheme, "actor-hide-bone!", g_actor_hide_bone, 2, 0, false, "(actor-hide-bone! id bone)");
    s7_define_function(
        scheme, "actor-bone-severed?", g_actor_bone_severed, 2, 0, false, "(actor-bone-severed? id bone)");
    s7_define_function(
        scheme,
        "actor-ragdoll!",
        g_actor_ragdoll,
        1,
        6,
        false,
        "(actor-ragdoll! id [dx dy dz] [speed] [hit-bone] [lifetime])");
    s7_define_function(
        scheme,
        "physics-blast!",
        g_physics_blast,
        5,
        1,
        false,
        "(physics-blast! x y z radius speed [lift])");
    s7_define_function(scheme, "actor-bone-pos", g_actor_bone_pos, 2, 0, false, "(actor-bone-pos id bone)");
    s7_define_function(
        scheme,
        "actor-bone-springs!",
        g_actor_bone_springs,
        2,
        1,
        false,
        "(actor-bone-springs! id ((bone param stiffness damping gain limit) ...) [(step max-dt accel-smoothing)])");
    s7_define_function(
        scheme,
        "actor-bone-springs-active!",
        g_actor_bone_springs_active,
        2,
        0,
        false,
        "(actor-bone-springs-active! id active?)");
    s7_define_function(
        scheme, "actor-restore-bones!", g_actor_restore_bones, 1, 0, false, "(actor-restore-bones! id)");
    s7_define_function(
        scheme,
        "actor-clear-bone-scales!",
        g_actor_clear_bone_scales,
        1,
        0,
        false,
        "(actor-clear-bone-scales! id)");
    s7_define_function(
        scheme,
        "actor-set-shader-param!",
        g_actor_set_shader_param,
        2,
        4,
        false,
        "(actor-set-shader-param! id name [x y z w])");
    s7_define_function(
        scheme,
        "actor-clear-shader-params!",
        g_actor_clear_shader_params,
        1,
        0,
        false,
        "(actor-clear-shader-params! id)");
    s7_define_function(scheme, "thing-pos", g_thing_pos, 1, 0, false, "(thing-pos id)");
    s7_define_function(scheme, "thing-yaw", g_thing_yaw, 1, 0, false, "(thing-yaw id)");
    s7_define_function(
        scheme,
        "actor-set-wish",
        g_actor_set_wish,
        3,
        0,
        false,
        "(actor-set-wish id wx wz)");
    s7_define_function(
        scheme,
        "actor-set-wish-3d",
        g_actor_set_wish_3d,
        4,
        0,
        false,
        "(actor-set-wish-3d id wx wy wz)");
    s7_define_function(
        scheme,
        "actor-set-move-speed!",
        g_actor_set_move_speed,
        2,
        0,
        false,
        "(actor-set-move-speed! id speed)");
    s7_define_function(
        scheme, "actor-set-corpse!", g_actor_set_corpse, 1, 0, false, "(actor-set-corpse! id)");
    s7_define_function(
        scheme,
        "sprite-hide-part!",
        g_sprite_hide_part,
        2,
        0,
        false,
        "(sprite-hide-part! id part-name)");
    s7_define_function(
        scheme, "actor-grounded?", g_actor_grounded, 1, 0, false, "(actor-grounded? id)");
    s7_define_function(
        scheme, "actor-jump!", g_actor_jump, 2, 0, false, "(actor-jump! id speed)");
    s7_define_function(
        scheme, "actor-submerged?", g_actor_submerged, 1, 0, false, "(actor-submerged? id)");
    s7_define_function(
        scheme,
        "actor-near-water-exit?",
        g_actor_near_water_exit,
        1,
        0,
        false,
        "(actor-near-water-exit? id)");
    s7_define_function(
        scheme,
        "actor-water-exit-dir",
        g_actor_water_exit_dir,
        1,
        0,
        false,
        "(actor-water-exit-dir id)");
    s7_define_function(
        scheme,
        "actor-play-anim",
        g_actor_play_anim,
        2,
        1,
        false,
        "(actor-play-anim path clip [loop])");
    s7_define_function(
        scheme,
        "actor-anim-busy?",
        g_actor_anim_busy,
        1,
        0,
        false,
        "(actor-anim-busy? path)");
    s7_define_function(
        scheme,
        "actor-attach-geo",
        g_actor_attach_geo,
        4,
        7,
        false,
        "(actor-attach-geo path name point geo [x y z] [pitch yaw roll] [scale])");
    s7_define_function(
        scheme,
        "actor-detach",
        g_actor_detach,
        2,
        0,
        false,
        "(actor-detach path name)");
    s7_define_function(scheme, "actor-tags", g_actor_tags, 1, 0, false, "(actor-tags id)");
    s7_define_function(
        scheme, "actors-with-tag", g_actors_with_tag, 1, 0, false, "(actors-with-tag tag)");
    s7_define_function(
        scheme,
        "actors-in-radius",
        g_actors_in_radius,
        4,
        1,
        false,
        "(actors-in-radius x y z r [tag])");
    s7_define_function(scheme, "los?", g_los, 6, 0, false, "(los? x0 y0 z0 x1 y1 z1)");
    s7_define_function(
        scheme, "volume-floor-at", g_volume_floor_at, 3, 1, false, "(volume-floor-at x y z [max-dist])");
    s7_define_function(
        scheme,
        "volume-ceiling-at",
        g_volume_ceiling_at,
        3,
        1,
        false,
        "(volume-ceiling-at x y z [max-dist])");
    s7_define_function(
        scheme,
        "volume-clearance",
        g_volume_clearance,
        4,
        1,
        false,
        "(volume-clearance x y z half-height [max-dist])");
    s7_define_function(
        scheme,
        "debug-line-add!",
        g_debug_line_add,
        9,
        1,
        false,
        "(debug-line-add! x0 y0 z0 x1 y1 z1 r g b [a])");
    s7_define_function(
        scheme, "debug-lines-clear!", g_debug_lines_clear, 0, 0, false, "(debug-lines-clear!)");
    s7_define_function(
        scheme,
        "pvs-can-see",
        g_pvs_can_see,
        6,
        0,
        false,
        "(pvs-can-see x0 y0 z0 x1 y1 z1)");
    s7_define_function(
        scheme,
        "sound-emit!",
        g_sound_emit,
        4,
        1,
        false,
        "(sound-emit! x y z loudness [falloff-per-unit]) -> ((id loudness) ...)");
    s7_define_function(
        scheme, "actor-los?", g_actor_los, 2, 0, false, "(actor-los? from-id to-id)");
    s7_define_function(
        scheme,
        "actor-sight-set!",
        g_actor_sight_set,
        2,
        0,
        false,
        "(actor-sight-set! id alist)");
    s7_define_function(
        scheme, "actor-sight-get", g_actor_sight_get, 1, 0, false, "(actor-sight-get id)");
    s7_define_function(
        scheme, "actor-can-see?", g_actor_can_see, 2, 0, false, "(actor-can-see? from-id to-id)");
    s7_define_function(scheme, "sight-budget", g_sight_budget, 0, 0, false, "(sight-budget)");
    s7_define_function(
        scheme, "sight-budget-set!", g_sight_budget_set, 1, 0, false, "(sight-budget-set! n)");
    s7_define_function(
        scheme,
        "hitscan-actors",
        g_hitscan_actors,
        7,
        2,
        false,
        "(hitscan-actors ox oy oz dx dy dz max-distance [tag] [xz-only?]) -> "
        "(id part distance x y z)");
    s7_define_function(
        scheme,
        "linescan-world",
        g_linescan_world,
        7,
        0,
        false,
        "(linescan-world ox oy oz dx dy dz max-distance) -> (x y z nx ny nz distance) or #f");
    s7_define_function(scheme, "mover-open", g_mover_open, 1, 0, false, "(mover-open id)");
    s7_define_function(scheme, "mover-close", g_mover_close, 1, 0, false, "(mover-close id)");
    s7_define_function(scheme, "mover-toggle", g_mover_toggle, 1, 0, false, "(mover-toggle id)");
    s7_define_function(
        scheme, "mover-open-group", g_mover_open_group, 1, 0, false, "(mover-open-group group)");
    s7_define_function(
        scheme, "mover-close-group", g_mover_close_group, 1, 0, false, "(mover-close-group group)");
    s7_define_function(
        scheme,
        "mover-toggle-group",
        g_mover_toggle_group,
        1,
        0,
        false,
        "(mover-toggle-group group)");
    s7_define_function(
        scheme, "mover-set-locked", g_mover_set_locked, 2, 0, false, "(mover-set-locked id bool)");
    s7_define_function(scheme, "mover-locked?", g_mover_locked_p, 1, 0, false, "(mover-locked? id)");
    s7_define_function(scheme, "mover-progress", g_mover_progress, 1, 0, false, "(mover-progress id)");
    s7_define_function(scheme, "mover-state", g_mover_state, 1, 0, false, "(mover-state id)");
    s7_define_function(scheme, "mover-ids", g_mover_ids, 0, 0, false, "(mover-ids)");
    s7_define_function(
        scheme,
        "mover-set-state",
        g_mover_set_state,
        3,
        1,
        false,
        "(mover-set-state id open? progress [locked?])");
}

}
