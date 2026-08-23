#include "physics/rigid_mover.hpp"

#include "audio/audio_module.hpp"
#include "game/game_state.hpp"
#include "map/bsp.hpp"
#include "physics/components.hpp"
#include "physics/physics_module.hpp"
#include "physics/physics_world.hpp"
#include "render/components.hpp"
#include "render/fx_local_light.hpp"
#include "script/scheme_call.hpp"
#include "script/script_context.hpp"

#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace slopengine {

namespace {

constexpr float kCrushPen = 0.08f;
constexpr float kShoveSkin = 0.02f;

std::string entityIdString(flecs::entity entity) {
    const char* name = entity.name();
    if (name != nullptr && name[0] != '\0') {
        return name;
    }
    return std::to_string(static_cast<std::uint64_t>(entity.id()));
}

Vector3 localAxis(MoverRotAxis axis) {
    switch (axis) {
    case MoverRotAxis::Pitch:
        return {1.0f, 0.0f, 0.0f};
    case MoverRotAxis::Roll:
        return {0.0f, 0.0f, 1.0f};
    case MoverRotAxis::Yaw:
    default:
        return {0.0f, 1.0f, 0.0f};
    }
}

Vector3 rotateVec(Quaternion q, Vector3 v) {
    return Vector3RotateByQuaternion(v, q);
}

struct Aabb {
    Vector3 min{};
    Vector3 max{};
};

Aabb capsuleAabb(const Vector3& feet, const CharacterMotor& motor) {
    const float totalHeight = motor.height + 2.0f * motor.radius;
    return {
        .min = {feet.x - motor.radius, feet.y, feet.z - motor.radius},
        .max = {feet.x + motor.radius, feet.y + totalHeight, feet.z + motor.radius},
    };
}

Aabb obbWorldAabb(Vector3 center, Quaternion rot, Vector3 halfExtents) {
    const Vector3 axes[3] = {
        rotateVec(rot, {halfExtents.x, 0.0f, 0.0f}),
        rotateVec(rot, {0.0f, halfExtents.y, 0.0f}),
        rotateVec(rot, {0.0f, 0.0f, halfExtents.z}),
    };
    Vector3 ext = {
        std::fabs(axes[0].x) + std::fabs(axes[1].x) + std::fabs(axes[2].x),
        std::fabs(axes[0].y) + std::fabs(axes[1].y) + std::fabs(axes[2].y),
        std::fabs(axes[0].z) + std::fabs(axes[1].z) + std::fabs(axes[2].z),
    };
    return {
        .min = {center.x - ext.x, center.y - ext.y, center.z - ext.z},
        .max = {center.x + ext.x, center.y + ext.y, center.z + ext.z},
    };
}

bool aabbOverlap(const Aabb& a, const Aabb& b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y &&
        a.min.z <= b.max.z && a.max.z >= b.min.z;
}

float aabbPenetration(const Aabb& a, const Aabb& b, Vector3& outNormal) {
    const float overlapX = std::min(a.max.x, b.max.x) - std::max(a.min.x, b.min.x);
    const float overlapY = std::min(a.max.y, b.max.y) - std::max(a.min.y, b.min.y);
    const float overlapZ = std::min(a.max.z, b.max.z) - std::max(a.min.z, b.min.z);
    if (overlapX <= 0.0f || overlapY <= 0.0f || overlapZ <= 0.0f) {
        outNormal = {0.0f, 1.0f, 0.0f};
        return 0.0f;
    }

    const float aCx = 0.5f * (a.min.x + a.max.x);
    const float aCy = 0.5f * (a.min.y + a.max.y);
    const float aCz = 0.5f * (a.min.z + a.max.z);
    const float bCx = 0.5f * (b.min.x + b.max.x);
    const float bCy = 0.5f * (b.min.y + b.max.y);
    const float bCz = 0.5f * (b.min.z + b.max.z);

    if (overlapX <= overlapY && overlapX <= overlapZ) {
        outNormal = {aCx >= bCx ? 1.0f : -1.0f, 0.0f, 0.0f};
        return overlapX;
    }
    if (overlapZ <= overlapY) {
        outNormal = {0.0f, 0.0f, aCz >= bCz ? 1.0f : -1.0f};
        return overlapZ;
    }
    outNormal = {0.0f, aCy >= bCy ? 1.0f : -1.0f, 0.0f};
    return overlapY;
}

void ensureKinematic(PhysicsWorld* physics, flecs::entity entity, RigidMover& mover) {
    if (physics == nullptr) {
        return;
    }
    const std::uint64_t id = static_cast<std::uint64_t>(entity.id());
    Vector3 pos{};
    Quaternion rot{};
    computeMoverPose(mover, mover.progress, pos, rot);
    const Vector3 center = moverCollideWorldCenter(pos, rot, mover);
    if (!physics->hasKinematic(id)) {
        physics->createKinematicBox(id, center, mover.collideHalfExtents, rot, mover.slide);
        mover.kinematicReady = true;
    }
}

void syncKinematic(PhysicsWorld* physics, flecs::entity entity, RigidMover& mover, float dt) {
    if (physics == nullptr) {
        return;
    }
    ensureKinematic(physics, entity, mover);
    Vector3 pos{};
    Quaternion rot{};
    computeMoverPose(mover, mover.progress, pos, rot);
    if (entity.has<LocalTransformation>()) {
        LocalTransformation& local = entity.get_mut<LocalTransformation>();
        local.position = pos;
        local.rotation = rot;
    }
    const std::uint64_t id = static_cast<std::uint64_t>(entity.id());
    physics->setKinematicSlide(id, mover.slide);
    physics->setKinematicPose(
        id,
        moverCollideWorldCenter(pos, rot, mover),
        rot,
        dt);
}

void playMoverSound(flecs::world& world, const Vector3& pos, const std::string& sound, float volume) {
    if (sound.empty() || !world.has<AudioContext>()) {
        return;
    }
    AudioContext& ctx = world.get_mut<AudioContext>();
    if (ctx.world == nullptr || ctx.assets == nullptr || !ctx.world->ready()) {
        return;
    }
    ctx.world->playSound3d(*ctx.assets, sound, pos.x, pos.y, pos.z, volume);
}

bool requestTarget(flecs::entity entity, float target) {
    if (!entity.is_valid() || !entity.has<RigidMover>()) {
        return false;
    }
    RigidMover& mover = entity.get_mut<RigidMover>();
    if (mover.locked) {
        return false;
    }
    mover.target = target < 0.5f ? 0.0f : 1.0f;
    return true;
}

} // namespace

