#pragma once

namespace slopengine {

struct PlayerCamera {};

struct PlayerFlashlight {
    bool enabled = false;
    flecs::entity_t light = 0;
};

struct FirstPersonController {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float moveSpeed = 6.0f;
    float lookSensitivity = 0.003f;
    float eyeHeight = 1.7f;
};

}