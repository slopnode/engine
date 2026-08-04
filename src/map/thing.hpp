#pragma once

#include "map/handler_binding.hpp"
#include "physics/components.hpp"

#include <raylib.h>

#include "map/sky_types.hpp"

#include <array>
#include <string>
#include <vector>

namespace slopengine {

enum class ThingKind {
    PlayerStart,
    Prop,
    Usable,
    Pickup,
    Actor,
    Mover,
    Trigger,
    PointLight,
    SpotLight,
    AreaLight,
    Sun,
    AmbientLight,
    Skybox,
    Prefab,
    SoundSource,
    Marker,
    Particle,
};

/** Runtime component: catalog type id for a spawned map thing (empty if legacy). */
struct ThingTypeRef {
    std::string type;
};

struct Thing {
    ThingKind kind = ThingKind::Prop;
    std::string type;
    std::string id;
    Vector3 at{0.0f, 0.0f, 0.0f};
    bool haveAt = false;
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool havePitch = false;
    Vector3 angles{0.0f, 0.0f, 0.0f};
    bool haveAngles = false;

    std::string sprite;
    std::string geo;
    std::string brush;
    std::string frame = "A";
    std::string animClip;
    bool animLoop = true;
    bool haveAnim = false;

    std::string prompt = "Interact";
    HandlerBinding onUse;
    bool havePrompt = false;

    Vector3 moverPivot{0.0f, 0.0f, 0.0f};
    bool haveMoverPivot = false;
    Vector3 moverOpenOffset{0.0f, 0.0f, 0.0f};
    bool haveMoverOpenOffset = false;
    float moverOpenAngle = 0.0f;
    int moverRotAxis = 1;
    bool haveMoverOpenAngle = false;
    float moverDuration = 0.8f;
    bool haveMoverDuration = false;
    float moverAutoClose = 0.0f;
    bool haveMoverAutoClose = false;
    Vector3 moverCollideSize{1.0f, 2.0f, 0.1f};
    bool haveMoverCollideSize = false;
    Vector3 moverCollideCenter{0.0f, 1.0f, 0.0f};
    bool haveMoverCollideCenter = false;
    std::string moverBlockMode = "shove";
    std::string moverPush = "full";
    bool moverSlide = true;
    bool haveMoverSlide = false;
    std::string onCrush;
    std::string moverGroup;

    HandlerBinding onEnter;
    HandlerBinding onExit;
    Vector3 triggerSize{1.0f, 1.0f, 1.0f};
    bool haveTriggerSize = false;
    std::vector<std::string> collideTags;
    std::vector<std::string> tags;

    float motorRadius = 0.3f;
    float motorHeight = 1.1f;
    float motorSpeed = 6.0f;
    float motorGravity = 9.81f;
    float motorStepHeight = 0.4f;
    CharacterHull motorHull = CharacterHull::Capsule;
    CharacterMoveMode motorMoveMode = CharacterMoveMode::Slide;
    bool haveMotor = false;

    bool haveSight = false;
    bool sightEnabled = true;
    float sightRange = 32.0f;
    float sightFovDegrees = 180.0f;
    float sightEyeLift = 0.75f;
    std::vector<std::string> sightSeeTags;
    std::vector<std::string> sightIgnoreTags;
    std::string sightFilterProc;

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

    std::string particleSystem;
    bool particlePlay = true;
    bool haveParticlePlay = false;

    SkyboxMode skyboxMode = SkyboxMode::Solid;
    bool haveSkyboxMode = false;
    std::string skyCubePx;
    std::string skyCubeNx;
    std::string skyCubePy;
    std::string skyCubeNy;
    std::string skyCubePz;
    std::string skyCubeNz;
    std::array<SkyGradientStop, 4> skyGradientStops{};
    int skyGradientStopCount = 0;
    std::string skyMaterial;
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

inline Thing makeDefaultMarkerThing() {
    Thing t{};
    t.kind = ThingKind::Marker;
    return t;
}

inline Thing makeDefaultParticleThing() {
    Thing t{};
    t.kind = ThingKind::Particle;
    t.particleSystem = "fx/generic-smoke";
    t.particlePlay = true;
    return t;
}

inline Thing makeDefaultSkyboxThing() {
    Thing t{};
    t.kind = ThingKind::Skybox;
    t.skyMaterial = "engine/sky";
    return t;
}

inline void ensureSkyboxGradientDefaults(Thing& t) {
    if (t.skyGradientStopCount == 4) {
        return;
    }
    t.skyGradientStops = {{
        {0.0f, {0.55f, 0.75f, 1.0f}},
        {0.25f, {0.45f, 0.65f, 0.95f}},
        {0.55f, {0.25f, 0.45f, 0.75f}},
        {1.0f, {0.05f, 0.10f, 0.25f}},
    }};
    t.skyGradientStopCount = 4;
}

inline const char* thingKindName(ThingKind kind) {
    switch (kind) {
    case ThingKind::PlayerStart:
        return "player-start";
    case ThingKind::Prop:
        return "prop";
    case ThingKind::Usable:
        return "usable";
    case ThingKind::Pickup:
        return "pickup";
    case ThingKind::Actor:
        return "actor";
    case ThingKind::Mover:
        return "mover";
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
    case ThingKind::AmbientLight:
        return "ambient-light";
    case ThingKind::Skybox:
        return "skybox";
    case ThingKind::Prefab:
        return "prefab";
    case ThingKind::SoundSource:
        return "sound-source";
    case ThingKind::Marker:
        return "marker";
    case ThingKind::Particle:
        return "particle";
    }
    return "thing";
}

inline bool thingKindIsLight(ThingKind kind) {
    return kind == ThingKind::PointLight || kind == ThingKind::SpotLight ||
        kind == ThingKind::AreaLight || kind == ThingKind::Sun ||
        kind == ThingKind::AmbientLight;
}

inline bool thingKindNeedsPresentation(ThingKind kind) {
    return kind == ThingKind::Prop || kind == ThingKind::Usable || kind == ThingKind::Pickup ||
        kind == ThingKind::Actor || kind == ThingKind::Mover;
}

}
