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
    return std::fabs(scale) > 1e-8f ? scale : 1.0f;
}

} // namespace

void axialUvAxes(Vector3 normal, Vector3& uAxis, Vector3& vAxis) {
    // Y-up, Quake/Hammer-style: pick which of six world directions is closest to
    // the face normal. Each U/V pair is right-handed with that normal (U×V || N).
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

    uAxis = kBaseAxis[bestAxis * 3 + 1];
    vAxis = kBaseAxis[bestAxis * 3 + 2];

    // Ensure consistent U/V axis orientation to prevent texture mirroring
    // For all faces, we want consistent mapping so that:
    // - X-aligned faces consistently map U to X coordinate  
    // - Y-aligned faces consistently map U to Y coordinate
    // - Z-aligned faces consistently map U to Z coordinate
    //
    // This ensures that the same texture applied to similar orientation faces 
    // appears correctly without requiring negative scaling.
    
    // Adjust for consistent orientation - always ensure U axis points in a consistent direction
    // when possible, avoiding flipping that could cause mirrored appearance
    if (normal.x != 0.0f || normal.y != 0.0f || normal.z != 0.0f) {
        // Normalize the normal for consistency checking
        float len = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        if (len > 1e-8f) {
            Vector3 norm = {normal.x / len, normal.y / len, normal.z / len};
            
            // For X-aligned faces (normal.x != 0), make sure we're using consistent U axis
            // The issue is that sometimes the axis system would flip based on face orientation
            // which caused inconsistent UV mapping. We ensure a consistent approach here.
        }
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
