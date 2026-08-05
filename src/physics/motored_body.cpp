#include "physics/motored_body.hpp"

#include "game/game_state.hpp"
#include "physics/components.hpp"
#include "physics/motored_sweep.hpp"
#include "physics/physics_module.hpp"
#include "physics/physics_world.hpp"
#include "render/components.hpp"
#include "render/transform.hpp"
#include "script/scheme_call.hpp"
#include "script/script_context.hpp"
#include "script/thing_script.hpp"

#include <raylib.h>
#include <raymath.h>

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace slopengine {

namespace {

std::string entityIdString(flecs::entity entity) {
    const char* name = entity.name();
    if (name != nullptr && name[0] != '\0') {
        return name;
    }
    return std::to_string(static_cast<std::uint64_t>(entity.id()));
}

std::optional<Vector3> motoredTargetFeet(flecs::entity entity, PhysicsWorld* physics) {
    if (physics != nullptr) {
        const std::uint64_t id = static_cast<std::uint64_t>(entity.id());
        if (physics->hasCharacter(id)) {
            const JPH::RVec3 feet = physics->characterPosition(id);
            return Vector3{
                static_cast<float>(feet.GetX()),
                static_cast<float>(feet.GetY()),
                static_cast<float>(feet.GetZ()),
            };
        }
    }
    if (entity.has<LocalTransformation>()) {
        return entity.get<LocalTransformation>().position;
    }
    return std::nullopt;
}

CharacterMotor motoredSweepMotor(const CharacterMotor& motor) {
    if (motor.hull != CharacterHull::Box) {
        return motor;
    }
    CharacterMotor sweep = motor;
    const float radius = motor.radius > 0.0f ? motor.radius : 0.3f;
    sweep.radius = radius * 1.41421356f;
    return sweep;
}

void trySweepCharacterTarget(
    flecs::entity projectileEntity,
    MotoredBody& body,
    flecs::entity targetEntity,
    const CharacterMotor& motor,
    Vector3 feet,
    Vector3 origin,
    Vector3 dir,
    float distance,
    float radius,
    float& bestFraction,
    Vector3& bestPoint,
    std::string& bestHitTarget) {
    if (!targetEntity.is_valid() || targetEntity == projectileEntity || targetEntity.has<ActorCorpse>()) {
        return;
    }
    if (!body.ignoreId.empty() && entityIdString(targetEntity) == body.ignoreId) {
        return;
    }
    const CharacterMotor sweepMotor = motoredSweepMotor(motor);
    if (const auto hit =
            sweepSphereActorCapsule(origin, dir, distance, radius, feet, sweepMotor)) {
        if (*hit < bestFraction) {
            bestFraction = *hit;
            bestPoint = Vector3Add(origin, Vector3Scale(dir, distance * (*hit)));
            bestHitTarget = entityIdString(targetEntity);
        }
    }
}

void impactMotoredBody(
    flecs::world& world,
    flecs::entity entity,
    MotoredBody& body,
    LocalTransformation& local,
    Vector3 impactPoint,
    Vector3 dir,
    std::string_view hitTarget) {
    local.position = impactPoint;
    if (entity.has<SpriteInstance>()) {
        const float horiz = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        if (horiz > 1.0e-4f) {
            entity.get_mut<SpriteInstance>().facingYaw = std::atan2(dir.x, dir.z);
        }
    }

    if (!body.onImpact.empty() && world.has<ScriptContext>()) {
        tryCallSchemeProc1String3Reals1OptString(
            world.get<ScriptContext>().scheme,
            body.onImpact,
            entityIdString(entity),
            impactPoint.x,
            impactPoint.y,
            impactPoint.z,
            hitTarget,
            ScriptScope::World);
    }
    queueThingDespawn(world, entityIdString(entity));
}

} // namespace

void registerMotoredBodySystem(flecs::world& world) {
    world.component<MotoredBody>();

    world.system<MotoredBody, LocalTransformation>("MotoredBodyIntegrate")
        .kind(flecs::OnUpdate)
        .each([](flecs::entity entity, MotoredBody& body, LocalTransformation& local) {
            flecs::world world = entity.world();
            if (isSimulationPaused(world)) {
                return;
            }
            if (!world.has<PhysicsContext>() || world.get<PhysicsContext>().world == nullptr) {
                return;
            }

            const float dt = GetFrameTime();
            if (dt <= 0.0f) {
                return;
            }

            body.age += dt;
            if (body.lifetime > 0.0f && body.age >= body.lifetime) {
                queueThingDespawn(world, entityIdString(entity));
                return;
            }

            body.velocity.y -= body.gravity * dt;

            const float speed = Vector3Length(body.velocity);
            if (speed <= 1.0e-6f) {
                return;
            }

            const Vector3 dir = Vector3Normalize(body.velocity);
            const float distance = speed * dt;
            PhysicsWorld* physics = world.get_mut<PhysicsContext>().world;
            const float radius = body.radius > 0.0f ? body.radius : 0.12f;

            float bestFraction = 2.0f;
            Vector3 bestPoint = local.position;
            std::string bestHitTarget;

            if (const auto wall = physics->castSphere(local.position, dir, distance, radius, BrushBlock::Projectile)) {
                bestFraction = wall->fraction;
                bestPoint = impactEffectPosition(wall->point, wall->normal);
                bestHitTarget.clear();
            }

            world.each([&](flecs::entity targetEntity, const CharacterMotor& motor) {
                const std::optional<Vector3> feet = motoredTargetFeet(targetEntity, physics);
                if (!feet.has_value()) {
                    return;
                }
                trySweepCharacterTarget(
                    entity,
                    body,
                    targetEntity,
                    motor,
                    *feet,
                    local.position,
                    dir,
                    distance,
                    radius,
                    bestFraction,
                    bestPoint,
                    bestHitTarget);
            });

            if (bestFraction <= 1.0f) {
                impactMotoredBody(world, entity, body, local, bestPoint, dir, bestHitTarget);
                return;
            }

            local.position = Vector3Add(local.position, Vector3Scale(dir, distance));
            if (entity.has<SpriteInstance>()) {
                const float horiz = std::sqrt(dir.x * dir.x + dir.z * dir.z);
                if (horiz > 1.0e-4f) {
                    entity.get_mut<SpriteInstance>().facingYaw = std::atan2(dir.x, dir.z);
                }
            }
            if (entity.has<LocalTransformation>() && entity.has<GlobalTransformation>()) {
                updateTransform(
                    entity,
                    entity.get_mut<LocalTransformation>(),
                    entity.get_mut<GlobalTransformation>());
            }
        });
}

}
