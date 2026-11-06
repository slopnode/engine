#include "particles/particle_module.hpp"

#include "particles/particle_sim.hpp"

#include "assets/asset_services.hpp"
#include "game/game_state.hpp"
#include "map/bsp.hpp"
#include "physics/physics_module.hpp"
#include "physics/physics_world.hpp"
#include "render/components.hpp"
#include "render/bone_attachment.hpp"
#include "render/fx_local_light.hpp"
#include "render/model_hitboxes.hpp"
#include "render/view_sprite_attachment.hpp"
#include "render/world_sprite_attachment.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <raymath.h>

namespace slopengine {

namespace {

constexpr std::uint32_t kParticleRelightInterval = 4;

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
    global.matrix = MatrixMultiply(s, MatrixMultiply(r, t));
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
    global.matrix = MatrixMultiply(s, MatrixMultiply(r, t));
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

void syncFollowWorldAttachPoints(flecs::world& world) {
    world.each([&](flecs::entity entity,
                   ParticleFollowWorldAttachPoint& follow,
                   ParticleSystemInstance& instance,
                   LocalTransformation& local,
                   GlobalTransformation& global) {
        if (!instance.playing) {
            entity.destruct();
            return;
        }
        flecs::entity host = world.entity(static_cast<flecs::entity_t>(follow.host));
        if (!host.is_valid()) {
            entity.destruct();
            return;
        }
        const auto tip = resolveWorldSpriteAttachmentWorld(world, host, follow.name);
        if (!tip) {
            return;
        }
        writeWorldPose(local, global, *tip, 0.0f);
    });
}

Matrix bonePoseMatrix(const Transform& pose) {
    return MatrixMultiply(
        MatrixMultiply(MatrixScale(pose.scale.x, pose.scale.y, pose.scale.z), QuaternionToMatrix(pose.rotation)),
        MatrixTranslate(pose.translation.x, pose.translation.y, pose.translation.z));
}

bool boneEmitterPose(
    flecs::entity host,
    const ParticleFollowBone& follow,
    Vector3& position,
    Vector3& direction) {
    if (!host.is_valid() || !host.has<Model3D>() || !host.has<GlobalTransformation>()) {
        return false;
    }
    const Model& model = host.get<Model3D>().model;
    if (follow.bone < 0 || follow.bone >= model.skeleton.boneCount || model.currentPose == nullptr ||
        model.boneMatrices == nullptr) {
        return false;
    }
    const Matrix& world = host.get<GlobalTransformation>().matrix;
    const Matrix joint = MatrixMultiply(bonePoseMatrix(model.currentPose[follow.bone]), world);
    position = {joint.m12, joint.m13, joint.m14};
    direction = {0.0f, 1.0f, 0.0f};
    if (follow.reference >= 0 && follow.reference < model.skeleton.boneCount) {
        const Matrix skin = MatrixMultiply(model.boneMatrices[follow.reference], world);
        const Vector3 center = Vector3Transform(follow.referenceCenter, skin);
        const Vector3 out = Vector3Subtract(position, center);
        if (Vector3LengthSqr(out) > 1e-8f) {
            direction = Vector3Normalize(out);
        }
    }
    return true;
}

void syncFollowBones(flecs::world& world) {
    world.each([&](flecs::entity entity,
                   ParticleFollowBone& follow,
                   ParticleSystemInstance& instance,
                   LocalTransformation& local,
                   GlobalTransformation& global) {
        if (!instance.playing) {
            entity.destruct();
            return;
        }
        Vector3 position{};
        Vector3 direction{};
        if (!boneEmitterPose(world.entity(static_cast<flecs::entity_t>(follow.host)), follow, position, direction)) {
            instance.playing = false;
            return;
        }
        writeWorldPoseAimed(local, global, position, direction);
    });
}

bool followEntityPose(flecs::entity host, float back, Vector3& position, Vector3& direction) {
    if (!host.is_valid() || !host.has<GlobalTransformation>()) {
        return false;
    }
    const Matrix& world = host.get<GlobalTransformation>().matrix;
    Vector3 forward{world.m8, world.m9, world.m10};
    if (Vector3LengthSqr(forward) <= 1.0e-12f) {
        forward = {0.0f, 0.0f, 1.0f};
    }
    forward = Vector3Normalize(forward);
    position = Vector3Subtract(Vector3{world.m12, world.m13, world.m14}, Vector3Scale(forward, back));
    direction = Vector3Negate(forward);
    return true;
}

void syncFollowEntities(flecs::world& world) {
    world.each([&](flecs::entity entity,
                   ParticleFollowEntity& follow,
                   ParticleSystemInstance& instance,
                   LocalTransformation& local,
                   GlobalTransformation& global) {
        const flecs::entity host = world.entity(static_cast<flecs::entity_t>(follow.host));
        Vector3 position{};
        Vector3 direction{};
        if (!follow.placed) {
            if (!host.is_alive()) {
                entity.destruct();
                return;
            }
            if (!followEntityPose(host, follow.back, position, direction)) {
                return;
            }
            writeWorldPoseAimed(local, global, position, direction);
            follow.placed = true;
            instance.playing = true;
            return;
        }
        if (!instance.playing) {
            entity.destruct();
            return;
        }
        if (!instance.emitting) {
            return;
        }
        if (!host.is_alive() || !followEntityPose(host, follow.back, position, direction)) {
            instance.emitting = false;
            return;
        }
        writeWorldPoseAimed(local, global, position, direction);
    });
}

} // namespace

flecs::entity spawnParticleSystemFollow(
    flecs::world& world,
    AssetStore& assets,
    const char* id,
    flecs::entity host,
    float back,
    std::string_view path,
    bool mapOwned) {
    Vector3 position{};
    Vector3 direction{0.0f, 1.0f, 0.0f};
    const bool placed = followEntityPose(host, back, position, direction);
    flecs::entity entity =
        spawnParticleSystemAimed(world, assets, id, position, direction, path, placed, mapOwned);
    if (!entity.is_valid()) {
        return {};
    }
    entity.set<ParticleFollowEntity>({
        .host = static_cast<std::uint64_t>(host.id()),
        .back = back,
        .placed = placed,
    });
    return entity;
}

flecs::entity spawnParticleSystemBone(
    flecs::world& world,
    AssetStore& assets,
    const char* id,
    flecs::entity host,
    std::string_view bone,
    bool fromParent,
    std::string_view path,
    bool mapOwned) {
    if (!host.is_valid() || !host.has<Model3D>()) {
        return {};
    }
    const Model& model = host.get<Model3D>().model;
    ParticleFollowBone follow{};
    follow.host = static_cast<std::uint64_t>(host.id());
    follow.bone = findModelBoneIndex(model, bone);
    if (follow.bone < 0) {
        return {};
    }
    std::vector<BoneHitbox> scratch;
    const std::vector<BoneHitbox>* boxes = nullptr;
    if (host.has<ModelHitboxes>()) {
        boxes = &ensureModelHitboxes(model, host.get_mut<ModelHitboxes>());
    } else {
        scratch = buildBoneHitboxes(model);
        boxes = &scratch;
    }
    int reference = fromParent ? model.skeleton.bones[follow.bone].parent : follow.bone;
    while (reference >= 0 &&
           (static_cast<std::size_t>(reference) >= boxes->size() || !(*boxes)[static_cast<std::size_t>(reference)].valid)) {
        reference = model.skeleton.bones[reference].parent;
    }
    if (reference >= 0) {
        const BoneHitbox& box = (*boxes)[static_cast<std::size_t>(reference)];
        follow.reference = reference;
        follow.referenceCenter = Vector3Scale(Vector3Add(box.min, box.max), 0.5f);
    }
    Vector3 position{};
    Vector3 direction{};
    if (!boneEmitterPose(host, follow, position, direction)) {
        return {};
    }
    flecs::entity entity =
        spawnParticleSystemAimed(world, assets, id, position, direction, path, true, mapOwned);
    if (!entity.is_valid()) {
        return {};
    }
    entity.set<ParticleFollowBone>(follow);
    return entity;
}

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

flecs::entity spawnParticleSystemWorldAttach(
    flecs::world& world,
    AssetStore& assets,
    const char* id,
    flecs::entity hostSprite,
    const std::string& attachName,
    std::string_view path,
    bool mapOwned) {
    if (!hostSprite.is_valid()) {
        return {};
    }
    const auto tip = resolveWorldSpriteAttachmentWorld(world, hostSprite, attachName);
    if (!tip) {
        TraceLog(
            LOG_WARNING,
            "spawnParticleSystemWorldAttach: host missing attach point '%s' or sprite pose",
            attachName.c_str());
        return {};
    }

    flecs::entity entity =
        spawnParticleSystem(world, assets, id, *tip, 0.0f, path, true, mapOwned);
    if (!entity.is_valid()) {
        return {};
    }
    entity.set<ParticleFollowWorldAttachPoint>({
        .host = static_cast<std::uint64_t>(hostSprite.id()),
        .name = attachName,
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
    syncFollowWorldAttachPoints(world);
    syncFollowBones(world);
    syncFollowEntities(world);

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

void lightParticleSystems(flecs::world& world, const Frustum& frustum, bool unlit) {
    if (unlit) {
        return;
    }
    static std::uint32_t frame = 0;
    ++frame;
    world.each([&](ParticleSystemInstance& instance) {
        for (ParticleEmitterRuntime& emitter : instance.emitters) {
            if (emitter.aliveCount <= 0 || emitter.def.unlit) {
                continue;
            }
            for (std::size_t index = 0; index < emitter.particles.size(); ++index) {
                Particle& p = emitter.particles[index];
                if (!p.alive) {
                    continue;
                }
                if (p.lit && (frame + static_cast<std::uint32_t>(index)) % kParticleRelightInterval != 0) {
                    continue;
                }
                if (!sphereInFrustum(frustum, p.position, p.size0)) {
                    continue;
                }
                if (!p.bakeCached) {
                    p.bakeTint = sampleBakeTintAtOrigin(world, p.position, false, 64.0f);
                    p.bakeCached = p.stuck;
                }
                p.tint = composeBakeTintWithOverlay(
                    p.bakeTint, sampleOverlayLightsAtOrigin(world, p.position, true));
                p.lit = true;
            }
        }
    });
}

void drawParticleSystems(
    flecs::world& world,
    AssetStore& assets,
    const Camera3D& camera,
    const Frustum* frustum,
    bool unlit) {
    std::vector<ParticleDrawItem> items;
    items.reserve(256);

    world.each([&](flecs::entity entity, const ParticleSystemInstance& instance) {
        if (entity.has<ParticleFollowAttachPoint>()) {
            return;
        }
        appendParticleDrawItems(instance, assets, camera, items, frustum, unlit);
    });
    items.erase(
        std::remove_if(
            items.begin(), items.end(), [](const ParticleDrawItem& item) { return item.depthTest; }),
        items.end());
    drawParticleDrawItems(items, camera, false);
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
        appendParticleDrawItems(instance, assets, camera, items, nullptr, unlit);
    });
    drawParticleDrawItems(items, camera, false);
}

void registerParticleModule(flecs::world& world, AssetStore& assets) {
    world.component<ParticleSystemInstance>();
    world.component<ParticleFollowAttachPoint>();
    world.component<ParticleFollowWorldAttachPoint>();
    world.component<ParticleFollowBone>();
    world.component<ParticleFollowEntity>();

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
