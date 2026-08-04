#include "render/dynamic_light_compositing.hpp"

#include <algorithm>
#include <cmath>

namespace slopengine {

namespace {

float saturate(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float smoothstep(float edge0, float edge1, float x) {
    const float t = saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

float tonemapDisplayChannel(float linear) {
    return linear / (1.0f + std::max(linear, 0.0f));
}

Vector3 tonemapDisplay(Vector3 linear) {
    return {
        tonemapDisplayChannel(linear.x),
        tonemapDisplayChannel(linear.y),
        tonemapDisplayChannel(linear.z),
    };
}

float displayLuminance(Vector3 display) {
    return 0.2126f * display.x + 0.7152f * display.y + 0.0722f * display.z;
}

float dynamicOverlayScale(float bakedDisplayLuma) {
    const float t = smoothstep(
        kDynamicOverlayLumaStart,
        kDynamicOverlayLumaEnd,
        bakedDisplayLuma);
    return 1.0f + (kDynamicOverlayMinScale - 1.0f) * t;
}

Vector3 composeLumaAwareOverlay(Vector3 bakedDisplay, Vector3 dynamicLinear) {
    const Vector3 dynamicDisplay = tonemapDisplay(dynamicLinear);
    const float dynScale = dynamicOverlayScale(displayLuminance(bakedDisplay));
    return {
        saturate(bakedDisplay.x + dynamicDisplay.x * dynScale),
        saturate(bakedDisplay.y + dynamicDisplay.y * dynScale),
        saturate(bakedDisplay.z + dynamicDisplay.z * dynScale),
    };
}

} // namespace slopengine
