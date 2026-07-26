#pragma once

#include <raylib.h>

namespace slopengine {

/** Omnidirectional placement light (baked by sloprad; not a DynamicLight).
 *  @ingroup map_components
 */
struct PointLight {
    Vector3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 8.0f;
};

/** Cone placement light (baked by sloprad; not a DynamicLight).
 *  @ingroup map_components
 */
struct SpotLight {
    Vector3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 8.0f;
    float coneAngle = 0.7f; /**< Cone half-angle in radians. */
};

/** Rectangular area light for authoring / gizmos (not a bake emitter yet).
 *  @ingroup map_components
 */
struct AreaLight {
    Vector3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    Vector2 size{1.0f, 1.0f};
};

/** Directional sun for bake + authoring gizmos.
 *  @ingroup map_components
 */
struct SunLight {
    Vector3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
};

/** Map-wide ambient fill for bake + runtime probe fallback.
 *  @ingroup map_components
 */
struct AmbientLight {
    Vector3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
};

}
