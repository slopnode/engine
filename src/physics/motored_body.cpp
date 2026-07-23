#include "physics/motored_body.hpp"

#include "physics/components.hpp"
#include "physics/motored_sweep.hpp"
#include "physics/physics_module.hpp"
#include "render/components.hpp"
#include "script/scheme_call.hpp"
#include "script/script_context.hpp"
#include "script/thing_script.hpp"

#include <raylib.h>
#include <raymath.h>

#include <cmath>
#include <cstdint>
#include <string>

namespace slopengine {

namespace {

std::string entityIdString(flecs::entity entity) {
    const char* name = entity.name();
    if (name != nullptr && name[0] != '\0') {
        return name;
    }
    return std::to_string(static_cast<std::uint64_t>(entity.id()));
}

void impactMotoredBody(
    flecs::world& world,
    flecs::entity entity,
    MotoredBody& body,
    LocalTransformation& local,
    Vector3 impactPoint,
    Vector3 dir) {
    local.position = impactPoint;
    if (entity.has<SpriteInstance>()) {
        const float horiz = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        if (horiz > 1.0e-4f) {
            entity.get_mut<SpriteInstance>().facingYaw = std::atan2(dir.x, dir.z);
        }
    }

    if (!body.onImpact.empty() && world.has<ScriptContext>()) {
        tryCallSchemeProc1String3Reals(
            world.get<ScriptContext>().scheme,
            body.onImpact,
            entityIdString(entity),
            impactPoint.x,
            impactPoint.y,
            impactPoint.z,
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

            if (const auto wall = physics->castSphere(local.position, dir, distance, radius)) {
                bestFraction = wall->fraction;
                bestPoint = wall->point;
            }

            world.each([&](flecs::entity actorEntity, Actor, const CharacterMotor& motor,
                           const LocalTransformation& actorLocal) {
                if (actorEntity == entity) {
                    return;
                }
                if (const auto hit = sweepSphereActorCapsule(
                        local.position, dir, distance, radius, actorLocal.position, motor)) {
                    if (*hit < bestFraction) {
                        bestFraction = *hit;
                        bestPoint = Vector3Add(local.position, Vector3Scale(dir, distance * (*hit)));
                    }
                }
            });

            if (bestFraction <= 1.0f) {
                impactMotoredBody(world, entity, body, local, bestPoint, dir);
                return;
            }

            local.position = Vector3Add(local.position, Vector3Scale(dir, distance));
            if (entity.has<SpriteInstance>()) {
                const float horiz = std::sqrt(dir.x * dir.x + dir.z * dir.z);
                if (horiz > 1.0e-4f) {
                    entity.get_mut<SpriteInstance>().facingYaw = std::atan2(dir.x, dir.z);
                }
            }
        });
}

}
