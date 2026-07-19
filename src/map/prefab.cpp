#include "map/prefab.hpp"

#include "map/uv_math.hpp"

#include <raymath.h>

#include <cmath>
#include <string>

namespace slopengine {

namespace {

Vector3 normalizeAxis(Vector3 v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-8f) {
        return {};
    }
    return {v.x / len, v.y / len, v.z / len};
}

} // namespace

void remapBrushIds(Brush& brush, std::string_view instanceId) {
    brush.id = std::string(instanceId) + "/" + brush.id;
    for (BrushFace& face : brush.faces) {
        if (!face.id.empty()) {
            face.id = std::string(instanceId) + "/" + face.id;
        }
    }
}

void transformBrush(
    Brush& brush,
    Vector3 at,
    Vector3 anglesPitchYawRoll,
    const MaterialUvResolver& resolveMaterialUv) {
    const Matrix rotation = MatrixRotateXYZ(anglesPitchYawRoll);
    for (BrushFace& face : brush.faces) {
        const Vector3 oldRef = face.vertices.empty() ? Vector3{} : face.vertices[0];
        Vector3 oldU{};
        Vector3 oldV{};
        if (face.uvLock) {
            ensureFaceUvAxes(face);
            oldU = face.uvUAxis;
            oldV = face.uvVAxis;
        }
        for (Vector3& vertex : face.vertices) {
            vertex = Vector3Add(Vector3Transform(vertex, rotation), at);
        }
        face.normal = faceNormalFromVertices(face.vertices);
        if (face.uvLock) {
            face.uvUAxis = normalizeAxis(Vector3Transform(oldU, rotation));
            face.uvVAxis = normalizeAxis(Vector3Transform(oldV, rotation));
            float ppm = 64.0f;
            if (resolveMaterialUv) {
                ppm = resolveMaterialUv(face.material).pixelsPerMeter;
            }
            lockFaceUvShift(face, oldRef, oldU, oldV, ppm);
        }
    }
    recomputeBrushBounds(brush);
    brush.box = false;
}

}
