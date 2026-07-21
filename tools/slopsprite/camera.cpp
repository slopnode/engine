#include "camera.hpp"

#include <algorithm>
#include <cmath>

namespace slopsprite {

namespace {

constexpr float kPi = 3.14159265358979323846f;

} // namespace

Vector3 OrbitCamera::position() const {
    const float cp = std::cos(pitch);
    return {
        target.x + std::sin(yaw) * cp * distance,
        target.y + std::sin(pitch) * distance,
        target.z + std::cos(yaw) * cp * distance,
    };
}

Camera3D OrbitCamera::toRaylib() const {
    Camera3D camera{};
    camera.position = position();
    camera.target = target;
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    return camera;
}

void OrbitCamera::frameBounds(Vector3 center, float radius) {
    target = center;
    distance = std::clamp(radius * 2.5f, 0.5f, 40.0f);
}

void OrbitCamera::update(bool allowInput) {
    if (!allowInput) {
        return;
    }

    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        const Vector2 delta = GetMouseDelta();
        yaw -= delta.x * lookSensitivity;
        pitch -= delta.y * lookSensitivity;
        pitch = std::clamp(pitch, -1.45f, 1.45f);
        while (yaw > kPi) {
            yaw -= 2.0f * kPi;
        }
        while (yaw < -kPi) {
            yaw += 2.0f * kPi;
        }
    }

    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        distance = std::clamp(distance * (1.0f - wheel * zoomSensitivity), 0.5f, 40.0f);
    }
}

}
