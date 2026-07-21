#pragma once

#include <flecs.h>
#include <raylib.h>

namespace slopengine {

/** Tag: this entity is the first-person player camera. */
struct PlayerCamera {};

/** Handles for the eye-space stage root and sockets under Player. */
struct FirstPersonScene {
    flecs::entity_t root = 0;           /**< PlayerFp root (ViewSpace). */
    flecs::entity_t weaponSocket = 0;
    flecs::entity_t emissionSocket = 0;
    bool useRadTint = false;            /**< Tint viewmodels from map light probe. */
    bool useShading = false;            /**< Use viewmodel faux lighting shader. */
    bool radTintInitialized = false;
    Vector3 radTintSmoothed{1.0f, 1.0f, 1.0f};
};

/** Runtime on/off state for a light spawned under an FP socket. */
struct FpLightControl {
    float onIntensity = 1.0f;
    bool enabled = false;
};

/** Look state and free-move rates when not physics-driven. */
struct FirstPersonController {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float moveSpeed = 6.0f;
    float lookSensitivity = 0.003f;
    float eyeHeight = 1.7f;
};

/** Package-written view-space eye offset (meters). Applied only to presentation camera. */
struct ViewEyeOffset {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

}
