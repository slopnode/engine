#pragma once

#include <flecs.h>
#include <raylib.h>

namespace slopengine {

/** Tag: this entity is the first-person player camera.
 *  @ingroup camera_components
 */
struct PlayerCamera {};

/** Handles for the eye-space stage root and sockets under Player.
 *  @ingroup camera_components
 */
struct FirstPersonScene {
    flecs::entity_t root = 0;           /**< PlayerFp root (ViewSpace). */
    flecs::entity_t weaponSocket = 0;
    flecs::entity_t emissionSocket = 0;
    bool useRadTint = false;            /**< Tint viewmodels from map light probe. */
    bool useShading = false;            /**< Use viewmodel faux lighting shader. */
    bool radTintInitialized = false;
    Vector3 radTintSmoothed{1.0f, 1.0f, 1.0f};
};

/** Runtime on/off state for a light spawned under an FP socket.
 *  @ingroup camera_components
 */
struct FpLightControl {
    float onIntensity = 1.0f;
    bool enabled = false;
};

/** Look state and free-move rates when not physics-driven.
 *  @ingroup camera_components
 */
struct FirstPersonController {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float moveSpeed = 6.0f;
    float lookSensitivity = 0.003f;
    float eyeHeight = 1.7f;
    bool allowMove = true;
    bool allowLook = true;
    bool allowPitch = true;
};

/** Package-written view-space eye offset (meters). Applied only to presentation camera.
 *  @ingroup camera_components
 */
struct ViewEyeOffset {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

/** Detached debug spectator camera, driven independently of the Player entity.
 *  Singleton; only moves/renders from when DebugUiState::freeCamera is set.
 *  @ingroup camera_components
 */
struct FreeFlyCamera {
    bool active = false;
    Vector3 position{0.0f, 0.0f, 0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float moveSpeed = 8.0f;
    float fastMultiplier = 3.0f;
    float lookSensitivity = 0.003f;
};

}
