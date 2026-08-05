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

namespace {

struct OrthoPanAxes {
    Vector3 right{};
    Vector3 planeUp{};
    Vector3 normal{};
};

OrthoPanAxes orthoPanAxes(ViewPlane plane, Vector3 rightFlatAxis) {
    OrthoPanAxes axes{};
    axes.right = rightFlatAxis;
    switch (plane) {
    case ViewPlane::Top:
        axes.planeUp = {0.0f, 0.0f, 1.0f};
        axes.normal = {0.0f, 1.0f, 0.0f};
        break;
    case ViewPlane::Front:
        axes.planeUp = {0.0f, 1.0f, 0.0f};
        axes.normal = {0.0f, 0.0f, 1.0f};
        break;
    case ViewPlane::Side:
        axes.planeUp = {0.0f, 1.0f, 0.0f};
        axes.normal = {1.0f, 0.0f, 0.0f};
        break;
    case ViewPlane::PerspectiveY0:
    default:
        axes.planeUp = {0.0f, 1.0f, 0.0f};
        axes.normal = {0.0f, 1.0f, 0.0f};
        break;
    }
    return axes;
}

} // namespace

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
            const OrthoPanAxes axes = orthoPanAxes(viewPlane, rightFlat());
            const Vector2 mouseDelta = GetMouseDelta();
            if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
                const float panScale = orthoHalfHeight * orthoPanSensitivity;
                position.x +=
                    (axes.right.x * mouseDelta.x - axes.planeUp.x * mouseDelta.y) * panScale;
                position.y +=
                    (axes.right.y * mouseDelta.x - axes.planeUp.y * mouseDelta.y) * panScale;
                position.z +=
                    (axes.right.z * mouseDelta.x - axes.planeUp.z * mouseDelta.y) * panScale;
            }
            if (IsKeyDown(KEY_W)) {
                move.x += axes.planeUp.x;
                move.y += axes.planeUp.y;
                move.z += axes.planeUp.z;
            }
            if (IsKeyDown(KEY_S)) {
                move.x -= axes.planeUp.x;
                move.y -= axes.planeUp.y;
                move.z -= axes.planeUp.z;
            }
            if (IsKeyDown(KEY_D)) {
                move.x += axes.right.x;
                move.y += axes.right.y;
                move.z += axes.right.z;
            }
            if (IsKeyDown(KEY_A)) {
                move.x -= axes.right.x;
                move.y -= axes.right.y;
                move.z -= axes.right.z;
            }
            if (IsKeyDown(KEY_E) || IsKeyDown(KEY_SPACE)) {
                move.x += axes.normal.x;
                move.y += axes.normal.y;
                move.z += axes.normal.z;
            }
            if (IsKeyDown(KEY_Q) || IsKeyDown(KEY_LEFT_CONTROL)) {
                move.x -= axes.normal.x;
                move.y -= axes.normal.y;
                move.z -= axes.normal.z;
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
            if (orthographic) {
                HideCursor();
            } else {
                DisableCursor();
            }
        }
        wasFlying = true;
    } else {
        if (wasFlying && retainCursorHidden) {
            EnableCursor();
            HideCursor();
        } else if (IsCursorHidden() && !retainCursorHidden) {
            if (orthographic) {
                ShowCursor();
            } else {
                EnableCursor();
            }
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
