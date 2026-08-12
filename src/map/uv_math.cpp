#include "map/uv_math.hpp"

#include <raymath.h>

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

bool isZeroAxis(Vector3 v) {
    return v.x == 0.0f && v.y == 0.0f && v.z == 0.0f;
}

bool axesAreZero(Vector3 u, Vector3 v) {
    return isZeroAxis(u) || isZeroAxis(v);
}

float axisScale(float scale) {
    return std::fabs(scale) > 1e-8f ? scale : 1.0f;
}

Vector3 orthogonalizeAgainstNormal(Vector3 normal, Vector3 templateU, Vector3 templateV, Vector3& uAxis, Vector3& vAxis) {
    const Vector3 n = normalizeAxis(normal);
    if (isZeroAxis(n)) {
        uAxis = templateU;
        vAxis = templateV;
        return n;
    }

    const float dotU = templateU.x * n.x + templateU.y * n.y + templateU.z * n.z;
    const Vector3 u = normalizeAxis({
        templateU.x - n.x * dotU,
        templateU.y - n.y * dotU,
        templateU.z - n.z * dotU,
    });
    if (isZeroAxis(u)) {
        uAxis = templateU;
        vAxis = templateV;
        return n;
    }

    uAxis = u;
    vAxis = Vector3CrossProduct(u, n);
    return n;
}

} // namespace

void axialUvAxes(Vector3 normal, Vector3& uAxis, Vector3& vAxis) {
    /*
    static constexpr Vector3 kBaseAxis[18] = {
        {0.0f, 1.0f, 0.0f},  {1.0f, 0.0f, 0.0f},  {0.0f, 0.0f, -1.0f}, // +Y floor
        {0.0f, -1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, // -Y ceiling
        {1.0f, 0.0f, 0.0f},  {0.0f, 0.0f, 1.0f},  {0.0f, -1.0f, 0.0f}, // +X east
        {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, -1.0f, 0.0f}, // -X west
        {0.0f, 0.0f, 1.0f},  {-1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, // +Z north
        {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f},  {0.0f, -1.0f, 0.0f}, // -Z south
    };
    */
    static constexpr Vector3 kBaseAxis[18] = {
    {0.0f, 1.0f, 0.0f},  {-1.0f, 0.0f, 0.0f},  {0.0f, 0.0f, -1.0f}, // +Y floor (flipped X)
    {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f},   {0.0f, 0.0f, -1.0f}, // -Y ceiling (flipped X)
    {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f},  {0.0f, -1.0f, 0.0f}, // +X east (flipped X)
    {1.0f, 0.0f, 0.0f},  {0.0f, 0.0f, -1.0f}, {0.0f, -1.0f, 0.0f}, // -X west (flipped X)
    {0.0f, 0.0f, 1.0f},  {1.0f, 0.0f, 0.0f},  {0.0f, -1.0f, 0.0f}, // +Z north (flipped X)
    {0.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, // -Z south (flipped X)
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

    const Vector3 templateU = kBaseAxis[bestAxis * 3 + 1];
    const Vector3 templateV = kBaseAxis[bestAxis * 3 + 2];
    orthogonalizeAgainstNormal(normal, templateU, templateV, uAxis, vAxis);
}

void faceUvAxes(
    bool uvLock,
    Vector3 normal,
    Vector3 storedUAxis,
    Vector3 storedVAxis,
    Vector3& uAxis,
    Vector3& vAxis) {
    if (uvLock && !axesAreZero(storedUAxis, storedVAxis)) {
        orthogonalizeAgainstNormal(normal, storedUAxis, storedVAxis, uAxis, vAxis);
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
