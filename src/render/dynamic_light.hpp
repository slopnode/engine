#pragma once

#include <raylib.h>
#include <raymath.h>

#include <cstdint>
#include <vector>

#include <flecs.h>

namespace slopengine {

constexpr int kMaxDynamicLights = 8;
constexpr int kMaxShadowedDynamicLights = 2;
constexpr int kDynamicShadowMapResolution = 512;
constexpr int kDynamicShadowFacesPerSlot = 6;

/** Runtime dynamic light shape. */
enum class DynamicLightKind {
    Point,
    Spot,
};

/** Color interpretation for DynamicLight::color. */
enum class DynamicLightColorSpace {
    Rgb,
    Hsv,
    Hsl,
};

/** Color stored in @p space; converted to linear RGB at gather time. */
struct DynamicLightColor {
    DynamicLightColorSpace space = DynamicLightColorSpace::Rgb;
    Vector3 value{1.0f, 1.0f, 1.0f};
};

/** Runtime light composited on top of baked lightmaps. */
struct DynamicLight {
    DynamicLightKind kind = DynamicLightKind::Point;
    DynamicLightColor color{};
    float intensity = 1.0f;
    float range = 8.0f;
    float coneAngle = 0.7f; /**< Spot half-angle in radians. */
    bool castShadows = false;
};

/** One ranked light ready for shader upload this frame. */
struct RankedDynamicLight {
    DynamicLight light{};
    Vector3 position{};
    Vector3 direction{0.0f, 0.0f, 1.0f};
    Vector3 linearRgb{1.0f, 1.0f, 1.0f};
    float score = 0.0f;
    int shadowSlot = -1; /**< Index into shadow state, or -1. */
};

/** Converts @p light color to linear RGB. */
Vector3 dynamicLightLinearRgb(const DynamicLight& light);
void setDynamicLightRgb(DynamicLight& light, Vector3 rgb);
void setDynamicLightHsv(DynamicLight& light, Vector3 hsv);
void setDynamicLightHsl(DynamicLight& light, Vector3 hsl);
void modulateDynamicLightHsv(DynamicLight& light, Vector3 hsvDelta);
void modulateDynamicLightHsl(DynamicLight& light, Vector3 hslDelta);

/** Spot direction from entity rotation. */
Vector3 dynamicLightDirectionFromRotation(Quaternion rotation);

/** Spawns a named DynamicLight entity at @p position. */
flecs::entity spawnDynamicLight(
    flecs::world& world,
    const char* name,
    Vector3 position,
    Quaternion rotation,
    const DynamicLight& light);

/** Evaluates one ranked light at @p point with surface @p normal. */
Vector3 evaluateDynamicLightAtPoint(
    const RankedDynamicLight& light,
    Vector3 point,
    Vector3 normal);

/** Sums ranked lights at @p point. */
Vector3 evaluateDynamicLightsAtPoint(
    const std::vector<RankedDynamicLight>& lights,
    Vector3 point,
    Vector3 normal);

/** Picks the best lights near @p focus up to the ranking caps. */
std::vector<RankedDynamicLight> rankDynamicLights(
    const std::vector<RankedDynamicLight>& candidates,
    Vector3 focus,
    int maxLights = kMaxDynamicLights,
    int maxShadowed = kMaxShadowedDynamicLights);

}
