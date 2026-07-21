#pragma once

#include <raylib.h>

namespace slopsprite {

struct OrbitCamera {
    Vector3 target{0.0f, 0.5f, 0.0f};
    float distance = 3.0f;
    float yaw = 0.0f;
    float pitch = 0.35f;
    float moveSpeed = 4.0f;
    float lookSensitivity = 0.005f;
    float zoomSensitivity = 0.15f;

    Camera3D toRaylib() const;
    void update(bool allowInput);
    void frameBounds(Vector3 center, float radius);
    Vector3 position() const;
};

}
