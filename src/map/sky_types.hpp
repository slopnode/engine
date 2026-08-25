#pragma once

#include <raylib.h>

#include <array>
#include <string>

namespace slopengine {

enum class SkyboxMode {
    Solid,
    Cube,
    Gradient,
    Cylinder,
};

struct SkyGradientStop {
    float position = 0.0f;
    Vector3 color{0.0f, 0.0f, 0.0f};
};

/** Runtime sky configuration from a map skybox thing or sky material. */
struct SkyboxSettings {
    SkyboxMode mode = SkyboxMode::Solid;
    Vector3 solidColor{0.0f, 0.0f, 0.0f};
    std::string cubeFaces[6];
    std::array<SkyGradientStop, 4> gradientStops{};
    int gradientStopCount = 0;
    std::string cylinderTexture;
    float cylinderOffset = 0.0f;
    float cylinderScale = 1.0f;
    int cylinderRepeat = 1;
};

}
