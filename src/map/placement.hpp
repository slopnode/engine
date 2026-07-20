#pragma once

#include <raylib.h>

#include <string>
#include <vector>

namespace slopengine {

/** Kind of authored placement in entities.s7 / prefab sidecars. */
enum class PlacementKind {
    PlayerStart,
    Prop,
    Usable,
    PointLight,
    SpotLight,
    AreaLight,
    Sun,
    Prefab,
};

/** One authored placement record before flecs spawn. */
struct Placement {
    PlacementKind kind = PlacementKind::Prop;
    std::string id;
    Vector3 at{0.0f, 0.0f, 0.0f};
    bool haveAt = false;
    float yaw = 0.0f;
    Vector3 angles{0.0f, 0.0f, 0.0f}; /**< Pitch, yaw, roll in radians. */
    bool haveAngles = false;

    std::string sprite;
    std::string geo;
    std::string frame = "A";
    std::string animClip;
    bool animLoop = true;
    bool haveAnim = false;

    std::string prompt = "Interact";
    std::string onUse; /**< Scheme procedure name for usable. */

    Vector3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 8.0f;
    float coneAngle = 0.7f;
    Vector2 size{1.0f, 1.0f};

    std::string prefabPath;
};

/** All placements loaded from one entities file. */
struct PlacementDocument {
    std::vector<Placement> placements;
};

/** Default light params for editor / palette creation. */
inline Placement makeDefaultLightPlacement(PlacementKind kind) {
    Placement p{};
    p.kind = kind;
    p.color = {1.0f, 1.0f, 1.0f};
    p.intensity = 1.0f;
    p.range = 8.0f;
    p.coneAngle = 0.7f;
    p.size = {1.0f, 1.0f};
    return p;
}

/** Map-form name for @p kind (e.g. "point-light"). */
inline const char* placementKindName(PlacementKind kind) {
    switch (kind) {
    case PlacementKind::PlayerStart:
        return "player-start";
    case PlacementKind::Prop:
        return "prop";
    case PlacementKind::Usable:
        return "usable";
    case PlacementKind::PointLight:
        return "point-light";
    case PlacementKind::SpotLight:
        return "spot-light";
    case PlacementKind::AreaLight:
        return "area-light";
    case PlacementKind::Sun:
        return "sun";
    case PlacementKind::Prefab:
        return "prefab";
    }
    return "placement";
}

inline bool placementKindIsLight(PlacementKind kind) {
    return kind == PlacementKind::PointLight || kind == PlacementKind::SpotLight ||
        kind == PlacementKind::AreaLight || kind == PlacementKind::Sun;
}

inline bool placementKindNeedsPresentation(PlacementKind kind) {
    return kind == PlacementKind::Prop || kind == PlacementKind::Usable;
}

}
