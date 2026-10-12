#include "path_draw.hpp"

#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace slopmap {

namespace {

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

void drawFrustumWedge(const slopengine::CameraPose& pose, float fovDeg, float depth, Color color) {
    const Vector3 forward = slopengine::cameraPoseForward(pose);
    Vector3 right = Vector3CrossProduct(forward, {0.0f, 1.0f, 0.0f});
    if (Vector3LengthSqr(right) < 1e-6f) {
        right = {1.0f, 0.0f, 0.0f};
    }
    right = Vector3Normalize(right);
    const Vector3 up = Vector3Normalize(Vector3CrossProduct(right, forward));
    const float halfH = std::tan(fovDeg * 0.5f * kDegToRad) * depth;
    const float halfW = halfH * (16.0f / 9.0f);
    const Vector3 center = Vector3Add(pose.position, Vector3Scale(forward, depth));
    const Vector3 corners[4] = {
        Vector3Add(center, Vector3Add(Vector3Scale(right, -halfW), Vector3Scale(up, halfH))),
        Vector3Add(center, Vector3Add(Vector3Scale(right, halfW), Vector3Scale(up, halfH))),
        Vector3Add(center, Vector3Add(Vector3Scale(right, halfW), Vector3Scale(up, -halfH))),
        Vector3Add(center, Vector3Add(Vector3Scale(right, -halfW), Vector3Scale(up, -halfH))),
    };
    for (int i = 0; i < 4; ++i) {
        DrawLine3D(pose.position, corners[i], color);
        DrawLine3D(corners[i], corners[(i + 1) % 4], color);
    }
}

bool isKnotSelected(const std::vector<EntityRef>& selected, int path, int knot) {
    for (const EntityRef& ref : selected) {
        if (ref.kind == EntityRef::Kind::Knot && ref.path == path && ref.index == knot) {
            return true;
        }
    }
    return false;
}

} // namespace

void drawCameraPaths(
    const slopengine::GraphDocument& graphs,
    const std::vector<EntityRef>& selected,
    int activePath,
    bool showGizmos,
    const std::optional<slopengine::CameraPose>& previewPose) {
    const Color activeCurve{255, 200, 80, 255};
    const Color idleCurve{200, 160, 90, 140};
    const Color knotColor{255, 170, 60, 255};
    const Color knotIdle{200, 150, 90, 170};
    const Color selectedColor{255, 255, 120, 255};
    const Color lookColor{255, 90, 90, 255};
    const Color wedgeColor{255, 140, 140, 150};

    std::vector<Vector3> points;
    for (std::size_t p = 0; p < graphs.paths.size(); ++p) {
        const slopengine::CameraPath& path = graphs.paths[p];
        const bool active = static_cast<int>(p) == activePath;
        points.clear();
        slopengine::tessellateCameraPath(path, 16, points);
        for (std::size_t i = 1; i < points.size(); ++i) {
            DrawLine3D(points[i - 1], points[i], active ? activeCurve : idleCurve);
        }
        const int segments = slopengine::cameraPathSegmentCount(path);
        for (std::size_t k = 0; k < path.knots.size(); ++k) {
            const slopengine::PathKnot& knot = path.knots[k];
            const bool sel = isKnotSelected(selected, static_cast<int>(p), static_cast<int>(k));
            const Color color = sel ? selectedColor : (active ? knotColor : knotIdle);
            DrawSphere(knot.at, sel ? 0.13f : 0.1f, color);
            if (k == 0) {
                DrawSphereWires(knot.at, 0.2f, 6, 6, color);
            }
            if (!showGizmos && !active) {
                continue;
            }
            slopengine::CameraPose pose{};
            if (segments > 0) {
                const int segment = std::min(static_cast<int>(k), segments - 1);
                const float u = static_cast<int>(k) < segments ? 0.0f : 1.0f;
                pose = slopengine::sampleCameraPathSegment(path, segment, u);
            } else {
                pose.position = knot.at;
                pose.yaw = knot.yaw;
                pose.pitch = knot.pitch;
                pose.fov = knot.fov > 0.0f ? knot.fov : path.fov;
            }
            pose.position = knot.at;
            DrawLine3D(
                knot.at,
                Vector3Add(knot.at, Vector3Scale(slopengine::cameraPoseForward(pose), 0.7f)),
                lookColor);
            if (sel || active) {
                const float fov = pose.fov > 0.0f ? pose.fov : 60.0f;
                drawFrustumWedge(pose, fov, sel ? 0.6f : 0.35f, wedgeColor);
            }
        }
    }

    if (previewPose.has_value()) {
        const float fov = previewPose->fov > 0.0f ? previewPose->fov : 60.0f;
        DrawSphereWires(previewPose->position, 0.12f, 8, 8, selectedColor);
        drawFrustumWedge(*previewPose, fov, 0.8f, selectedColor);
    }
}

bool pickKnot(
    const slopengine::GraphDocument& graphs,
    Ray ray,
    EntityRef& outRef,
    float& outDistance) {
    float best = std::numeric_limits<float>::max();
    bool found = false;
    for (std::size_t p = 0; p < graphs.paths.size(); ++p) {
        const slopengine::CameraPath& path = graphs.paths[p];
        for (std::size_t k = 0; k < path.knots.size(); ++k) {
            const RayCollision hit = GetRayCollisionSphere(ray, path.knots[k].at, kKnotPickRadius);
            if (hit.hit && hit.distance < best) {
                best = hit.distance;
                outRef = knotRef(static_cast<int>(p), static_cast<int>(k));
                found = true;
            }
        }
    }
    if (found) {
        outDistance = best;
    }
    return found;
}

}
