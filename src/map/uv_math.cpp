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

float axisScale(float scale) {
    return scale > 1e-8f ? scale : 1.0f;
}

} // namespace

void axialUvAxes(Vector3 normal, Vector3& uAxis, Vector3& vAxis) {
    // Y-up, Quake/Hammer-style: pick which of six world directions is closest to
    // the face normal. Each U/V pair is right-handed with that normal (U×V || N).
    static constexpr Vector3 kBaseAxis[18] = {
        {0.0f, 1.0f, 0.0f},  {1.0f, 0.0f, 0.0f},  {0.0f, 0.0f, -1.0f}, // +Y floor
        {0.0f, -1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, // -Y ceiling
        {1.0f, 0.0f, 0.0f},  {0.0f, 0.0f, 1.0f},  {0.0f, -1.0f, 0.0f}, // +X east
        {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, -1.0f, 0.0f}, // -X west
        {0.0f, 0.0f, 1.0f},  {-1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, // +Z north
        {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f},  {0.0f, -1.0f, 0.0f}, // -Z south
    };

    int bestAxis = 0;
    float bestDot = -1.0f;
    for (int i = 0; i < 6; ++i) {
        const Vector3& axisN = kBaseAxis[i * 3];
        const float d = normal.x * axisN.x + normal.y * axisN.y + normal.z * axisN.z;
        if (d > bestDot) {
            bestDot = d;
            bestAxis = i;
        }
    }

    uAxis = kBaseAxis[bestAxis * 3 + 1];
    vAxis = kBaseAxis[bestAxis * 3 + 2];

    const Vector3 crossed = {
        uAxis.y * vAxis.z - uAxis.z * vAxis.y,
        uAxis.z * vAxis.x - uAxis.x * vAxis.z,
        uAxis.x * vAxis.y - uAxis.y * vAxis.x,
    };
    if (crossed.x * normal.x + crossed.y * normal.y + crossed.z * normal.z < 0.0f) {
        uAxis.x = -uAxis.x;
        uAxis.y = -uAxis.y;
        uAxis.z = -uAxis.z;
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
    Vector2 uvScale,
    const MaterialUvInfo& materialUv) {
    const float metersU = position.x * uAxis.x + position.y * uAxis.y + position.z * uAxis.z;
    const float metersV = position.x * vAxis.x + position.y * vAxis.y + position.z * vAxis.z;
    const float width = materialUv.textureWidth > 0.0f ? materialUv.textureWidth : 64.0f;
    const float height = materialUv.textureHeight > 0.0f ? materialUv.textureHeight : 64.0f;
    const float ppm = materialUv.pixelsPerMeter > 0.0f ? materialUv.pixelsPerMeter : 64.0f;
    return Vector2{
        (metersU * ppm * axisScale(uvScale.x) + uvShiftPixels.x) / width,
        (metersV * ppm * axisScale(uvScale.y) + uvShiftPixels.y) / height,
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
    face.uvShiftPixels.x += (oldMetersU - newMetersU) * ppm * axisScale(face.uvScale.x);
    face.uvShiftPixels.y += (oldMetersV - newMetersV) * ppm * axisScale(face.uvScale.y);
}

}
