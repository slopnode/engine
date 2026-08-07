#pragma once

#include <raylib.h>

namespace slopengine {

struct SeamStitchParams {
    float maxDistance = 0.0f;
    float normalCosThreshold = 0.999f;
    float planeEps = 1e-4f;
};

bool luxelsShareSeam(
    Vector3 posA,
    Vector3 normalA,
    Vector3 posB,
    Vector3 normalB,
    const SeamStitchParams& params);

float seamBlendWeight(float distance, float maxDistance);

} // namespace slopengine
