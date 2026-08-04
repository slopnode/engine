#include "map/radiosity_emitters.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace slopengine {

namespace {

constexpr float kPi = 3.14159265358979323846f;

Vector3 add3(Vector3 a, Vector3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 scale3(Vector3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}

} // namespace

float emitterLuminance(Vector3 radiance) {
    return 0.2126f * radiance.x + 0.7152f * radiance.y + 0.0722f * radiance.z;
}

bool passesCastGate(Vector3 radiance) {
    return emitterLuminance(radiance) >= kMinCastLuminance;
}

bool blockIsUniform(
    const std::vector<EmitterMergeCandidate>& candidates,
    float maxRelativeVariance) {
    if (candidates.size() <= 1) {
        return true;
    }
    float minLum = std::numeric_limits<float>::max();
    float maxLum = 0.0f;
    for (const EmitterMergeCandidate& candidate : candidates) {
        const float lum = emitterLuminance(candidate.radiance);
        minLum = std::min(minLum, lum);
        maxLum = std::max(maxLum, lum);
    }
    if (maxLum <= 0.0f) {
        return true;
    }
    return (maxLum - minLum) / maxLum < maxRelativeVariance;
}

bool shouldMergeChart(int castEmitterCount) {
    return castEmitterCount > kMaxEmittersBeforeMerge;
}

int chooseEmitterBlockSize(int emissiveCount, int chartWidth, int chartHeight) {
    if (emissiveCount <= kTargetEmittersPerChart) {
        return 1;
    }
    int blockSize = static_cast<int>(std::ceil(
        std::sqrt(static_cast<float>(emissiveCount) / static_cast<float>(kTargetEmittersPerChart))));
    blockSize = std::clamp(blockSize, 1, kMaxEmitterBlockSize);
    const int maxByChart = std::max(1, std::min(chartWidth, chartHeight));
    return std::min(blockSize, maxByChart);
}

std::optional<EmitterPatch> mergeEmitterBlock(
    const std::vector<EmitterMergeCandidate>& candidates,
    float luxelArea,
    Vector3 normal,
    float normalOffset,
    std::int32_t faceIndex,
    std::int32_t interiorLeaf) {
    if (candidates.empty()) {
        return std::nullopt;
    }

    float weightSum = 0.0f;
    Vector3 positionSum{};
    Vector3 radianceSum{};
    for (const EmitterMergeCandidate& candidate : candidates) {
        const float weight = emitterLuminance(candidate.radiance) * luxelArea;
        if (weight <= 0.0f) {
            continue;
        }
        weightSum += weight;
        positionSum.x += candidate.position.x * weight;
        positionSum.y += candidate.position.y * weight;
        positionSum.z += candidate.position.z * weight;
        radianceSum.x += candidate.radiance.x * weight;
        radianceSum.y += candidate.radiance.y * weight;
        radianceSum.z += candidate.radiance.z * weight;
    }
    if (weightSum <= 0.0f) {
        return std::nullopt;
    }

    const float invWeight = 1.0f / weightSum;
    EmitterPatch patch;
    patch.position = add3(
        scale3(positionSum, invWeight),
        scale3(normal, normalOffset));
    patch.normal = normal;
    patch.radiance = scale3(radianceSum, invWeight);
    patch.area = static_cast<float>(candidates.size()) * luxelArea;
    patch.faceIndex = faceIndex;
    patch.interiorLeaf = interiorLeaf;
    return patch;
}

bool emitterPairBelowThreshold(Vector3 radiance, float area, float dist2, float minDist2) {
    const float maxForm = area / (std::max(dist2, minDist2) * kPi);
    return emitterLuminance(radiance) * maxForm < kMinEmitterContrib;
}

}
