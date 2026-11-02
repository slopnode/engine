#pragma once

#include "physics/ragdoll_pose.hpp"

#include <flecs.h>
#include <raylib.h>

#include <cstdint>
#include <vector>

namespace slopengine {

/** Builds a ragdoll from @p model posed at @p world, starts it for @p entity and gives the
 *  entity its RagdollRig. Freezes the oldest running ragdoll first when @p maxActive are
 *  already simulating. */
bool startEntityRagdoll(
    flecs::entity entity,
    const Model& model,
    const Matrix& world,
    PhysicsWorld& physics,
    const std::vector<BoneHitbox>& boxes,
    const std::vector<std::uint8_t>& physical,
    const std::vector<std::uint8_t>& follow,
    Vector3 velocity,
    Vector3 angularVelocity,
    int hitBone,
    Vector3 hitVelocity,
    float lifetime,
    int maxActive = 16);

/** Poses every running ragdoll entity from its physics bodies. */
void registerRagdollSystems(flecs::world& world);

}
