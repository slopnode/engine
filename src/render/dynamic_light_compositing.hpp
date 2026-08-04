#pragma once

#include <raymath.h>

namespace slopengine {

constexpr float kDynamicOverlayMinScale = 0.25f;
constexpr float kDynamicOverlayLumaStart = 0.70f;
constexpr float kDynamicOverlayLumaEnd = 0.95f;

float tonemapDisplayChannel(float linear);
Vector3 tonemapDisplay(Vector3 linear);
float displayLuminance(Vector3 display);
float dynamicOverlayScale(float bakedDisplayLuma);
Vector3 composeLumaAwareOverlay(Vector3 bakedDisplay, Vector3 dynamicLinear);

} // namespace slopengine
