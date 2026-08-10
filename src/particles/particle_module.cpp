#include "particles/particle_module.hpp"

#include "particles/particle_sim.hpp"

#include "assets/asset_services.hpp"
#include "game/game_state.hpp"
#include "map/bsp.hpp"
#include "physics/physics_module.hpp"
#include "physics/physics_world.hpp"
#include "render/components.hpp"
#include "render/fx_local_light.hpp"
#include "render/view_sprite_attachment.hpp"

#include <algorithm>
#include <cmath>
#include <raymath.h>

namespace slopengine {

namespace {

Color multiplyParticleTint(Color particle, Color scene) {
    return {
        static_cast<unsigned char>(
            std::clamp(static_cast<int>(particle.r) * static_cast<int>(scene.r) / 255, 0, 255)),
        static_cast<unsigned char>(
            std::clamp(static_cast<int>(particle.g) * static_cast<int>(scene.g) / 255, 0, 255)),
        static_cast<unsigned char>(
            std::clamp(static_cast<int>(particle.b) * static_cast<int>(scene.b) / 255, 0, 255)),
        particle.a,
    };
}

void writeWorldPose(
    LocalTransformation& local,
    GlobalTransformation& global,
    Vector3 position,
    float yaw) {
    local.position = position;
    local.scale = {1.0f, 1.0f, 1.0f};
    local.rotation = QuaternionFromAxisAngle({0.0f, 1.0f, 0.0f}, yaw);
    const Matrix s = MatrixScale(local.scale.x, local.scale.y, local.scale.z);
    const Matrix r = QuaternionToMatrix(local.rotation);
    const Matrix t = MatrixTranslate(local.position.x, local.position.y, local.position.z);
    global.matrix = MatrixMultiply(t, MatrixMultiply(r, s));
}

void writeWorldPoseAimed(
    LocalTransformation& local,
    GlobalTransformation& global,
    Vector3 position,
    Vector3 direction) {
    local.position = position;
    local.scale = {1.0f, 1.0f, 1.0f};
    const float len = Vector3Length(direction);
    if (len <= 1.0e-6f) {
        local.rotation = QuaternionIdentity();
    } else {
        const Vector3 aim = Vector3Scale(direction, 1.0f / len);
        local.rotation = QuaternionFromVector3ToVector3({0.0f, 1.0f, 0.0f}, aim);
    }
    const Matrix s = MatrixScale(local.scale.x, local.scale.y, local.scale.z);
    const Matrix r = QuaternionToMatrix(local.rotation);
    const Matrix t = MatrixTranslate(local.position.x, local.position.y, local.position.z);
    global.matrix = MatrixMultiply(t, MatrixMultiply(r, s));
}

void applyWorldPose(flecs::entity entity, Vector3 position, float yaw) {
    LocalTransformation local{};
    GlobalTransformation global{};
    writeWorldPose(local, global, position, yaw);
    entity.set<LocalTransformation>(local);
    entity.set<GlobalTransformation>(global);
}

void applyWorldPoseAimed(flecs::entity entity, Vector3 position, Vector3 direction) {
    LocalTransformation local{};
    GlobalTransformation global{};
    writeWorldPoseAimed(local, global, position, direction);
    entity.set<LocalTransformation>(local);
    entity.set<GlobalTransformation>(global);
}

void syncFollowAttachPoints(flecs::world& world) {
    world.each([&](flecs::entity entity,
                   ParticleFollowAttachPoint& follow,
                   ParticleSystemInstance& instance,
                   LocalTransformation& local,
                   GlobalTransformation& global) {
        if (!instance.playing) {
            entity.destruct();
            return;
        }
        flecs::entity host = world.entity(static_cast<flecs::entity_t>(follow.host));
        if (!host.is_valid() || !host.has<ViewSprite>()) {
            entity.destruct();
            return;
        }
        const auto tip = resolveViewSpriteAttachmentWorld(world, host, follow.name, follow.depth);
        if (!tip) {
            return;
        }
        writeWorldPose(local, global, *tip, 0.0f);
    });
}

} // namespace

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
    applyWorldPose(entity, position, yaw);
    entity.add<WorldSpace>().set<ParticleSystemInstance>(std::move(instance));
    if (mapOwned) {
        entity.add<MapOwned>();
    }
    return entity;
}

