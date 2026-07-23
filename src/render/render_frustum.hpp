#pragma once

#include <raylib.h>
#include <raymath.h>

namespace slopengine {

struct FrustumPlane {
    Vector3 normal{};
    float distance = 0.0f;
};

struct Frustum {
    FrustumPlane planes[6]{};
};

Frustum makeFrustumFromCamera(const Camera3D& camera, float aspect);
bool aabbInFrustum(const Frustum& frustum, BoundingBox box);
bool sphereInFrustum(const Frustum& frustum, Vector3 center, float radius);
BoundingBox transformAabb(BoundingBox box, const Matrix& matrix);

}
