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
    float swimSpeed = 3.0f;  /**< Horizontal move speed while submerged past the swim threshold. */
    float waterDrag = 1.0f;  /**< Vertical velocity damping rate (1/s) while swimming. */
    float buoyancy = 2.0f;   /**< Upward accel (m/s^2) at full submersion; blended with gravity by submersion fraction. */
    float waterExitReach = 0.6f;  /**< Max reach (m) of the water-exit ledge probe, both for how far
                                        above the true water surface a lip can still be detected and
                                        how far below a found lip the standable floor can be. Also
                                        bounds how close to the surface the character must be before
                                        exiting is attempted at all -- deeper than this, no ledge is
                                        ever searched for. */
    float waterExitSpeed = 3.5f;  /**< Capped speed (m/s) at which a character is steered toward a
                                        confirmed water-exit ledge, once found. */
    float submersion = 0.0f;      /**< Runtime state: fraction (0..1) of body height inside water,
                                        refreshed each physics tick by the PhysicsStep system. Not
                                        thing-def config -- read-only from script's perspective. */
    bool nearWaterSurface = false; /**< Runtime state: mirrors the per-tick water-exit-ledge probe
                                         (only meaningful while submersion > 0); refreshed alongside
                                         submersion. */
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

/** Tag: package-driven world actor (not the first-person player).
 *  @ingroup physics_components
 */
struct Actor {};

/** Tag: dead actor kept for visuals; no physics or combat collision.
 *  @ingroup physics_components
 */
struct ActorCorpse {};

}
