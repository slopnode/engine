#pragma once

namespace slopengine {

enum class CharacterHull : unsigned char {
    Capsule = 0,
    Box = 1,
    Sphere = 2,
};

enum class CharacterMoveMode : unsigned char {
    Slide = 0,
    TryMove = 1,
    Fly = 2,
};

/** Character motor params and per-frame wish velocity.
 *  @ingroup physics_components
 */
struct CharacterMotor {
    float radius = 0.3f;   /**< Capsule radius, or box half-width/depth. */
    float height = 0.88f;   /**< Cylinder height between hemispheres (capsule), or box body height. */
    float moveSpeed = 6.0f;
    float gravity = 9.81f;
    float eyeHeight = 0.8f; /**< Camera offset above physics feet. */
    float wishX = 0.0f;     /**< Horizontal wish in world X (filled from input). */
    float wishY = 0.0f;     /**< Vertical wish in world Y (flight mode). */
    float wishZ = 0.0f;     /**< Horizontal wish in world Z (filled from input). */
    float verticalSpeed = 3.0f; /**< Max climb/dive speed in flight mode. */
    float hoverHeight = 0.0f;   /**< Default cruise offset above floor (0 = script decides). */
    float stepHeight = 0.5f;
    CharacterHull hull = CharacterHull::Capsule;
    CharacterMoveMode moveMode = CharacterMoveMode::Slide;
};

inline float characterRadius(const CharacterMotor& motor) {
    return motor.radius > 0.0f ? motor.radius : 0.3f;
}

inline float characterBodyHeight(const CharacterMotor& motor) {
    return motor.height > 0.0f ? motor.height : 1.1f;
}

/** Vertical extent from physics feet (bottom) to top of hull. */
inline float characterTotalHeight(const CharacterMotor& motor) {
    if (motor.hull == CharacterHull::Sphere) {
        return 2.0f * characterRadius(motor);
    }
    return characterBodyHeight(motor) + 2.0f * characterRadius(motor);
}

/** Offset from physics feet to hull center along +Y. */
inline float characterCenterOffset(const CharacterMotor& motor) {
    if (motor.hull == CharacterHull::Sphere) {
        return characterRadius(motor);
    }
    return 0.5f * characterBodyHeight(motor) + characterRadius(motor);
}

/** Tag: package-driven world actor (not the first-person player). */
struct Actor {};

/** Tag: dead actor kept for visuals; no physics or combat collision. */
struct ActorCorpse {};

}
