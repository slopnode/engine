#pragma once

#include "assets/rigged_assets.hpp"

#include <raylib.h>

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace daggerlike {

struct AnimClipMatrices {
    std::vector<std::vector<Matrix>> keyframes;
};

struct AnimBank {
    std::string skeletonId;
    std::vector<AnimClip> clipMeta;
    std::vector<ModelAnimation> clips;
    std::vector<AnimClipMatrices> matrixClips;
    std::unordered_map<std::string, std::size_t> clipIndexByName;
};

bool parseAnimAsset(std::string_view source, AnimAsset& asset);
bool loadTracksToModelAnimation(
    std::span<const std::byte> data,
    const SkeletonAsset& skeleton,
    ModelAnimation& animation,
    AnimClipMatrices* matrixClip);
void unloadModelAnimation(ModelAnimation& animation);
void unloadAnimBank(AnimBank& bank);
bool buildAnimBank(
    const AnimAsset& asset,
    const SkeletonAsset& skeleton,
    const std::function<std::vector<std::byte>(const AnimClip&)>& readTracks,
    AnimBank& bank);

}
