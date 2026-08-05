#pragma once

#include <raylib.h>

namespace slopmap {

enum class ViewPlane {
    PerspectiveY0,
    Top,
    Front,
    Side,
};

enum class ViewportLayout {
    Single,
    Quad,
};

constexpr int kViewportCount = 4;

struct FlyCamera {
    Vector3 position{0.0f, 2.5f, 8.0f};
    float yaw = 3.14159265f;
    float pitch = -0.35f;
    bool orthographic = false;
    ViewPlane viewPlane = ViewPlane::PerspectiveY0;
    float orthoHalfHeight = 8.0f;
    float moveSpeed = 10.0f;
    float fastMultiplier = 2.5f;
    float lookSensitivity = 0.003f;
    bool wasFlying = false;

    Camera3D toRaylib() const;
    void update(bool allowInput, bool retainCursorHidden = false);
    Vector3 forward() const;
    Vector3 forwardFlat() const;
    Vector3 rightFlat() const;
    void lookAt(Vector3 target);
};

}
