#pragma once

#include <raylib.h>

#include <cstdint>
#include <span>
#include <vector>

namespace slopengine {

inline constexpr float kMinEmitterContrib = 1e-5f;
inline constexpr float kMinCastLuminance = 0.03f;

/** Textured area-light face used for inter-surface direct lighting. */
struct EmissiveFace {
    std::int32_t faceIndex = -1;
    std::int32_t interiorLeaf = -1;
    Vector3 normal{};
    Vector3 uAxis{};
    Vector3 vAxis{};
    float planeD = 0.0f;
    float uMin = 0.0f;
    float uMax = 0.0f;
    float vMin = 0.0f;
    float vMax = 0.0f;
    float area = 0.0f;
    float castRange = 0.0f; /**< Max cast distance; 0 = unlimited. */
    Vector3 aabbMins{};
    Vector3 aabbMaxs{};
    Vector3 peakRadiance{};
    int gridWidth = 0;
    int gridHeight = 0;
    int gridOffset = 0;
    /** Base index into a parallel EmitterDirectSample buffer, sized emitterDirectSamples^2;
     *  -1 when not built (solid-color emitters fall back to the blind fu/fv grid). */
    int directSampleOffset = -1;
};

/** One stratified direct-light sample, precomputed once per emissive face from the fine
 *  emission grid so the gather pass finds real content even when it's a thin mask feature. */
struct EmitterDirectSample {
    float u = 0.0f;
    float v = 0.0f;
    Vector3 radiance{};
};

float emitterLuminance(Vector3 radiance);

/** True when radiance is bright enough to cast light onto other surfaces. */
bool passesCastGate(Vector3 radiance);

/** True when even an optimistic form-factor upper bound is below the cutoff. */
bool emitterPairBelowThreshold(
    Vector3 radiance,
    float area,
    float dist2,
    float minDist2,
    float castRange = 0.0f);

/** Point-light-style falloff; returns 1 when @p range <= 0 (unlimited). */
float emitterRangeAttenuation(float dist, float range);

/** Squared distance from @p point to the closest point on an axis-aligned box. */
float dist2PointToAabb(Vector3 point, Vector3 mins, Vector3 maxs);

/** Bilinear sample from a face emission grid in world UV space. */
Vector3 sampleEmissionGridBilinear(
    const EmissiveFace& face,
    std::span<const Vector3> grid,
    float u,
    float v);

} // namespace slopengine
