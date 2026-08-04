#include "render/dynamic_light_compositing.hpp"

#include <algorithm>
#include <cmath>

namespace slopengine {

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

Vector3 composeDisplayAdditiveOverlay(
    Vector3 bakedDisplay,
    Vector3 dynamicLinear,
    float dynamicBoost) {
    const Vector3 dynamicDisplay = tonemapDisplay({
        dynamicLinear.x * dynamicBoost,
        dynamicLinear.y * dynamicBoost,
        dynamicLinear.z * dynamicBoost,
    });
    return {
        std::clamp(bakedDisplay.x + dynamicDisplay.x, 0.0f, 1.0f),
        std::clamp(bakedDisplay.y + dynamicDisplay.y, 0.0f, 1.0f),
        std::clamp(bakedDisplay.z + dynamicDisplay.z, 0.0f, 1.0f),
    };
}

} // namespace slopengine
