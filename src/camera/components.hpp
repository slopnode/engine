#pragma once

#include <flecs.h>
#include <raylib.h>

namespace slopengine {

struct PlayerCamera {};

struct FirstPersonScene {
    flecs::entity_t root = 0;
    flecs::entity_t weaponSocket = 0;
    flecs::entity_t emissionSocket = 0;
    bool useRadTint = false;
    bool useShading = false;
    bool radTintInitialized = false;
    Vector3 radTintSmoothed{1.0f, 1.0f, 1.0f};
};

struct FpLightControl {
    float onIntensity = 1.0f;
    bool enabled = false;
};

struct FirstPersonController {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float moveSpeed = 6.0f;
    float lookSensitivity = 0.003f;
    float eyeHeight = 1.7f;
};

}
