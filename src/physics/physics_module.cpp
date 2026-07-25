#include "physics/physics_module.hpp"

#include "camera/components.hpp"
#include "core/frame_perf.hpp"
#include "input/actions.hpp"
#include "input/input_context.hpp"
#include "input/input_state.hpp"
#include "interact/components.hpp"
#include "physics/components.hpp"
#include "physics/motored_body.hpp"
#include "physics/rigid_mover.hpp"
#include "physics/trigger_components.hpp"
#include "render/components.hpp"
#include "script/scheme_call.hpp"
#include "script/script_context.hpp"
#include "script/script_scope.hpp"
#include "ui/ui_state.hpp"

#include <raylib.h>
#include <raymath.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace slopengine {

namespace {

Vector3 forwardFromYawPitch(float yaw, float pitch) {
    const float cosPitch = std::cos(pitch);
    return Vector3Normalize({
        std::sin(yaw) * cosPitch,
        std::sin(pitch),
        std::cos(yaw) * cosPitch,
    });
}

struct Aabb {
    Vector3 min{};
    Vector3 max{};
};

struct TriggerCandidate {
    flecs::entity entity{};
    Aabb bounds{};
    std::vector<std::string> tags;
};

bool aabbOverlap(const Aabb& a, const Aabb& b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y &&
        a.min.z <= b.max.z && a.max.z >= b.min.z;
}

bool tagsIntersect(const std::vector<std::string>& filter, const std::vector<std::string>& tags) {
    const std::vector<std::string> effectiveFilter =
        filter.empty() ? std::vector<std::string>{"player"} : filter;
    for (const std::string& required : effectiveFilter) {
        for (const std::string& tag : tags) {
            if (tag == required) {
                return true;
            }
        }
    }
    return false;
}

Aabb triggerAabb(const Vector3& center, const Vector3& size) {
    const Vector3 half = {size.x * 0.5f, size.y * 0.5f, size.z * 0.5f};
    return {
        .min = {center.x - half.x, center.y - half.y, center.z - half.z},
        .max = {center.x + half.x, center.y + half.y, center.z + half.z},
    };
}

Aabb capsuleAabb(const Vector3& feet, const CharacterMotor& motor) {
    const float totalHeight = motor.height + 2.0f * motor.radius;
    return {
        .min = {feet.x - motor.radius, feet.y, feet.z - motor.radius},
        .max = {feet.x + motor.radius, feet.y + totalHeight, feet.z + motor.radius},
    };
}

Vector3 candidateFeet(flecs::entity entity, const CharacterMotor& motor) {
    if (entity.has<Lens>()) {
        const Vector3 eye = entity.get<Lens>().camera.position;
        return {eye.x, eye.y - motor.eyeHeight, eye.z};
    }
    if (entity.has<LocalTransformation>()) {
        return entity.get<LocalTransformation>().position;
    }
    return {};
}

std::string entityIdString(flecs::entity entity) {
    const char* name = entity.name();
    if (name != nullptr && name[0] != '\0') {
        return name;
    }
    return std::to_string(static_cast<std::uint64_t>(entity.id()));
}

bool pointInFacePolygon(const Vector3& point, const FaceUseSurface& surface) {
    if (surface.vertices.size() < 3) {
        return false;
    }
    const Vector3& v0 = surface.vertices[0];
    for (std::size_t i = 1; i + 1 < surface.vertices.size(); ++i) {
        const Vector3& v1 = surface.vertices[i];
        const Vector3& v2 = surface.vertices[i + 1];
        const Vector3 n = Vector3CrossProduct(Vector3Subtract(v1, v0), Vector3Subtract(v2, v0));
        const Vector3 c0 = Vector3CrossProduct(Vector3Subtract(v1, v0), Vector3Subtract(point, v0));
        const Vector3 c1 = Vector3CrossProduct(Vector3Subtract(v2, v1), Vector3Subtract(point, v1));
        const Vector3 c2 = Vector3CrossProduct(Vector3Subtract(v0, v2), Vector3Subtract(point, v2));
        if (Vector3DotProduct(n, c0) >= -1e-5f && Vector3DotProduct(n, c1) >= -1e-5f &&
            Vector3DotProduct(n, c2) >= -1e-5f) {
            return true;
        }
        if (Vector3DotProduct(n, c0) <= 1e-5f && Vector3DotProduct(n, c1) <= 1e-5f &&
            Vector3DotProduct(n, c2) <= 1e-5f) {
            return true;
        }
    }
    return false;
}

bool capsuleTouchesFace(
    const Vector3& feet,
    const CharacterMotor& motor,
    const FaceUseSurface& surface,
    float depth) {
    if (surface.vertices.size() < 3 || Vector3Length(surface.normal) < 1e-6f) {
        return false;
    }
    const float totalHeight = motor.height + 2.0f * motor.radius;
    const Vector3 center = {
        feet.x,
        feet.y + totalHeight * 0.5f,
        feet.z,
    };
    const float planeDist = Vector3DotProduct(Vector3Subtract(center, surface.vertices[0]), surface.normal);
    if (std::fabs(planeDist) > depth + motor.radius) {
        return false;
    }
    const Vector3 onPlane = Vector3Subtract(center, Vector3Scale(surface.normal, planeDist));
    return pointInFacePolygon(onPlane, surface);
}

} // namespace

