#pragma once

#include <raymath.h>

namespace slopengine {

float tonemapDisplayChannel(float linear);
Vector3 tonemapDisplay(Vector3 linear);
float displayLuminance(Vector3 display);
Vector3 composeDisplayAdditiveOverlay(
    Vector3 bakedDisplay,
    Vector3 dynamicLinear,
    float dynamicBoost = 1.0f);

} // namespace slopengine
