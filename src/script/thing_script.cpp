#include "script/thing_script.hpp"
#include "script/script_scope.hpp"

#include "assets/asset_services.hpp"
#include "assets/asset_store.hpp"
#include "assets/skeleton_loader.hpp"
#include "assets/sprite_anim_loader.hpp"
#include "game/game_state.hpp"
#include "map/bsp.hpp"
#include "map/pvs.hpp"
#include "map/thing.hpp"
#include "map/thing_def_registry.hpp"
#include "navigation/nav_components.hpp"
#include "physics/components.hpp"
#include "physics/motored_body.hpp"
#include "physics/physics_module.hpp"
#include "physics/rigid_mover.hpp"
#include "physics/sight_components.hpp"
#include "physics/sight_module.hpp"
#include "physics/trigger_components.hpp"
#include "render/components.hpp"
#include "render/debug_line_pool.hpp"
#include "render/dynamic_light.hpp"
#include "render/fx_local_light.hpp"
#include "render/sprite_animator.hpp"
#include "render/sprite_billboard.hpp"
#include "particles/components.hpp"
#include "particles/particle_module.hpp"
#include "particles/particle_sim.hpp"
#include "render/transform.hpp"
#include "script/first_person_script.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <string>
#include <string_view>
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
    local.rotation = QuaternionIdentity();

    const float horiz = std::sqrt(vx * vx + vz * vz);
    const float facingYaw = horiz > 1.0e-4f ? std::atan2(vx, vz) : 0.0f;

    entity.add<WorldSpace>().add<MapOwned>().set<LocalTransformation>(local);

    if (isSprite) {
        entity.set<SpriteInstance>({
            .sprite = path,
            .frame = "A",
            .facingYaw = facingYaw,
        });
    } else {
        const Model source = assets.getGeoModel(path);
        Model model = cloneGeoModelInstance(source);
        if (model.meshCount <= 0) {
            entity.destruct();
            return s7_f(sc);
        }
        entity.set<Model3D>({model, WHITE});
    }

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

    flecs::entity entity = g_thingWorld->lookup(id.c_str());
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
        path,
        depth,
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

    if (PhysicsWorld* physics = physicsWorld()) {
        const std::uint64_t eid = static_cast<std::uint64_t>(entity.id());
        if (physics->hasCharacter(eid)) {
            physics->destroyCharacter(eid);
        }
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

    flecs::entity entity = lookupActor(id);
    if (!entity.is_valid() || !entity.has<SpriteInstance>()) {
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
        const auto hit = raycastSpriteBillboard(ray, *billboard, bestDistance);
        if (!hit) {
            return;
        }
        bestDistance = hit->distance;
        bestHit = *hit;
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
    return pvsVisiblePoints(
               g_thingWorld->get<MapBsp>().tree,
               g_thingWorld->get<MapPvs>().pvs,
               {x0, y0, z0},
               {x1, y1, z1})
        ? s7_t(sc)
        : s7_f(sc);
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

s7_pointer g_thing_def_melee_damage(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-def-melee-damage", 1, args, "type string");
    }
    const ThingDef* def = thingDefRegistry().find(s7_string(s7_car(args)));
    if (def == nullptr || !def->haveMelee) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(def->meleeDamage));
}

s7_pointer g_thing_def_melee_range(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-def-melee-range", 1, args, "type string");
    }
    const ThingDef* def = thingDefRegistry().find(s7_string(s7_car(args)));
    if (def == nullptr || !def->haveMelee) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(def->meleeRange));
}

s7_pointer g_thing_def_melee_cooldown(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-def-melee-cooldown", 1, args, "type string");
    }
    const ThingDef* def = thingDefRegistry().find(s7_string(s7_car(args)));
    if (def == nullptr || !def->haveMelee) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(def->meleeCooldown));
}

s7_pointer g_thing_def_melee_anim(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-def-melee-anim", 1, args, "type string");
    }
    const ThingDef* def = thingDefRegistry().find(s7_string(s7_car(args)));
    if (def == nullptr || !def->haveMelee || def->meleeAnim.empty()) {
        return s7_f(sc);
    }
    return s7_make_string(sc, def->meleeAnim.c_str());
}