void registerPhysicsModule(flecs::world& world, PhysicsWorld* physics) {
    world.component<CharacterMotor>();
    world.component<Actor>();
    world.component<CollisionTags>();
    world.component<TriggerVolume>();
    world.set<PhysicsContext>(PhysicsContext{physics});
    registerRigidMoverSystem(world);

    world.system("CharacterMotorInput")
        .kind(flecs::PreUpdate)
        .run([](flecs::iter& it) {
            if (!it.world().has<PhysicsContext>() || !it.world().has<InputState>()) {
                return;
            }

            PhysicsContext& physics = it.world().get_mut<PhysicsContext>();
            if (physics.world == nullptr || !physics.world->hasPlayer()) {
                return;
            }

            InputContextStack& contexts = it.world().get_mut<InputContextStack>();
            if (!contexts.allowsGameplay()) {
                return;
            }

            const flecs::entity camera = it.world().lookup("Player");
            if (!camera.is_valid() || !camera.has<CharacterMotor>() || !camera.has<FirstPersonController>()) {
                return;
            }

            CharacterMotor& motor = camera.get_mut<CharacterMotor>();
            FirstPersonController& controller = camera.get_mut<FirstPersonController>();
            InputState& input = it.world().get_mut<InputState>();

            const Vector3 forwardFlat =
                Vector3Normalize({std::sin(controller.yaw), 0.0f, std::cos(controller.yaw)});
            const Vector3 right = Vector3CrossProduct(forwardFlat, {0.0f, 1.0f, 0.0f});
            Vector3 wish{};

            if (input.down(Action::MoveForward)) {
                wish = Vector3Add(wish, forwardFlat);
            }
            if (input.down(Action::MoveBackward)) {
                wish = Vector3Subtract(wish, forwardFlat);
            }
            if (input.down(Action::MoveLeft)) {
                wish = Vector3Subtract(wish, right);
            }
            if (input.down(Action::MoveRight)) {
                wish = Vector3Add(wish, right);
            }

            motor.wishX = wish.x;
            motor.wishZ = wish.z;
        });

    world.system("PhysicsStep")
        .kind(flecs::OnUpdate)
        .run([](flecs::iter& it) {
            FramePerfStats* perf =
                it.world().has<FramePerfStats>() ? &it.world().get_mut<FramePerfStats>() : nullptr;
            if (perf != nullptr) {
                perf->physicsMs = 0.0f;
            }

            if (!it.world().has<PhysicsContext>()) {
                return;
            }

            PhysicsContext& physics = it.world().get_mut<PhysicsContext>();
            if (physics.world == nullptr) {
                return;
            }

            const bool playerNoclip =
                it.world().has<DebugUiState>() && it.world().get<DebugUiState>().noclip;

            std::vector<CharacterStep> steps;
            it.world().each([&](flecs::entity entity, CharacterMotor& motor) {
                const std::uint64_t id = static_cast<std::uint64_t>(entity.id());
                if (!physics.world->hasCharacter(id)) {
                    return;
                }
                CharacterStep step{};
                step.id = id;
                step.motor = &motor;
                step.noclip = entity.has<PlayerCamera>() && playerNoclip;
                steps.push_back(step);
            });

            if (steps.empty()) {
                return;
            }

            const double physicsStart = perfNow();
            physics.world->update(GetFrameTime(), steps);
            if (perf != nullptr) {
                perf->physicsMs = perfElapsedMs(physicsStart);
            }

            const flecs::entity camera = it.world().lookup("Player");
            if (camera.is_valid() && camera.has<CharacterMotor>() && camera.has<Lens>() &&
                camera.has<FirstPersonController>() && physics.world->hasPlayer()) {
                CharacterMotor& motor = camera.get_mut<CharacterMotor>();
                FirstPersonController& controller = camera.get_mut<FirstPersonController>();
                Lens& lens = camera.get_mut<Lens>();

                const JPH::RVec3 feet = physics.world->playerPosition();
                lens.camera.position = {
                    static_cast<float>(feet.GetX()),
                    static_cast<float>(feet.GetY()) + motor.eyeHeight,
                    static_cast<float>(feet.GetZ()),
                };

                const Vector3 forward = forwardFromYawPitch(controller.yaw, controller.pitch);
                lens.camera.target = Vector3Add(lens.camera.position, forward);
                lens.camera.up = {0.0f, 1.0f, 0.0f};
            }

            it.world().each([&](flecs::entity entity, Actor, CharacterMotor&, LocalTransformation& local) {
                const std::uint64_t id = static_cast<std::uint64_t>(entity.id());
                if (!physics.world->hasCharacter(id)) {
                    return;
                }
                const JPH::RVec3 feet = physics.world->characterPosition(id);
                local.position = {
                    static_cast<float>(feet.GetX()),
                    static_cast<float>(feet.GetY()),
                    static_cast<float>(feet.GetZ()),
                };

                const JPH::Vec3 vel = physics.world->characterVelocity(id);
                const float horizSq = vel.GetX() * vel.GetX() + vel.GetZ() * vel.GetZ();
                if (horizSq > 1.0e-4f && entity.has<SpriteInstance>()) {
                    entity.get_mut<SpriteInstance>().facingYaw =
                        std::atan2(vel.GetX(), vel.GetZ());
                }
            });
        });

    world.system("TriggerOverlap")
        .kind(flecs::OnUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            if (!world.has<ScriptContext>()) {
                return;
            }
            s7_scheme* scheme = world.get<ScriptContext>().scheme;

            std::vector<TriggerCandidate> candidates;
            world.each([&](flecs::entity entity, const CollisionTags& tags, const CharacterMotor& motor) {
                TriggerCandidate candidate{};
                candidate.entity = entity;
                candidate.tags = tags.tags;
                candidate.bounds = capsuleAabb(candidateFeet(entity, motor), motor);
                candidates.push_back(std::move(candidate));
            });

            world.each([&](flecs::entity triggerEntity, TriggerVolume& volume, const LocalTransformation& local) {
                const Aabb volumeBounds = triggerAabb(local.position, volume.size);
                const std::string thingId = entityIdString(triggerEntity);

                std::unordered_set<std::uint64_t> currentlyInside;
                for (const TriggerCandidate& candidate : candidates) {
                    if (!tagsIntersect(volume.filterTags, candidate.tags)) {
                        continue;
                    }
                    if (!aabbOverlap(volumeBounds, candidate.bounds)) {
                        continue;
                    }

                    const std::uint64_t otherId =
                        static_cast<std::uint64_t>(candidate.entity.id());
                    currentlyInside.insert(otherId);

                    if (volume.inside.find(otherId) == volume.inside.end() &&
                        !volume.onEnter.empty()) {
                        tryCallMapHandlerEnterExit(
                            scheme,
                            volume.onEnter,
                            thingId,
                            entityIdString(candidate.entity),
                            ScriptScope::World);
                    }
                }

                for (const std::uint64_t previousId : volume.inside) {
                    if (currentlyInside.find(previousId) != currentlyInside.end()) {
                        continue;
                    }
                    if (volume.onExit.empty()) {
                        continue;
                    }
                    flecs::entity other = world.entity(previousId);
                    const std::string otherId = other.is_alive() ? entityIdString(other)
                                                                : std::to_string(previousId);
                    tryCallMapHandlerEnterExit(
                        scheme, volume.onExit, thingId, otherId, ScriptScope::World);
                }

                volume.inside = std::move(currentlyInside);
            });
        });

    world.system("UpdateFaceTouch")
        .kind(flecs::OnUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            if (!world.has<ScriptContext>()) {
                return;
            }
            s7_scheme* scheme = world.get<ScriptContext>().scheme;

            std::vector<TriggerCandidate> candidates;
            world.each([&](flecs::entity entity, const CollisionTags& tags, const CharacterMotor& motor) {
                TriggerCandidate candidate{};
                candidate.entity = entity;
                candidate.tags = tags.tags;
                candidate.bounds = capsuleAabb(candidateFeet(entity, motor), motor);
                candidates.push_back(std::move(candidate));
            });

            world.each([&](flecs::entity faceEntity, FaceTouch& touch, const FaceUseSurface& surface) {
                if (touch.onTouch.empty()) {
                    return;
                }
                const std::string faceId = entityIdString(faceEntity);
                std::unordered_set<std::uint64_t> currentlyInside;
                for (const TriggerCandidate& candidate : candidates) {
                    if (!tagsIntersect({}, candidate.tags)) {
                        continue;
                    }
                    const CharacterMotor& motor = candidate.entity.get<CharacterMotor>();
                    const Vector3 feet = candidateFeet(candidate.entity, motor);
                    if (!capsuleTouchesFace(feet, motor, surface, touch.depth)) {
                        continue;
                    }
                    const std::uint64_t otherId =
                        static_cast<std::uint64_t>(candidate.entity.id());
                    currentlyInside.insert(otherId);
                    if (touch.inside.find(otherId) == touch.inside.end()) {
                        tryCallMapHandlerEnterExit(
                            scheme,
                            touch.onTouch,
                            faceId,
                            entityIdString(candidate.entity),
                            ScriptScope::World);
                    }
                }
                touch.inside = std::move(currentlyInside);
            });
        });

    registerMotoredBodySystem(world);
}

void unregisterPhysicsModule(flecs::world& world) {
    if (world.has<PhysicsContext>()) {
        world.remove<PhysicsContext>();
    }
}

}
