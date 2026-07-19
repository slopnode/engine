#include "map/uv_math.hpp"

#include <cmath>

namespace slopengine {

namespace {

Vector3 normalizeAxis(Vector3 v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-8f) {
        return {};
    }
    return {v.x / len, v.y / len, v.z / len};
}

bool axesAreZero(Vector3 u, Vector3 v) {
    return (u.x == 0.0f && u.y == 0.0f && u.z == 0.0f) ||
        (v.x == 0.0f && v.y == 0.0f && v.z == 0.0f);
}

} // namespace

void axialUvAxes(Vector3 normal, Vector3& uAxis, Vector3& vAxis) {
    const float ax = std::fabs(normal.x);
    const float ay = std::fabs(normal.y);
    const float az = std::fabs(normal.z);

    if (ay >= ax && ay >= az) {
        uAxis = {1.0f, 0.0f, 0.0f};
        vAxis = {0.0f, 0.0f, -1.0f};
    } else if (ax >= ay && ax >= az) {
        uAxis = {0.0f, 0.0f, 1.0f};
        vAxis = {0.0f, -1.0f, 0.0f};
    } else {
        uAxis = {1.0f, 0.0f, 0.0f};
        vAxis = {0.0f, -1.0f, 0.0f};
    }
}

void faceUvAxes(
    bool uvLock,
    Vector3 normal,
    Vector3 storedUAxis,
    Vector3 storedVAxis,
    Vector3& uAxis,
    Vector3& vAxis) {
    if (uvLock && !axesAreZero(storedUAxis, storedVAxis)) {
        uAxis = storedUAxis;
        vAxis = storedVAxis;
        return;
    }
    axialUvAxes(normal, uAxis, vAxis);
}

void faceUvAxes(const BrushFace& face, Vector3& uAxis, Vector3& vAxis) {
    faceUvAxes(face.uvLock, face.normal, face.uvUAxis, face.uvVAxis, uAxis, vAxis);
}

void ensureFaceUvAxes(BrushFace& face) {
    if (!face.uvLock) {
        return;
    }
    if (!axesAreZero(face.uvUAxis, face.uvVAxis)) {
        face.uvUAxis = normalizeAxis(face.uvUAxis);
        face.uvVAxis = normalizeAxis(face.uvVAxis);
        return;
    }
    axialUvAxes(face.normal, face.uvUAxis, face.uvVAxis);
}

Vector2 worldPlanarUv(
    Vector3 position,
    Vector3 uAxis,
    Vector3 vAxis,
    Vector2 uvShiftPixels,
    const MaterialUvInfo& materialUv) {
    const float metersU = position.x * uAxis.x + position.y * uAxis.y + position.z * uAxis.z;
    const float metersV = position.x * vAxis.x + position.y * vAxis.y + position.z * vAxis.z;
    const float width = materialUv.textureWidth > 0.0f ? materialUv.textureWidth : 64.0f;
    const float height = materialUv.textureHeight > 0.0f ? materialUv.textureHeight : 64.0f;
    return Vector2{
        (metersU * materialUv.pixelsPerMeter + uvShiftPixels.x) / width,
        (metersV * materialUv.pixelsPerMeter + uvShiftPixels.y) / height,
    };
}

void lockFaceUvShift(
    BrushFace& face,
    Vector3 oldRef,
    Vector3 oldUAxis,
    Vector3 oldVAxis,
    float pixelsPerMeter) {
    if (!face.uvLock || face.vertices.empty()) {
        return;
    }
    const float ppm = pixelsPerMeter > 0.0f ? pixelsPerMeter : 64.0f;
    Vector3 newU{};
    Vector3 newV{};
    faceUvAxes(face, newU, newV);
    const Vector3& newRef = face.vertices[0];
    const float oldMetersU = oldRef.x * oldUAxis.x + oldRef.y * oldUAxis.y + oldRef.z * oldUAxis.z;
    const float oldMetersV = oldRef.x * oldVAxis.x + oldRef.y * oldVAxis.y + oldRef.z * oldVAxis.z;
    const float newMetersU = newRef.x * newU.x + newRef.y * newU.y + newRef.z * newU.z;
    const float newMetersV = newRef.x * newV.x + newRef.y * newV.y + newRef.z * newV.z;
    face.uvShiftPixels.x += (oldMetersU - newMetersU) * ppm;
    face.uvShiftPixels.y += (oldMetersV - newMetersV) * ppm;
}

}
