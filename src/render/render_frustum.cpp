#include "render/render_frustum.hpp"

#include <algorithm>
#include <cmath>

#include <rlgl.h>

namespace slopengine {

namespace {

FrustumPlane normalizePlane(Vector3 normal, float distance) {
    const float length = Vector3Length(normal);
    if (length < 1e-8f) {
        return {normal, distance};
    }
    const float inv = 1.0f / length;
    return {Vector3Scale(normal, inv), distance * inv};
}

float planeDistance(const FrustumPlane& plane, Vector3 point) {
    return Vector3DotProduct(plane.normal, point) + plane.distance;
}

} // namespace

Frustum makeFrustumFromCamera(const Camera3D& camera, float aspect) {
    const Matrix view = GetCameraMatrix(camera);
    const Matrix proj = camera.projection == CAMERA_ORTHOGRAPHIC
        ? MatrixOrtho(
              -camera.fovy * aspect * 0.5f,
              camera.fovy * aspect * 0.5f,
              -camera.fovy * 0.5f,
              camera.fovy * 0.5f,
              static_cast<float>(RL_CULL_DISTANCE_NEAR),
              static_cast<float>(RL_CULL_DISTANCE_FAR))
        : MatrixPerspective(
              camera.fovy * DEG2RAD,
              aspect,
              static_cast<float>(RL_CULL_DISTANCE_NEAR),
              static_cast<float>(RL_CULL_DISTANCE_FAR));
    const Matrix clip = MatrixMultiply(view, proj);

    Frustum frustum{};
    frustum.planes[0] = normalizePlane(
        {clip.m3 + clip.m0, clip.m7 + clip.m4, clip.m11 + clip.m8},
        clip.m15 + clip.m12);
    frustum.planes[1] = normalizePlane(
        {clip.m3 - clip.m0, clip.m7 - clip.m4, clip.m11 - clip.m8},
        clip.m15 - clip.m12);
    frustum.planes[2] = normalizePlane(
        {clip.m3 + clip.m1, clip.m7 + clip.m5, clip.m11 + clip.m9},
        clip.m15 + clip.m13);
    frustum.planes[3] = normalizePlane(
        {clip.m3 - clip.m1, clip.m7 - clip.m5, clip.m11 - clip.m9},
        clip.m15 - clip.m13);
    frustum.planes[4] = normalizePlane(
        {clip.m3 + clip.m2, clip.m7 + clip.m6, clip.m11 + clip.m10},
        clip.m15 + clip.m14);
    frustum.planes[5] = normalizePlane(
        {clip.m3 - clip.m2, clip.m7 - clip.m6, clip.m11 - clip.m10},
        clip.m15 - clip.m14);
    return frustum;
}

bool aabbInFrustum(const Frustum& frustum, BoundingBox box) {
    for (const FrustumPlane& plane : frustum.planes) {
        const Vector3 positive{
            plane.normal.x >= 0.0f ? box.max.x : box.min.x,
            plane.normal.y >= 0.0f ? box.max.y : box.min.y,
            plane.normal.z >= 0.0f ? box.max.z : box.min.z,
        };
        if (planeDistance(plane, positive) < 0.0f) {
            return false;
        }
    }
    return true;
}

bool sphereInFrustum(const Frustum& frustum, Vector3 center, float radius) {
    const float r = std::max(radius, 0.0f);
    for (const FrustumPlane& plane : frustum.planes) {
        if (planeDistance(plane, center) < -r) {
            return false;
        }
    }
    return true;
}

BoundingBox transformAabb(BoundingBox box, const Matrix& matrix) {
    const Vector3 corners[8] = {
        {box.min.x, box.min.y, box.min.z},
        {box.min.x, box.min.y, box.max.z},
        {box.min.x, box.max.y, box.min.z},
        {box.min.x, box.max.y, box.max.z},
        {box.max.x, box.min.y, box.min.z},
        {box.max.x, box.min.y, box.max.z},
        {box.max.x, box.max.y, box.min.z},
        {box.max.x, box.max.y, box.max.z},
    };

    Vector3 transformed = Vector3Transform(corners[0], matrix);
    BoundingBox result{transformed, transformed};
    for (int i = 1; i < 8; ++i) {
        transformed = Vector3Transform(corners[i], matrix);
        result.min.x = std::min(result.min.x, transformed.x);
        result.min.y = std::min(result.min.y, transformed.y);
        result.min.z = std::min(result.min.z, transformed.z);
        result.max.x = std::max(result.max.x, transformed.x);
        result.max.y = std::max(result.max.y, transformed.y);
        result.max.z = std::max(result.max.z, transformed.z);
    }
    return result;
}

}
