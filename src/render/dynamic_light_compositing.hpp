#pragma once

#include <raymath.h>

namespace slopengine {

float tonemapDisplayChannel(float linear);
Vector3 tonemapDisplay(Vector3 linear);
float displayToLinearChannel(float display);
Vector3 displayToLinearIrradiance(Vector3 display);

/** Single-tonemap combine: tonemap(inverseTonemap(bakedDisplay) + dynamicLinear). */
Vector3 composeLinearLightingOverlay(
    Vector3 bakedDisplay,
    Vector3 dynamicLinear);

} // namespace slopengine
