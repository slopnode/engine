#include "camera.hpp"

#include <algorithm>
#include <cmath>

namespace slopmap {

namespace {

constexpr float kPi = 3.14159265358979323846f;

Vector3 normalizeSafe(Vector3 v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len <= 1e-8f) {
        return {0.0f, 0.0f, 0.0f};
    }
    return {v.x / len, v.y / len, v.z / len};
}

} // namespace

Vector3 FlyCamera::forward() const {
    const float cp = std::cos(pitch);
    return {
        std::sin(yaw) * cp,
        std::sin(pitch),
        std::cos(yaw) * cp,
    };
}

Vector3 FlyCamera::forwardFlat() const {
    return normalizeSafe({std::sin(yaw), 0.0f, std::cos(yaw)});
}

Vector3 FlyCamera::rightFlat() const {
    return normalizeSafe({-std::cos(yaw), 0.0f, std::sin(yaw)});
}

Camera3D FlyCamera::toRaylib() const {
    Camera3D camera{};
    camera.position = position;
    const Vector3 dir = forward();
    camera.target = {
        position.x + dir.x,
        position.y + dir.y,
        position.z + dir.z,
    };
    if (orthographic && viewPlane == ViewPlane::Top) {
        camera.up = {0.0f, 0.0f, 1.0f};
    } else {
        camera.up = {0.0f, 1.0f, 0.0f};
    }
    camera.fovy = orthographic ? orthoHalfHeight * 2.0f : 60.0f;
    camera.projection = orthographic ? CAMERA_ORTHOGRAPHIC : CAMERA_PERSPECTIVE;
    return camera;
}

void FlyCamera::update(bool allowInput, bool retainCursorHidden) {
    const bool flying = allowInput && IsMouseButtonDown(MOUSE_BUTTON_RIGHT);

    if (flying) {
        if (!orthographic) {
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

        const float dt = GetFrameTime();
        float speed = moveSpeed;
        if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
            speed *= fastMultiplier;
        }
        const float step = speed * dt;

        Vector3 move{};
        if (orthographic) {
            const Vector3 right = rightFlat();
            Vector3 planeUp{};
            Vector3 normal{};
            switch (viewPlane) {
            case ViewPlane::Top:
                planeUp = {0.0f, 0.0f, 1.0f};
                normal = {0.0f, 1.0f, 0.0f};
                break;
            case ViewPlane::Front:
                planeUp = {0.0f, 1.0f, 0.0f};
                normal = {0.0f, 0.0f, 1.0f};
                break;
            case ViewPlane::Side:
                planeUp = {0.0f, 1.0f, 0.0f};
                normal = {1.0f, 0.0f, 0.0f};
                break;
            case ViewPlane::PerspectiveY0:
            default:
                planeUp = {0.0f, 1.0f, 0.0f};
                normal = {0.0f, 1.0f, 0.0f};
                break;
            }
            if (IsKeyDown(KEY_W)) {
                move.x += planeUp.x;
                move.y += planeUp.y;
                move.z += planeUp.z;
            }
            if (IsKeyDown(KEY_S)) {
                move.x -= planeUp.x;
                move.y -= planeUp.y;
                move.z -= planeUp.z;
            }
            if (IsKeyDown(KEY_D)) {
                move.x += right.x;
                move.y += right.y;
                move.z += right.z;
            }
            if (IsKeyDown(KEY_A)) {
                move.x -= right.x;
                move.y -= right.y;
                move.z -= right.z;
            }
            if (IsKeyDown(KEY_E) || IsKeyDown(KEY_SPACE)) {
                move.x += normal.x;
                move.y += normal.y;
                move.z += normal.z;
            }
            if (IsKeyDown(KEY_Q) || IsKeyDown(KEY_LEFT_CONTROL)) {
                move.x -= normal.x;
                move.y -= normal.y;
                move.z -= normal.z;
            }
        } else {
            const Vector3 fwd = forward();
            const Vector3 right = rightFlat();
            if (IsKeyDown(KEY_W)) {
                move.x += fwd.x;
                move.y += fwd.y;
                move.z += fwd.z;
            }
            if (IsKeyDown(KEY_S)) {
                move.x -= fwd.x;
                move.y -= fwd.y;
                move.z -= fwd.z;
            }
            if (IsKeyDown(KEY_D)) {
                move.x += right.x;
                move.z += right.z;
            }
            if (IsKeyDown(KEY_A)) {
                move.x -= right.x;
                move.z -= right.z;
            }
            if (IsKeyDown(KEY_E) || IsKeyDown(KEY_SPACE)) {
                move.y += 1.0f;
            }
            if (IsKeyDown(KEY_Q) || IsKeyDown(KEY_LEFT_CONTROL)) {
                move.y -= 1.0f;
            }
        }

        const float moveLen = std::sqrt(move.x * move.x + move.y * move.y + move.z * move.z);
        if (moveLen > 1e-6f) {
            position.x += (move.x / moveLen) * step;
            position.y += (move.y / moveLen) * step;
            position.z += (move.z / moveLen) * step;
        }

        if (!IsCursorHidden()) {
            DisableCursor();
        }
        wasFlying = true;
    } else {
        if (wasFlying && retainCursorHidden) {
            EnableCursor();
            HideCursor();
        } else if (IsCursorHidden() && !retainCursorHidden) {
            EnableCursor();
        }
        wasFlying = false;
    }

    if (allowInput) {
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            if (orthographic) {
                orthoHalfHeight =
                    std::clamp(orthoHalfHeight * (wheel > 0.0f ? 0.9f : 1.1f), 1.0f, 64.0f);
            } else {
                const Vector3 dir = forward();
                position.x += dir.x * wheel * 0.75f;
                position.y += dir.y * wheel * 0.75f;
                position.z += dir.z * wheel * 0.75f;
            }
        }
    }
}

void FlyCamera::lookAt(Vector3 target) {
    const Vector3 to{
        target.x - position.x,
        target.y - position.y,
        target.z - position.z,
    };
    const float horiz = std::sqrt(to.x * to.x + to.z * to.z);
    yaw = std::atan2(to.x, to.z);
    pitch = std::atan2(to.y, std::max(horiz, 1e-6f));
    pitch = std::clamp(pitch, -1.45f, 1.45f);
}

}
