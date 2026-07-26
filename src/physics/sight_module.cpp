#include "physics/sight_module.hpp"

#include "camera/components.hpp"
#include "map/bsp.hpp"
#include "map/pvs.hpp"
#include "physics/components.hpp"
#include "physics/physics_module.hpp"
#include "physics/sight_components.hpp"
#include "physics/sight_math.hpp"
#include "physics/trigger_components.hpp"
#include "render/components.hpp"
#include "script/hook_registry.hpp"
#include "script/scheme_call.hpp"
#include "script/script_context.hpp"
#include "script/script_scope.hpp"

#include <raylib.h>
#include <raymath.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace slopengine {

namespace {

std::string entityIdString(flecs::entity entity) {
    const char* name = entity.name();
    if (name != nullptr && name[0] != '\0') {
        return name;
    }
    return std::to_string(static_cast<std::uint64_t>(entity.id()));
}

Vector3 characterFeet(flecs::entity entity, const CharacterMotor& motor) {
    if (entity.has<Lens>()) {
        const Vector3 eye = entity.get<Lens>().camera.position;
        return {eye.x, eye.y - motor.eyeHeight, eye.z};
    }
    if (entity.has<LocalTransformation>()) {
        return entity.get<LocalTransformation>().position;
    }
    return {};
}

Vector3 characterEye(flecs::entity entity, const CharacterMotor& motor, float eyeLift) {
    if (entity.has<Lens>()) {
        return entity.get<Lens>().camera.position;
    }
    const Vector3 feet = characterFeet(entity, motor);
    return {
        feet.x,
        feet.y + sightEyeOffset(motor.height, motor.radius, eyeLift),
        feet.z,
    };
}

float characterYaw(flecs::entity entity) {
    if (entity.has<FirstPersonController>()) {
        return entity.get<FirstPersonController>().yaw;
    }
    if (entity.has<SpriteInstance>()) {
        return entity.get<SpriteInstance>().facingYaw;
    }
    if (entity.has<LocalTransformation>()) {
        const Vector3 euler = QuaternionToEuler(entity.get<LocalTransformation>().rotation);
        return euler.y;
    }
    return 0.0f;
}

bool losClear(PhysicsWorld* physics, Vector3 from, Vector3 to) {
    if (physics == nullptr) {
        return true;
    }
    const Vector3 delta = Vector3Subtract(to, from);
    const float distance = Vector3Length(delta);
    if (distance <= 1.0e-6f) {
        return true;
    }
    const Vector3 dir = Vector3Scale(delta, 1.0f / distance);
    return !physics->castRay(from, dir, distance).has_value();
}

bool pvsClear(flecs::world& world, Vector3 from, Vector3 to) {
    if (!world.has<MapPvs>() || !world.has<MapBsp>()) {
        return true;
    }
    return pvsVisiblePoints(world.get<MapBsp>().tree, world.get<MapPvs>().pvs, from, to);
}

bool packageAllowsSight(
    s7_scheme* scheme,
    const ActorSight& sight,
    const std::string& observerId,
    const std::string& targetId) {
    if (scheme == nullptr) {
        return true;
    }
    if (!sight.filterProc.empty() &&
        !tryCallSchemeProc2StringTruthy(
            scheme, sight.filterProc, observerId, targetId, ScriptScope::World)) {
        return false;
    }
    return callHook2StringAllTruthy(
        scheme, "sight-filter", observerId, targetId, ScriptScope::World);
}

enum class SightProbeResult {
    Rejected,
    Deferred,
    Visible,
    Occluded,
};

SightProbeResult probeSightPair(
    flecs::world& world,
    s7_scheme* scheme,
    PhysicsWorld* physics,
    flecs::entity observer,
    const ActorSight& sight,
    flecs::entity target,
    const CollisionTags& targetTags,
    const CharacterMotor& targetMotor,
    int& losBudget,
    bool spendBudget) {
    if (!observer.is_valid() || !target.is_valid() || observer == target) {
        return SightProbeResult::Rejected;
    }
    if (!observer.has<CharacterMotor>()) {
        return SightProbeResult::Rejected;
    }
    const CharacterMotor& observerMotor = observer.get<CharacterMotor>();
    const std::string observerId = entityIdString(observer);
    const std::string targetId = entityIdString(target);

    if (!sightTagsAllow(sight.seeTags, sight.ignoreTags, targetTags.tags)) {
        return SightProbeResult::Rejected;
    }
    if (!packageAllowsSight(scheme, sight, observerId, targetId)) {
        return SightProbeResult::Rejected;
    }

    const Vector3 fromEye = characterEye(observer, observerMotor, sight.eyeLift);
    const Vector3 toEye = characterEye(target, targetMotor, sight.eyeLift);
    const float dx = toEye.x - fromEye.x;
    const float dy = toEye.y - fromEye.y;
    const float dz = toEye.z - fromEye.z;
    const float distSq = dx * dx + dy * dy + dz * dz;
    if (distSq > sight.range * sight.range) {
        return SightProbeResult::Rejected;
    }
    if (!sightInFov(characterYaw(observer), dx, dz, sight.fovDegrees)) {
        return SightProbeResult::Rejected;
    }
    if (!pvsClear(world, fromEye, toEye)) {
        return SightProbeResult::Rejected;
    }
    if (spendBudget) {
        if (losBudget <= 0) {
            return SightProbeResult::Deferred;
        }
        --losBudget;
    }
    return losClear(physics, fromEye, toEye) ? SightProbeResult::Visible
                                             : SightProbeResult::Occluded;
}

void scanObserver(
    flecs::world& world,
    s7_scheme* scheme,
    PhysicsWorld* physics,
    flecs::entity observer,
    ActorSight& sight,
    int& losBudget) {
    if (!sight.enabled || losBudget <= 0) {
        return;
    }

    const std::string observerId = entityIdString(observer);
    std::unordered_set<std::string> next = sight.visible;

    world.each([&](flecs::entity target, const CharacterMotor& targetMotor, const CollisionTags& tags) {
        if (target == observer) {
            return;
        }
        const std::string targetId = entityIdString(target);
        const SightProbeResult result = probeSightPair(
            world,
            scheme,
            physics,
            observer,
            sight,
            target,
            tags,
            targetMotor,
            losBudget,
            true);
        switch (result) {
        case SightProbeResult::Rejected:
        case SightProbeResult::Occluded:
            next.erase(targetId);
            break;
        case SightProbeResult::Deferred:
            break;
        case SightProbeResult::Visible:
            if (!sight.visible.contains(targetId) && scheme != nullptr) {
                callHook2String(
                    scheme, "on-sight", observerId, targetId, ScriptScope::World);
            }
            next.insert(targetId);
            break;
        }
    });

    sight.visible = std::move(next);
}

} // namespace

