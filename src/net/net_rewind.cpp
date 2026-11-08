#include "net/net_rewind.hpp"

#include "game/sim_interpolation.hpp"
#include "net/net_state.hpp"
#include "physics/components.hpp"
#include "physics/physics_module.hpp"
#include "physics/physics_world.hpp"
#include "physics/ragdoll_rig.hpp"
#include "render/components.hpp"
#include "render/transform.hpp"

#include <raymath.h>

#include <algorithm>
#include <cmath>

namespace slopengine {

namespace {

PhysicsWorld* rewindPhysics(flecs::world& world) {
    return world.has<PhysicsContext>() ? world.get_mut<PhysicsContext>().world : nullptr;
}

bool rewindable(flecs::entity entity) {
    return !entity.parent().is_valid() && entity.name().length() > 0 &&
        (entity.has<Actor>() || entity.has<PlayerBody>()) && !entity.has<NetStandIn>();
}

const NetRewindPose* findPose(const NetRewindFrame& frame, flecs::entity_t entity) {
    const auto it = std::lower_bound(
        frame.poses.begin(), frame.poses.end(), entity,
        [](const NetRewindPose& pose, flecs::entity_t id) { return pose.entity < id; });
    return it != frame.poses.end() && it->entity == entity ? &*it : nullptr;
}

NetRewindPose capture(flecs::entity entity, const LocalTransformation& local, PhysicsWorld* physics) {
    NetRewindPose pose;
    pose.entity = entity.id();
    pose.position = local.position;
    pose.rotation = local.rotation;
    const std::uint64_t id = static_cast<std::uint64_t>(entity.id());
    if (physics != nullptr && physics->hasCharacter(id)) {
        const JPH::RVec3 feet = physics->characterPosition(id);
        pose.character = true;
        pose.feet = {static_cast<float>(feet.GetX()), static_cast<float>(feet.GetY()), static_cast<float>(feet.GetZ())};
    }
    return pose;
}

void place(flecs::entity entity, const NetRewindPose& pose, PhysicsWorld* physics) {
    LocalTransformation& local = entity.get_mut<LocalTransformation>();
    local.position = pose.position;
    local.rotation = pose.rotation;
    if (GlobalTransformation* global = entity.try_get_mut<GlobalTransformation>()) {
        updateTransform(entity, local, *global);
    }
    const std::uint64_t id = static_cast<std::uint64_t>(entity.id());
    if (pose.character && physics != nullptr && physics->hasCharacter(id)) {
        physics->placeCharacter(id, pose.feet.x, pose.feet.y, pose.feet.z);
    }
}

NetRewindPose blend(const NetRewindPose& from, const NetRewindPose* to, float alpha) {
    if (to == nullptr || alpha <= 0.0f) {
        return from;
    }
    if (Vector3DistanceSqr(from.position, to->position) > kSimPoseSnapDistance * kSimPoseSnapDistance) {
        return alpha >= 1.0f ? *to : from;
    }
    NetRewindPose out = from;
    out.position = Vector3Lerp(from.position, to->position, alpha);
    out.rotation = QuaternionSlerp(from.rotation, to->rotation, alpha);
    if (from.character && to->character) {
        out.feet = Vector3Lerp(from.feet, to->feet, alpha);
    }
    return out;
}

}

void recordNetRewind(flecs::world& world, std::uint64_t tick) {
    NetRewind& rewind = world.ensure<NetRewind>();
    if (rewind.maxTicks == 0) {
        rewind.frames.clear();
        return;
    }
    PhysicsWorld* physics = rewindPhysics(world);
    NetRewindFrame frame;
    frame.tick = tick;
    world.each([&](flecs::entity entity, const LocalTransformation& local) {
        if (rewindable(entity)) {
            frame.poses.push_back(capture(entity, local, physics));
        }
    });
    std::sort(frame.poses.begin(), frame.poses.end(),
              [](const NetRewindPose& a, const NetRewindPose& b) { return a.entity < b.entity; });
    if (!rewind.frames.empty() && rewind.frames.back().tick >= tick) {
        rewind.frames.clear();
    }
    rewind.frames.push_back(std::move(frame));
    while (rewind.frames.size() > rewind.maxTicks + 1) {
        rewind.frames.pop_front();
    }
}

void clearNetRewind(flecs::world& world) {
    if (world.has<NetRewind>()) {
        world.get_mut<NetRewind>().frames.clear();
    }
}

void beginNetRewind(flecs::world& world, flecs::entity shooter) {
    NetRewind& rewind = world.ensure<NetRewind>();
    if (++rewind.depth > 1) {
        return;
    }
    rewind.saved.clear();
    rewind.rewoundTo = -1.0;
    if (rewind.frames.empty() || !shooter.is_valid() || !shooter.has<NetView>()) {
        return;
    }
    const double newest = static_cast<double>(rewind.frames.back().tick);
    const double oldest = static_cast<double>(rewind.frames.front().tick);
    const double view = shooter.get<NetView>().tick;
    if (view <= 0.0) {
        return;
    }
    const double target = std::clamp(view, oldest, newest);
    if (target >= newest) {
        return;
    }
    std::size_t index = rewind.frames.size() - 1;
    while (index > 0 && static_cast<double>(rewind.frames[index].tick) > target) {
        --index;
    }
    const NetRewindFrame& from = rewind.frames[index];
    const NetRewindFrame* to = index + 1 < rewind.frames.size() ? &rewind.frames[index + 1] : nullptr;
    const float alpha = to == nullptr ? 0.0f
        : static_cast<float>(std::clamp(
              (target - static_cast<double>(from.tick)) / static_cast<double>(to->tick - from.tick), 0.0, 1.0));
    PhysicsWorld* physics = rewindPhysics(world);
    for (const NetRewindPose& then : from.poses) {
        const flecs::entity entity = world.entity(then.entity);
        if (entity == shooter || !world.is_alive(then.entity) || !entity.has<LocalTransformation>() ||
            entity.has<RagdollRig>() || entity.has<ActorCorpse>() || !rewindable(entity)) {
            continue;
        }
        rewind.saved.push_back(capture(entity, entity.get<LocalTransformation>(), physics));
        place(entity, blend(then, to != nullptr ? findPose(*to, then.entity) : nullptr, alpha), physics);
    }
    rewind.rewoundTo = target;
}

void endNetRewind(flecs::world& world) {
    if (!world.has<NetRewind>()) {
        return;
    }
    NetRewind& rewind = world.get_mut<NetRewind>();
    if (rewind.depth <= 0 || --rewind.depth > 0) {
        return;
    }
    PhysicsWorld* physics = rewindPhysics(world);
    for (auto it = rewind.saved.rbegin(); it != rewind.saved.rend(); ++it) {
        if (world.is_alive(it->entity) && world.entity(it->entity).has<LocalTransformation>() &&
            !world.entity(it->entity).has<RagdollRig>()) {
            place(world.entity(it->entity), *it, physics);
        }
    }
    rewind.saved.clear();
    rewind.rewoundTo = -1.0;
}

}