s7_pointer g_thing_def_ranged_range(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-def-ranged-range", 1, args, "type string");
    }
    const ThingDef* def = thingDefRegistry().find(s7_string(s7_car(args)));
    if (def == nullptr || !def->haveRanged) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(def->rangedRange));
}

s7_pointer g_thing_def_ranged_min_range(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-def-ranged-min-range", 1, args, "type string");
    }
    const ThingDef* def = thingDefRegistry().find(s7_string(s7_car(args)));
    if (def == nullptr || !def->haveRanged) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(def->rangedMinRange));
}

s7_pointer g_thing_def_ranged_cooldown(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-def-ranged-cooldown", 1, args, "type string");
    }
    const ThingDef* def = thingDefRegistry().find(s7_string(s7_car(args)));
    if (def == nullptr || !def->haveRanged) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(def->rangedCooldown));
}

s7_pointer g_thing_def_ranged_anim(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "thing-def-ranged-anim", 1, args, "type string");
    }
    const ThingDef* def = thingDefRegistry().find(s7_string(s7_car(args)));
    if (def == nullptr || !def->haveRanged || def->rangedAnim.empty()) {
        return s7_f(sc);
    }
    return s7_make_string(sc, def->rangedAnim.c_str());
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
        "thing-def-melee-damage",
        g_thing_def_melee_damage,
        1,
        0,
        false,
        "(thing-def-melee-damage type)");
    s7_define_function(
        scheme,
        "thing-def-melee-range",
        g_thing_def_melee_range,
        1,
        0,
        false,
        "(thing-def-melee-range type)");
    s7_define_function(
        scheme,
        "thing-def-melee-cooldown",
        g_thing_def_melee_cooldown,
        1,
        0,
        false,
        "(thing-def-melee-cooldown type)");
    s7_define_function(
        scheme,
        "thing-def-melee-anim",
        g_thing_def_melee_anim,
        1,
        0,
        false,
        "(thing-def-melee-anim type)");
    s7_define_function(
        scheme,
        "thing-def-ranged-range",
        g_thing_def_ranged_range,
        1,
        0,
        false,
        "(thing-def-ranged-range type)");
    s7_define_function(
        scheme,
        "thing-def-ranged-min-range",
        g_thing_def_ranged_min_range,
        1,
        0,
        false,
        "(thing-def-ranged-min-range type)");
    s7_define_function(
        scheme,
        "thing-def-ranged-cooldown",
        g_thing_def_ranged_cooldown,
        1,
        0,
        false,
        "(thing-def-ranged-cooldown type)");
    s7_define_function(
        scheme,
        "thing-def-ranged-anim",
        g_thing_def_ranged_anim,
        1,
        0,
        false,
        "(thing-def-ranged-anim type)");
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
        3,
        1,
        false,
        "(particle-spawn-fp id socket path [depth])");
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
        "actor-spawn",
        g_actor_spawn,
        7,
        5,
        false,
        "(actor-spawn id x y z yaw kind path [radius height speed gravity tags-list])");
    s7_define_function(scheme, "actor-pos", g_actor_pos, 1, 0, false, "(actor-pos id)");
    s7_define_function(scheme, "actor-yaw", g_actor_yaw, 1, 0, false, "(actor-yaw id)");
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
        scheme, "actor-set-corpse!", g_actor_set_corpse, 1, 0, false, "(actor-set-corpse! id)");
    s7_define_function(
        scheme, "actor-grounded?", g_actor_grounded, 1, 0, false, "(actor-grounded? id)");
    s7_define_function(
        scheme,
        "actor-play-anim",
        g_actor_play_anim,
        2,
        1,
        false,
        "(actor-play-anim id clip [loop])");
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
        1,
        false,
        "(hitscan-actors ox oy oz dx dy dz max-distance [tag]) -> (id part distance x y z)");
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
