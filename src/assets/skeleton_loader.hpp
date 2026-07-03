#pragma once

#include "assets/rigged_assets.hpp"

#include <raylib.h>

#include <span>
#include <string_view>
#include <vector>

namespace daggerlike {

bool parseSkeletonAsset(std::string_view source, SkeletonAsset& asset);
void applySkeletonToModel(const SkeletonAsset& asset, Model& model);
Model cloneGeoModelInstance(const Model& source);
void globalizePoseFromParentJoints(const BoneInfo* bones, int boneCount, Transform* transforms);
void globalizePoseFromParentJoints(const SkeletonAsset& skeleton, Transform* transforms);
bool loadSkeletonBindMatrices(std::span<const std::byte> data, std::vector<Matrix>& bindMatrices);
void applyBindPoseFromGlobalMatrices(Model& model, const std::vector<Matrix>& bindMatrices);
void allocateModelSkinningBuffers(Model& model);
void updateSkinnedMeshVertexBuffers(Model& model);
void updateRiggedModelAnimation(
    Model& model,
    const ModelAnimation& anim,
    float frame,
    const std::vector<Matrix>* bindGlobalMatrices,
    const std::vector<std::vector<Matrix>>* matrixKeyframes);

}