void computeMoverPose(
    const RigidMover& mover,
    float t,
    Vector3& outPos,
    Quaternion& outRot) {
    t = std::clamp(t, 0.0f, 1.0f);
    const Vector3 pivotWorld =
        Vector3Add(mover.closedPos, rotateVec(mover.closedRot, mover.pivotLocal));
    const float angle = t * mover.openAngleRadians;
    Quaternion delta = QuaternionIdentity();
    if (std::fabs(angle) > 1.0e-8f) {
        const Vector3 axisLocal = localAxis(mover.rotAxis);
        const Vector3 axisWorld = Vector3Normalize(rotateVec(mover.closedRot, axisLocal));
        delta = QuaternionFromAxisAngle(axisWorld, angle);
    }
    outRot = QuaternionNormalize(QuaternionMultiply(delta, mover.closedRot));
    const Vector3 slide = rotateVec(mover.closedRot, Vector3Scale(mover.openPosOffset, t));
    outPos = Vector3Add(
        Vector3Add(pivotWorld, rotateVec(outRot, Vector3Negate(mover.pivotLocal))),
        slide);
}

Matrix moverClosedMatrix(const RigidMover& mover, Vector3 scale) {
    Matrix matrix = MatrixIdentity();
    matrix = MatrixMultiply(matrix, MatrixScale(scale.x, scale.y, scale.z));
    matrix = MatrixMultiply(matrix, QuaternionToMatrix(mover.closedRot));
    matrix = MatrixMultiply(
        matrix,
        MatrixTranslate(mover.closedPos.x, mover.closedPos.y, mover.closedPos.z));
    return matrix;
}

Vector3 moverCollideWorldCenter(const Vector3& pos, const Quaternion& rot, const RigidMover& mover) {
    return Vector3Add(pos, rotateVec(rot, mover.collideCenterLocal));
}

bool moverRequestOpen(flecs::entity entity) {
    return requestTarget(entity, 1.0f);
}

bool moverRequestClose(flecs::entity entity) {
    return requestTarget(entity, 0.0f);
}

