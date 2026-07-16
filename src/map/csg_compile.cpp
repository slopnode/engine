#include "map/csg_compile.hpp"

#include <cmath>

namespace slopengine {

namespace {

float vec3Dot(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

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

Vector2 worldPlanarUv(
    Vector3 position,
    Vector3 uAxis,
    Vector3 vAxis,
    Vector2 uvShiftPixels,
    const MaterialUvInfo& materialUv) {
    const float metersU = vec3Dot(position, uAxis);
    const float metersV = vec3Dot(position, vAxis);
    const float width = materialUv.textureWidth > 0.0f ? materialUv.textureWidth : 64.0f;
    const float height = materialUv.textureHeight > 0.0f ? materialUv.textureHeight : 64.0f;
    return Vector2{
        (metersU * materialUv.pixelsPerMeter + uvShiftPixels.x) / width,
        (metersV * materialUv.pixelsPerMeter + uvShiftPixels.y) / height,
    };
}

} // namespace

CsgCompileResult compileBrushesToGeo(
    const std::vector<Brush>& brushes,
    const MaterialUvResolver& resolveMaterialUv) {
    CsgCompileResult result;

    for (const Brush& brush : brushes) {
        for (const BrushFace& face : brush.faces) {
            GeoPrimitive primitive;
            primitive.name = face.id;
            primitive.material = face.material;
            primitive.vertexOffset = result.buffer.positions.size();
            primitive.vertexCount = 4;
            primitive.indexOffset = result.buffer.indices.size();
            primitive.indexCount = 6;

            MaterialUvInfo materialUv{};
            if (resolveMaterialUv) {
                materialUv = resolveMaterialUv(face.material);
            }

            Vector3 uAxis{};
            Vector3 vAxis{};
            axialUvAxes(face.normal, uAxis, vAxis);

            for (const Vector3& corner : face.corners) {
                result.buffer.positions.push_back(corner);
                result.buffer.normals.push_back(face.normal);
                result.buffer.texcoords.push_back(
                    worldPlanarUv(corner, uAxis, vAxis, face.uvShiftPixels, materialUv));
            }

            const std::uint32_t base = static_cast<std::uint32_t>(primitive.vertexOffset);
            result.buffer.indices.push_back(base + 0);
            result.buffer.indices.push_back(base + 1);
            result.buffer.indices.push_back(base + 2);
            result.buffer.indices.push_back(base + 0);
            result.buffer.indices.push_back(base + 2);
            result.buffer.indices.push_back(base + 3);

            result.asset.primitives.push_back(std::move(primitive));
        }
    }

    result.asset.verticesImplicit = false;
    return result;
}

}
