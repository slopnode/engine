#pragma once

namespace slopengine {

enum class CharacterHull : unsigned char {
    Capsule = 0,
    Box = 1,
};

enum class CharacterMoveMode : unsigned char {
    Slide = 0,
    TryMove = 1,
};

/** Character motor params and per-frame wish velocity.
 *  @ingroup physics_components
 */
struct CharacterMotor {
    float radius = 0.3f;   /**< Capsule radius, or box half-width/depth. */
    float height = 1.1f;   /**< Cylinder height between hemispheres (capsule), or box body height. */
    float moveSpeed = 6.0f;
    float gravity = 9.81f;
    float eyeHeight = 1.7f; /**< Camera offset above physics feet. */
    float wishX = 0.0f;     /**< Horizontal wish in world X (filled from input). */
    float wishZ = 0.0f;     /**< Horizontal wish in world Z (filled from input). */
    float stepHeight = 0.4f;
    CharacterHull hull = CharacterHull::Capsule;
    CharacterMoveMode moveMode = CharacterMoveMode::Slide;
};

/** Tag: package-driven world actor (not the first-person player). */
struct Actor {};

}
