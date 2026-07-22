#pragma once

#include <raylib.h>

#include <string>
#include <vector>

namespace slopengine {

enum class ThingKind {
    PlayerStart,
    Prop,
    Usable,
    Actor,
    Trigger,
    PointLight,
    SpotLight,
    AreaLight,
    Sun,
    Prefab,
    SoundSource,
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
    std::vector<std::string> tags;

    float motorRadius = 0.3f;
    float motorHeight = 1.1f;
    float motorSpeed = 6.0f;
    float motorGravity = 9.81f;
    bool haveMotor = false;

    Vector3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 8.0f;
    float coneAngle = 0.7f;
    Vector2 size{1.0f, 1.0f};

    std::string prefabPath;

    std::string audio;
    std::string clip;
    float volume = 1.0f;
    float minDistance = 1.0f;
    float maxDistance = 30.0f;
    bool looping = true;
    bool spatial = true;
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

inline Thing makeDefaultSoundSourceThing() {
    Thing t{};
    t.kind = ThingKind::SoundSource;
    t.volume = 1.0f;
    t.minDistance = 1.0f;
    t.maxDistance = 30.0f;
    t.looping = true;
    t.spatial = true;
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
    case ThingKind::Actor:
        return "actor";
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
    case ThingKind::SoundSource:
        return "sound-source";
    }
    return "thing";
}

inline bool thingKindIsLight(ThingKind kind) {
    return kind == ThingKind::PointLight || kind == ThingKind::SpotLight ||
        kind == ThingKind::AreaLight || kind == ThingKind::Sun;
}

inline bool thingKindNeedsPresentation(ThingKind kind) {
    return kind == ThingKind::Prop || kind == ThingKind::Usable || kind == ThingKind::Actor;
}

}
