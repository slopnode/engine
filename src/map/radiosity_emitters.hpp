#pragma once

#include <raylib.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace slopengine {

inline constexpr int kTargetEmittersPerChart = 256;
inline constexpr int kMaxEmitterBlockSize = 8;
inline constexpr int kMaxEmittersBeforeMerge = 8192;
inline constexpr int kMergeTileSize = 4;
inline constexpr float kMinEmitterContrib = 1e-5f;
inline constexpr float kMinCastLuminance = 0.03f;
inline constexpr float kMaxMergeVariance = 0.15f;

/** One emissive luxel candidate before block merging. */
struct EmitterMergeCandidate {
    Vector3 position{};
    Vector3 radiance{};
};

/** Area-light patch used for inter-surface direct lighting. */
struct EmitterPatch {
    Vector3 position{};
    Vector3 normal{};
    Vector3 radiance{};
    float area = 0.0f;
    std::int32_t faceIndex = -1;
    std::int32_t interiorLeaf = -1;
};

float emitterLuminance(Vector3 radiance);

/** True when radiance is bright enough to cast light onto other surfaces. */
bool passesCastGate(Vector3 radiance);

/** True when luminance spread within a tile is low enough to merge safely. */
bool blockIsUniform(
    const std::vector<EmitterMergeCandidate>& candidates,
    float maxRelativeVariance = kMaxMergeVariance);

/** Emergency merge when cast emitter count exceeds budget. */
bool shouldMergeChart(int castEmitterCount);

/** Picks block size for emergency CPU-only merge fallback. */
int chooseEmitterBlockSize(int emissiveCount, int chartWidth, int chartHeight);

/** Merges emissive luxel candidates into one energy-weighted patch. */
std::optional<EmitterPatch> mergeEmitterBlock(
    const std::vector<EmitterMergeCandidate>& candidates,
    float luxelArea,
    Vector3 normal,
    float normalOffset,
    std::int32_t faceIndex,
    std::int32_t interiorLeaf);

/** True when even an optimistic form-factor upper bound is below the cutoff. */
bool emitterPairBelowThreshold(Vector3 radiance, float area, float dist2, float minDist2);

}
