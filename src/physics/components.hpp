#pragma once

namespace slopengine {

/** Capsule character motor params and per-frame wish velocity.
 *  @ingroup physics_components
 */
struct CharacterMotor {
    float radius = 0.3f;   /**< Capsule radius in meters. */
    float height = 1.1f;   /**< Cylinder height between hemispheres. */
    float moveSpeed = 6.0f;
    float gravity = 9.81f;
    float eyeHeight = 1.7f; /**< Camera offset above physics feet. */
    float wishX = 0.0f;     /**< Horizontal wish in world X (filled from input). */
    float wishZ = 0.0f;     /**< Horizontal wish in world Z (filled from input). */
};

/** Tag: package-driven world actor (not the first-person player). */
struct Actor {};

}
