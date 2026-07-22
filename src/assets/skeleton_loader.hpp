#pragma once

#include "assets/rigged_assets.hpp"

#include <raylib.h>

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace slopengine {

/** Parses a skeleton asset from its text source into @p asset. */
bool parseSkeletonAsset(std::string_view source, SkeletonAsset& asset);

/** Applies bone hierarchy and bind pose from @p asset to @p model. */
void applySkeletonToModel(const SkeletonAsset& asset, Model& model);

/** Creates a deep copy of @p source suitable for independent animation. */
Model cloneGeoModelInstance(const Model& source);

/** Frees clone-owned buffers from @p model without unloading shared meshes. */
void unloadClonedGeoModelInstance(Model& model);

/** Converts local joint transforms to global space using @p bones. */
void globalizePoseFromParentJoints(const BoneInfo* bones, int boneCount, Transform* transforms);

/** Converts local joint transforms to global space using @p skeleton. */
void globalizePoseFromParentJoints(const SkeletonAsset& skeleton, Transform* transforms);

/** Loads bind-pose matrices from binary @p data into @p bindMatrices. */
bool loadSkeletonBindMatrices(std::span<const std::byte> data, std::vector<Matrix>& bindMatrices);

/** Applies global bind-pose matrices from @p bindMatrices to @p model. */
void applyBindPoseFromGlobalMatrices(Model& model, const std::vector<Matrix>& bindMatrices);

/** Allocates GPU skinning buffers on @p model. */
void allocateModelSkinningBuffers(Model& model);

/** Uploads deformed skinned vertex data from @p model to the GPU. */
void updateSkinnedMeshVertexBuffers(Model& model);

/** Updates @p model for @p frame of @p anim, optionally using bind pose or matrix keyframes. */
void updateRiggedModelAnimation(
    Model& model,
    const ModelAnimation& anim,
    float frame,
    const std::vector<Matrix>* bindGlobalMatrices,
    const std::vector<std::vector<Matrix>>* matrixKeyframes);

}
