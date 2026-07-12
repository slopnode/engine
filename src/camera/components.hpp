#pragma once

namespace slopengine {

struct PlayerCamera {};

struct FirstPersonController {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float moveSpeed = 6.0f;
    float lookSensitivity = 0.003f;
    float eyeHeight = 1.7f;
};

}