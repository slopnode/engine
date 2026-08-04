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

float displayToLinearChannel(float display) {
    return display / std::max(1.0f - display, 1e-4f);
}

Vector3 displayToLinearIrradiance(Vector3 display) {
    return {
        displayToLinearChannel(display.x),
        displayToLinearChannel(display.y),
        displayToLinearChannel(display.z),
    };
}

Vector3 composeLinearLightingOverlay(
    Vector3 bakedDisplay,
    Vector3 dynamicLinear) {
    const Vector3 irradiance = displayToLinearIrradiance(bakedDisplay);
    return tonemapDisplay({
        irradiance.x + dynamicLinear.x,
        irradiance.y + dynamicLinear.y,
        irradiance.z + dynamicLinear.z,
    });
}

} // namespace slopengine
