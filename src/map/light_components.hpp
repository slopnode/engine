#pragma once

#include <raylib.h>

namespace slopengine {

struct PointLight {
    Vector3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 8.0f;
};

struct SpotLight {
    Vector3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 8.0f;
    float coneAngle = 0.7f;
};

struct AreaLight {
    Vector3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    Vector2 size{1.0f, 1.0f};
};

struct SunLight {
    Vector3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
};

}
