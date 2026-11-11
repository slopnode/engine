#pragma once

#include "physics/physics_world.hpp"
#include "render/model_hitboxes.hpp"

#include <raylib.h>
#include <raymath.h>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace slopengine {

/** Maps a ragdoll's parts back onto an entity's skeleton. Each driven bone keeps its pose
 *  relative to one part from the moment the ragdoll started, and the entity follows the root
 *  part so culling bounds stay with the body.
 *  @ingroup physics_components
 */
struct RagdollRig {
    std::vector<int> partBone;
    std::vector<int> boneDriver;
    std::vector<Matrix> skinOffset;
    std::vector<Matrix> poseOffset;
    Matrix entityOffset = MatrixIdentity();
    int rootPart = 0;
    float age = 0.0f;
    float lifetime = 30.0f;
    bool frozen = false;
};

struct RagdollJointLimit {
    float swing = 0.5f;
    float twist = 0.3f;
};

/** Default joint limits in radians, picked from the bone's name. */
RagdollJointLimit ragdollJointLimit(std::string_view boneName);

struct RagdollBuild {
    RagdollRig rig;
    std::vector<RagdollPartDesc> parts;
};

/** Builds parts from the posed hitboxes of the bones in @p physical, parents first, and drives
 *  every bone in @p follow from its nearest physical ancestor (or the root part). @p hitBone's
 *  part, or its nearest physical ancestor's, gets @p hitVelocity on top of @p velocity. */
std::optional<RagdollBuild> buildRagdoll(
    const Model& model,
    const std::vector<BoneHitbox>& boxes,
    const Matrix& world,
    const std::vector<std::uint8_t>& physical,
    const std::vector<std::uint8_t>& follow,
    Vector3 velocity,
    Vector3 angularVelocity,
    int hitBone,
    Vector3 hitVelocity);

/** Writes skin matrices and bone poses from @p partWorld and returns the entity's new world
 *  matrix. Hidden bones are collapsed onto their joints afterwards. */
Matrix applyRagdollPose(
    const RagdollRig& rig,
    const std::vector<Matrix>& partWorld,
    Model& model,
    const std::uint8_t* hidden);

}
