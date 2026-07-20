#pragma once

#include <raylib.h>

namespace slopengine {

/** Omnidirectional placement light (baked by sloprad; not a DynamicLight). */
struct PointLight {
    Vector3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 8.0f;
};

/** Cone placement light (baked by sloprad; not a DynamicLight). */
struct SpotLight {
    Vector3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 8.0f;
    float coneAngle = 0.7f; /**< Cone half-angle in radians. */
};

/** Rectangular area light for authoring / gizmos (not a bake emitter yet). */
struct AreaLight {
    Vector3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    Vector2 size{1.0f, 1.0f};
};

/** Directional sun for authoring / gizmos (not a bake emitter yet). */
struct SunLight {
    Vector3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
};

}