bool moverRequestToggle(flecs::entity entity) {
    if (!entity.is_valid() || !entity.has<RigidMover>()) {
        return false;
    }
    const RigidMover& mover = entity.get<RigidMover>();
    const float next = mover.target >= 0.5f ? 0.0f : 1.0f;
    return requestTarget(entity, next);
}

void moverRequestOpenGroup(flecs::world& world, std::string_view groupId) {
    if (groupId.empty()) {
        return;
    }
    world.each([&](flecs::entity entity, RigidMover& mover) {
        if (mover.groupId == groupId) {
            moverRequestOpen(entity);
        }
    });
}

void moverRequestCloseGroup(flecs::world& world, std::string_view groupId) {
    if (groupId.empty()) {
        return;
    }
    world.each([&](flecs::entity entity, RigidMover& mover) {
        if (mover.groupId == groupId) {
            moverRequestClose(entity);
        }
    });
}

void moverRequestToggleGroup(flecs::world& world, std::string_view groupId) {
    if (groupId.empty()) {
        return;
    }
    bool anyOpenTarget = false;
    world.each([&](flecs::entity, RigidMover& mover) {
        if (mover.groupId == groupId && mover.target >= 0.5f) {
            anyOpenTarget = true;
        }
    });
    if (anyOpenTarget) {
        moverRequestCloseGroup(world, groupId);
    } else {
        moverRequestOpenGroup(world, groupId);
    }
}

void moverApplyState(flecs::entity entity, bool open, float progress, bool setLocked, bool locked) {
    if (!entity.is_valid() || !entity.has<RigidMover>()) {
        return;
    }
    RigidMover& mover = entity.get_mut<RigidMover>();
    mover.progress = std::clamp(progress, 0.0f, 1.0f);
    mover.target = open ? 1.0f : 0.0f;
    mover.lastTarget = mover.target;
    if (open && mover.progress < 1.0f && std::fabs(mover.progress - 1.0f) < 1.0e-4f) {
        mover.progress = 1.0f;
    }
    if (!open && std::fabs(progress) < 1.0e-4f) {
        mover.progress = 0.0f;
    }
    if (setLocked) {
        mover.locked = locked;
    }
    flecs::world world = entity.world();
    if (world.has<PhysicsContext>()) {
        syncKinematic(world.get_mut<PhysicsContext>().world, entity, mover, 1.0f / 60.0f);
    }
}