void registerSightModule(flecs::world& world) {
    world.component<ActorSight>();
    world.set<SightScanState>(SightScanState{});

    world.system("SightScan")
        .kind(flecs::OnUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            if (!world.has<SightScanState>() || !world.has<PhysicsContext>()) {
                return;
            }
            PhysicsWorld* physics = world.get<PhysicsContext>().world;
            s7_scheme* scheme =
                world.has<ScriptContext>() ? world.get<ScriptContext>().scheme : nullptr;

            SightScanState& state = world.get_mut<SightScanState>();
            int losBudget = state.maxLosPerFrame;
            if (losBudget <= 0) {
                return;
            }

            std::vector<flecs::entity> observers;
            world.each([&](flecs::entity entity, ActorSight& sight) {
                if (sight.enabled) {
                    observers.push_back(entity);
                }
            });
            if (observers.empty()) {
                return;
            }

            const std::size_t count = observers.size();
            if (state.cursor >= count) {
                state.cursor = 0;
            }
            for (std::size_t n = 0; n < count && losBudget > 0; ++n) {
                const std::size_t index = (state.cursor + n) % count;
                flecs::entity observer = observers[index];
                if (!observer.is_alive() || !observer.has<ActorSight>()) {
                    continue;
                }
                ActorSight& sight = observer.get_mut<ActorSight>();
                scanObserver(world, scheme, physics, observer, sight, losBudget);
            }
            state.cursor = (state.cursor + 1) % count;
        });
}

bool actorCanSee(flecs::world& world, std::string_view fromId, std::string_view toId) {
    if (fromId.empty() || toId.empty()) {
        return false;
    }
    flecs::entity from = world.lookup(std::string(fromId).c_str());
    flecs::entity to = world.lookup(std::string(toId).c_str());
    if (!from.is_valid() || !to.is_valid() || !from.has<ActorSight>() ||
        !from.has<CharacterMotor>() || !to.has<CharacterMotor>() || !to.has<CollisionTags>()) {
        return false;
    }
    const ActorSight& sight = from.get<ActorSight>();
    if (!sight.enabled) {
        return false;
    }
    PhysicsWorld* physics =
        world.has<PhysicsContext>() ? world.get<PhysicsContext>().world : nullptr;
    s7_scheme* scheme =
        world.has<ScriptContext>() ? world.get<ScriptContext>().scheme : nullptr;
    int unlimited = 1;
    const SightProbeResult result = probeSightPair(
        world,
        scheme,
        physics,
        from,
        sight,
        to,
        to.get<CollisionTags>(),
        to.get<CharacterMotor>(),
        unlimited,
        false);
    return result == SightProbeResult::Visible;
}

}