flecs::entity spawnParticleSystemAimed(
    flecs::world& world,
    AssetStore& assets,
    const char* id,
    Vector3 position,
    Vector3 direction,
    std::string_view path,
    bool playing,
    bool mapOwned) {
    ParticleSystemInstance instance{};
    if (!initParticleSystemInstance(instance, assets, path, playing)) {
        return {};
    }

    flecs::entity entity =
        id != nullptr && id[0] != '\0' ? world.entity(id) : world.entity();
    applyWorldPoseAimed(entity, position, direction);
    entity.add<WorldSpace>().set<ParticleSystemInstance>(std::move(instance));
    if (mapOwned) {
        entity.add<MapOwned>();
    }
    return entity;
}

flecs::entity spawnParticleSystemFp(
    flecs::world& world,
    AssetStore& assets,
    const char* id,
    flecs::entity hostViewSprite,
    const std::string& attachName,
    std::string_view path,
    float depth,
    bool mapOwned) {
    if (!hostViewSprite.is_valid()) {
        return {};
    }
    const auto tip = resolveViewSpriteAttachmentWorld(world, hostViewSprite, attachName, depth);
    if (!tip) {
        TraceLog(
            LOG_WARNING,
            "spawnParticleSystemFp: host missing attach point '%s' or view pose",
            attachName.c_str());
        return {};
    }

    flecs::entity entity =
        spawnParticleSystem(world, assets, id, *tip, 0.0f, path, true, mapOwned);
    if (!entity.is_valid()) {
        return {};
    }
    entity.set<ParticleFollowAttachPoint>({
        .host = static_cast<std::uint64_t>(hostViewSprite.id()),
        .name = attachName,
        .depth = depth > 0.0f ? depth : 0.35f,
    });
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

    syncFollowAttachPoints(world);

    ParticleRaycastFn raycast;
    if (physics != nullptr) {
        raycast = [physics](Vector3 origin, Vector3 direction, float distance)
            -> std::optional<ParticleRayHit> {
            const auto hit = physics->castRay(origin, direction, distance, BrushBlock::Projectile);
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
    const Camera3D& camera,
    bool unlit) {
    std::vector<ParticleDrawItem> items;
    items.reserve(256);

    world.each([&](flecs::entity entity, const ParticleSystemInstance& instance) {
        if (entity.has<ParticleFollowAttachPoint>()) {
            return;
        }
        appendParticleDrawItems(instance, assets, camera, items);
    });
    if (!unlit) {
        for (ParticleDrawItem& item : items) {
            if (item.unlit) {
                continue;
            }
            const Color scene = sampleReceiverTintColor(world, item.position, false, 64.0f);
            item.color = multiplyParticleTint(item.color, scene);
        }
    }
    std::vector<ParticleDrawItem> depthItems;
    std::vector<ParticleDrawItem> overlayItems;
    depthItems.reserve(items.size());
    overlayItems.reserve(items.size());
    for (ParticleDrawItem& item : items) {
        if (item.depthTest) {
            depthItems.push_back(std::move(item));
        } else {
            overlayItems.push_back(std::move(item));
        }
    }
    drawParticleDrawItems(overlayItems, camera, false);
}

void drawMuzzleParticleSystems(
    flecs::world& world,
    AssetStore& assets,
    const Camera3D& camera,
    bool unlit) {
    std::vector<ParticleDrawItem> items;
    items.reserve(64);

    world.each([&](flecs::entity entity, const ParticleSystemInstance& instance) {
        if (!entity.has<ParticleFollowAttachPoint>()) {
            return;
        }
        appendParticleDrawItems(instance, assets, camera, items);
    });
    if (!unlit) {
        for (ParticleDrawItem& item : items) {
            if (item.unlit) {
                continue;
            }
            const Color scene = sampleReceiverTintColor(world, item.position, false, 64.0f);
            item.color = multiplyParticleTint(item.color, scene);
        }
    }
    drawParticleDrawItems(items, camera, false);
}

void registerParticleModule(flecs::world& world, AssetStore& assets) {
    world.component<ParticleSystemInstance>();
    world.component<ParticleFollowAttachPoint>();

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