void registerRigidMoverSystem(flecs::world& world) {
    world.component<RigidMover>();

    world.observer<RigidMover>("RigidMoverOnRemove")
        .event(flecs::OnRemove)
        .each([](flecs::entity entity, RigidMover&) {
            flecs::world world = entity.world();
            if (!world.has<PhysicsContext>() || world.get<PhysicsContext>().world == nullptr) {
                return;
            }
            world.get_mut<PhysicsContext>().world->destroyKinematic(
                static_cast<std::uint64_t>(entity.id()));
        });

    world.system<RigidMover, LocalTransformation>("RigidMoverAdvance")
        .kind(flecs::PreUpdate)
        .each([](flecs::entity entity, RigidMover& mover, LocalTransformation& local) {
            flecs::world world = entity.world();
            if (isSimulationPaused(world)) {
                return;
            }
            PhysicsWorld* physics = nullptr;
            if (world.has<PhysicsContext>()) {
                physics = world.get_mut<PhysicsContext>().world;
            }

            const float dt = GetFrameTime();
            const float prevTarget = mover.lastTarget;
            tickRigidMover(mover, dt);

            Vector3 pos{};
            Quaternion rot{};
            computeMoverPose(mover, mover.progress, pos, rot);
            local.position = pos;
            local.rotation = rot;

            if (mover.target != prevTarget) {
                mover.lastTarget = mover.target;
                playMoverSound(
                    world,
                    pos,
                    mover.target >= 0.5f ? mover.openSound : mover.closeSound,
                    mover.soundVolume);
            }

            const float physDt = (dt > 1.0e-4f && dt <= 0.25f) ? dt : (1.0f / 60.0f);
            syncKinematic(physics, entity, mover, physDt);
        });

    world.system("RigidMoverCharacterResolve")
        .kind(flecs::OnUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            if (isSimulationPaused(world)) {
                return;
            }
            if (!world.has<PhysicsContext>() || world.get<PhysicsContext>().world == nullptr) {
                return;
            }
            PhysicsWorld* physics = world.get_mut<PhysicsContext>().world;
            s7_scheme* scheme = world.has<ScriptContext>() ? world.get<ScriptContext>().scheme
                                                           : nullptr;

            struct Victim {
                flecs::entity entity{};
                std::uint64_t id = 0;
                CharacterMotor* motor = nullptr;
                Vector3 feet{};
            };

            std::vector<Victim> victims;
            world.each([&](flecs::entity entity, CharacterMotor& motor) {
                const std::uint64_t id = static_cast<std::uint64_t>(entity.id());
                if (!physics->hasCharacter(id)) {
                    return;
                }
                const JPH::RVec3 feet = physics->characterPosition(id);
                Victim v{};
                v.entity = entity;
                v.id = id;
                v.motor = &motor;
                v.feet = {
                    static_cast<float>(feet.GetX()),
                    static_cast<float>(feet.GetY()),
                    static_cast<float>(feet.GetZ()),
                };
                victims.push_back(v);
            });

            world.each([&](flecs::entity moverEntity, RigidMover& mover) {
                Vector3 pos{};
                Quaternion rot{};
                computeMoverPose(mover, mover.progress, pos, rot);
                const Vector3 center = moverCollideWorldCenter(pos, rot, mover);
                const Aabb moverBounds = obbWorldAabb(center, rot, mover.collideHalfExtents);

                std::unordered_set<std::uint64_t> stillCrushing;
                for (Victim& victim : victims) {
                    if (victim.motor == nullptr) {
                        continue;
                    }
                    Aabb charBounds = capsuleAabb(victim.feet, *victim.motor);
                    if (!aabbOverlap(moverBounds, charBounds)) {
                        continue;
                    }

                    Vector3 normal{};
                    float pen = aabbPenetration(charBounds, moverBounds, normal);
                    if (pen <= 0.0f) {
                        continue;
                    }

                    Vector3 shoveDelta{};
                    if (moverComputeShove(mover.pushMode, normal, pen, shoveDelta)) {
                        victim.feet.x += shoveDelta.x;
                        victim.feet.y += shoveDelta.y;
                        victim.feet.z += shoveDelta.z;
                        physics->setCharacterPosition(
                            victim.id, victim.feet.x, victim.feet.y, victim.feet.z);
                        charBounds = capsuleAabb(victim.feet, *victim.motor);
                        pen = aabbPenetration(charBounds, moverBounds, normal);
                    }

                    if (mover.blockMode == MoverBlockMode::Crush && pen >= kCrushPen) {
                        stillCrushing.insert(victim.id);
                        if (mover.crushing.find(victim.id) == mover.crushing.end() &&
                            !mover.onCrush.empty() && scheme != nullptr) {
                            tryCallSchemeProc2String(
                                scheme,
                                mover.onCrush,
                                entityIdString(moverEntity),
                                entityIdString(victim.entity),
                                ScriptScope::World);
                        }
                    }
                }
                mover.crushing = std::move(stillCrushing);
            });
        });

    world.system<RigidMover, Model3D, GlobalTransformation>("RigidMoverRadTint")
        .without<BakedLightmapModel>()
        .kind(flecs::PreUpdate)
        .each([](flecs::entity entity, RigidMover& mover, Model3D& model, const GlobalTransformation& global) {
            flecs::world world = entity.world();
            Vector3 scale{1.0f, 1.0f, 1.0f};
            if (entity.has<LocalTransformation>()) {
                scale = entity.get<LocalTransformation>().scale;
            }
            const Matrix closedMatrix = moverClosedMatrix(mover, scale);
            model.color = sampleBakeTintColorForModel(
                world, model.model, global.matrix, false, &closedMatrix);
        });

    world.system<Model3D, GlobalTransformation>("WorldModelLightTint")
        .with<WorldSpace>()
        .without<RigidMover>()
        .without<MapLightmapState>()
        .without<ViewSpace>()
        .kind(flecs::PreUpdate)
        .each([](flecs::entity entity, Model3D& model, const GlobalTransformation& global) {
            flecs::world world = entity.world();
            model.color =
                sampleBakeTintColorForModel(world, model.model, global.matrix, false);
        });
}

}
