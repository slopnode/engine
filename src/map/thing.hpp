#pragma once

#include <raylib.h>

#include <string>
#include <vector>

namespace slopengine {

enum class ThingKind {
    PlayerStart,
    Prop,
    Usable,
    Trigger,
    PointLight,
    SpotLight,
    AreaLight,
    Sun,
    Prefab,
};

struct Thing {
    ThingKind kind = ThingKind::Prop;
    std::string id;
    Vector3 at{0.0f, 0.0f, 0.0f};
    bool haveAt = false;
    float yaw = 0.0f;
    Vector3 angles{0.0f, 0.0f, 0.0f};
    bool haveAngles = false;

    std::string sprite;
    std::string geo;
    std::string frame = "A";
    std::string animClip;
    bool animLoop = true;
    bool haveAnim = false;

    std::string prompt = "Interact";
    std::string onUse;

    std::string onEnter;
    std::string onExit;
    Vector3 triggerSize{1.0f, 1.0f, 1.0f};
    bool haveTriggerSize = false;
    std::vector<std::string> collideTags;

    Vector3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 8.0f;
    float coneAngle = 0.7f;
    Vector2 size{1.0f, 1.0f};

    std::string prefabPath;
};

struct ThingDocument {
    std::vector<Thing> things;
};

inline Thing makeDefaultLightThing(ThingKind kind) {
    Thing t{};
    t.kind = kind;
    t.color = {1.0f, 1.0f, 1.0f};
    t.intensity = 1.0f;
    t.range = 8.0f;
    t.coneAngle = 0.7f;
    t.size = {1.0f, 1.0f};
    return t;
}

inline const char* thingKindName(ThingKind kind) {
    switch (kind) {
    case ThingKind::PlayerStart:
        return "player-start";
    case ThingKind::Prop:
        return "prop";
    case ThingKind::Usable:
        return "usable";
    case ThingKind::Trigger:
        return "trigger";
    case ThingKind::PointLight:
        return "point-light";
    case ThingKind::SpotLight:
        return "spot-light";
    case ThingKind::AreaLight:
        return "area-light";
    case ThingKind::Sun:
        return "sun";
    case ThingKind::Prefab:
        return "prefab";
    }
    return "thing";
}

inline bool thingKindIsLight(ThingKind kind) {
    return kind == ThingKind::PointLight || kind == ThingKind::SpotLight ||
        kind == ThingKind::AreaLight || kind == ThingKind::Sun;
}

inline bool thingKindNeedsPresentation(ThingKind kind) {
    return kind == ThingKind::Prop || kind == ThingKind::Usable;
}

}
