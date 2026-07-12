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

namespace slopengine {

/** Precomputed global matrix keyframes for one animation clip. */
struct AnimClipMatrices {
    std::vector<std::vector<Matrix>> keyframes;
};

/** Loaded animation clips and lookup tables for a skeleton. */
struct AnimBank {
    std::string skeletonId;
    std::vector<AnimClip> clipMeta;
    std::vector<ModelAnimation> clips;
    std::vector<AnimClipMatrices> matrixClips;
    std::unordered_map<std::string, std::size_t> clipIndexByName;
};

/** Parses an animation asset from its text source into @p asset. */
bool parseAnimAsset(std::string_view source, AnimAsset& asset);

/** Loads binary track data from @p data into @p animation and optional @p matrixClip. */
bool loadTracksToModelAnimation(
    std::span<const std::byte> data,
    const SkeletonAsset& skeleton,
    ModelAnimation& animation,
    AnimClipMatrices* matrixClip);

/** Releases GPU resources held by @p animation. */
void unloadModelAnimation(ModelAnimation& animation);

/** Releases all clips and matrix data held by @p bank. */
void unloadAnimBank(AnimBank& bank);

/** Builds a runtime animation bank from @p asset using @p readTracks to load clip data. */
bool buildAnimBank(
    const AnimAsset& asset,
    const SkeletonAsset& skeleton,
    const std::function<std::vector<std::byte>(const AnimClip&)>& readTracks,
    AnimBank& bank);

}
