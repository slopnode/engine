#include "particles/particle_module.hpp"

#include "particles/particle_sim.hpp"

#include "assets/asset_services.hpp"
#include "game/game_state.hpp"
#include "map/bsp.hpp"
#include "physics/physics_module.hpp"
#include "physics/physics_world.hpp"
#include "render/components.hpp"

#include <raymath.h>

namespace slopengine {

flecs::entity spawnParticleSystem(
    flecs::world& world,
    AssetStore& assets,
    const char* id,
    Vector3 position,
    float yaw,
    std::string_view path,
    bool playing,
    bool mapOwned) {
    ParticleSystemInstance instance{};
    if (!initParticleSystemInstance(instance, assets, path, playing)) {
        return {};
    }

    flecs::entity entity =
        id != nullptr && id[0] != '\0' ? world.entity(id) : world.entity();
    LocalTransformation local{};
    local.position = position;
    local.scale = {1.0f, 1.0f, 1.0f};
    local.rotation = QuaternionFromAxisAngle({0.0f, 1.0f, 0.0f}, yaw);
    Matrix s = MatrixScale(local.scale.x, local.scale.y, local.scale.z);
    Matrix r = QuaternionToMatrix(local.rotation);
    Matrix t = MatrixTranslate(local.position.x, local.position.y, local.position.z);
    GlobalTransformation global{};
    global.matrix = MatrixMultiply(t, MatrixMultiply(r, s));
    entity.add<WorldSpace>()
        .set<LocalTransformation>(local)
        .set<GlobalTransformation>(global)
        .set<ParticleSystemInstance>(std::move(instance));
    if (mapOwned) {
        entity.add<MapOwned>();
    }
    return entity;
}

void updateParticleSystems(
    flecs::world& world,
    AssetStore& assets,
    PhysicsWorld* physics,
    float dt) {
    if (dt <= 0.0f) {
        return;
    }

    ParticleRaycastFn raycast;
    if (physics != nullptr) {
        raycast = [physics](Vector3 origin, Vector3 direction, float distance)
            -> std::optional<ParticleRayHit> {
            const auto hit = physics->castRay(origin, direction, distance);
            if (!hit) {
                return std::nullopt;
            }
            return ParticleRayHit{hit->point, hit->normal};
        };
    }

    world.each([&](ParticleSystemInstance& instance, GlobalTransformation& global) {
        tickParticleSystemInstance(instance, assets, global.matrix, dt, raycast);
    });
}

void drawParticleSystems(
    flecs::world& world,
    AssetStore& assets,
    const Camera3D& camera) {
    std::vector<ParticleDrawItem> items;
    items.reserve(256);
    world.each([&](const ParticleSystemInstance& instance) {
        appendParticleDrawItems(instance, assets, camera, items);
    });
    drawParticleDrawItems(items, camera);
}

void registerParticleModule(flecs::world& world, AssetStore& assets) {
    world.component<ParticleSystemInstance>();

    world.system("ParticleSystemUpdate")
        .kind(flecs::OnUpdate)
        .run([&assets](flecs::iter& it) {
            flecs::world world = it.world();
            if (!isPlaying(world)) {
                return;
            }
            PhysicsWorld* physics = nullptr;
            if (world.has<PhysicsContext>()) {
                physics = world.get_mut<PhysicsContext>().world;
            }
            updateParticleSystems(world, assets, physics, GetFrameTime());
        });
}

}
