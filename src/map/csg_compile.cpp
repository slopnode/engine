#include "map/csg_compile.hpp"

#include "map/brush.hpp"
#include "map/uv_math.hpp"

#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

namespace slopengine {

namespace {

struct FaceCompileInput {
    std::string id;
    std::string material;
    Vector3 normal{};
    const std::vector<Vector3>* vertices = nullptr;
    Vector2 uvShiftPixels{};
    Vector2 uvScale{1.0f, 1.0f};
    Vector3 uvUAxis{};
    Vector3 uvVAxis{};
    bool uvLock = false;
};

CsgCompileResult compileFacesToGeo(
    const std::vector<FaceCompileInput>& faces,
    const MaterialUvResolver& resolveMaterialUv,
    const RadFile* lightmaps) {
    CsgCompileResult result;

    std::unordered_map<std::string, const LightmapChart*> chartByFaceId;
    if (lightmaps != nullptr) {
        for (const LightmapChart& chart : lightmaps->charts) {
            if (!chart.faceId.empty()) {
                chartByFaceId[chart.faceId] = &chart;
            }
        }
    }

    int missingChartCount = 0;
    std::string missingChartSample;

    for (const FaceCompileInput& face : faces) {
        if (face.vertices == nullptr || face.vertices->size() < 3) {
            continue;
        }
        const std::vector<Vector3>& verts = *face.vertices;

        GeoPrimitive primitive;
        primitive.name = face.id;
        primitive.material = face.material;
        primitive.vertexOffset = result.buffer.positions.size();
        primitive.vertexCount = verts.size();
        primitive.indexOffset = result.buffer.indices.size();
        primitive.indexCount = 0;

        MaterialUvInfo materialUv{};
        if (resolveMaterialUv) {
            materialUv = resolveMaterialUv(face.material);
        }

        Vector3 uAxis{};
        Vector3 vAxis{};
        faceUvAxes(face.uvLock, face.normal, face.uvUAxis, face.uvVAxis, uAxis, vAxis);

        const LightmapChart* chart = nullptr;
        const auto chartIt = chartByFaceId.find(face.id);
        if (chartIt != chartByFaceId.end()) {
            chart = chartIt->second;
        } else if (lightmaps != nullptr) {
            ++missingChartCount;
            if (missingChartSample.empty()) {
                missingChartSample = face.id;
            }
        }

        float uMin = 0.0f;
        float uMax = 0.0f;
        float vMin = 0.0f;
        float vMax = 0.0f;
        for (std::size_t i = 0; i < verts.size(); ++i) {
            const float u = verts[i].x * uAxis.x + verts[i].y * uAxis.y + verts[i].z * uAxis.z;
            const float v = verts[i].x * vAxis.x + verts[i].y * vAxis.y + verts[i].z * vAxis.z;
            if (i == 0) {
                uMin = uMax = u;
                vMin = vMax = v;
            } else {
                uMin = std::min(uMin, u);
                uMax = std::max(uMax, u);
                vMin = std::min(vMin, v);
                vMax = std::max(vMax, v);
            }
        }
        const float uSpan = uMax - uMin > 1e-5f ? uMax - uMin : 1.0f;
        const float vSpan = vMax - vMin > 1e-5f ? vMax - vMin : 1.0f;

        for (const Vector3& corner : verts) {
            result.buffer.positions.push_back(corner);
            result.buffer.normals.push_back(face.normal);
            result.buffer.texcoords.push_back(
                worldPlanarUv(corner, uAxis, vAxis, face.uvShiftPixels, face.uvScale, materialUv));

            Vector2 lightUv{0.0f, 0.0f};
            if (chart != nullptr) {
                const float u = corner.x * uAxis.x + corner.y * uAxis.y + corner.z * uAxis.z;
                const float v = corner.x * vAxis.x + corner.y * vAxis.y + corner.z * vAxis.z;
                const float fu = (u - uMin) / uSpan;
                const float fv = (v - vMin) / vSpan;
                lightUv.x = chart->u0 + (chart->u1 - chart->u0) * fu;
                lightUv.y = chart->v0 + (chart->v1 - chart->v0) * fv;
            }
            result.buffer.texcoords2.push_back(lightUv);
        }

        const std::uint32_t base = static_cast<std::uint32_t>(primitive.vertexOffset);
        auto findVertIndex = [&](Vector3 p) -> std::int32_t {
            for (std::size_t i = 0; i < verts.size(); ++i) {
                const Vector3 d{
                    verts[i].x - p.x,
                    verts[i].y - p.y,
                    verts[i].z - p.z,
                };
                if (d.x * d.x + d.y * d.y + d.z * d.z <= 1e-8f) {
                    return static_cast<std::int32_t>(i);
                }
            }
            return -1;
        };

        const auto tris = triangulateFace(verts);
        std::size_t indexCount = 0;
        for (const auto& tri : tris) {
            const Vector3 e1{
                tri[1].x - tri[0].x,
                tri[1].y - tri[0].y,
                tri[1].z - tri[0].z,
            };
            const Vector3 e2{
                tri[2].x - tri[0].x,
                tri[2].y - tri[0].y,
                tri[2].z - tri[0].z,
            };
            const Vector3 tn{
                e1.y * e2.z - e1.z * e2.y,
                e1.z * e2.x - e1.x * e2.z,
                e1.x * e2.y - e1.y * e2.x,
            };
            const float align =
                tn.x * face.normal.x + tn.y * face.normal.y + tn.z * face.normal.z;
            if (align <= 1e-8f) {
                continue;
            }
            const std::int32_t i0 = findVertIndex(tri[0]);
            const std::int32_t i1 = findVertIndex(tri[1]);
            const std::int32_t i2 = findVertIndex(tri[2]);
            if (i0 < 0 || i1 < 0 || i2 < 0) {
                continue;
            }
            result.buffer.indices.push_back(base + static_cast<std::uint32_t>(i0));
            result.buffer.indices.push_back(base + static_cast<std::uint32_t>(i1));
            result.buffer.indices.push_back(base + static_cast<std::uint32_t>(i2));
            indexCount += 3;
        }
        if (indexCount == 0) {
            result.buffer.positions.resize(static_cast<std::size_t>(primitive.vertexOffset));
            result.buffer.normals.resize(static_cast<std::size_t>(primitive.vertexOffset));
            result.buffer.texcoords.resize(static_cast<std::size_t>(primitive.vertexOffset));
            result.buffer.texcoords2.resize(static_cast<std::size_t>(primitive.vertexOffset));
            continue;
        }
        primitive.indexCount = indexCount;

        result.asset.primitives.push_back(std::move(primitive));
    }

    if (missingChartCount > 0) {
        TraceLog(
            LOG_WARNING,
            "MAP: %d drawn face(s) missing lightmap charts (e.g. '%s'); rebake with sloprad",
            missingChartCount,
            missingChartSample.c_str());
    }

    result.asset.verticesImplicit = false;
    return result;
}

} // namespace

CsgCompileResult compileBrushesToGeo(
    const std::vector<Brush>& brushes,
    const MaterialUvResolver& resolveMaterialUv,
    const RadFile* lightmaps) {
    std::vector<FaceCompileInput> faces;
    for (const Brush& brush : brushes) {
        for (const BrushFace& face : brush.faces) {
            if (face.nodraw || face.vertices.size() < 3) {
                continue;
            }
            FaceCompileInput input;
            input.id = face.id;
            input.material = face.material;
            input.normal = face.normal;
            input.vertices = &face.vertices;
            input.uvShiftPixels = face.uvShiftPixels;
            input.uvScale = face.uvScale;
            input.uvUAxis = face.uvUAxis;
            input.uvVAxis = face.uvVAxis;
            input.uvLock = face.uvLock;
            faces.push_back(std::move(input));
        }
    }
    return compileFacesToGeo(faces, resolveMaterialUv, lightmaps);
}

CsgCompileResult compileVisibleFacesToGeo(
    const VisFile& vis,
    const MaterialUvResolver& resolveMaterialUv,
    const RadFile* lightmaps) {
    std::vector<FaceCompileInput> faces;
    faces.reserve(vis.faces.size());
    for (const VisibleFace& face : vis.faces) {
        if (face.vertices.size() < 3) {
            continue;
        }
        FaceCompileInput input;
        input.id = face.id;
        input.material = face.material;
        input.normal = face.normal;
        input.vertices = &face.vertices;
        input.uvShiftPixels = face.uvShiftPixels;
        input.uvScale = face.uvScale;
        input.uvUAxis = face.uvUAxis;
        input.uvVAxis = face.uvVAxis;
        input.uvLock = face.uvLock;
        faces.push_back(std::move(input));
    }
    return compileFacesToGeo(faces, resolveMaterialUv, lightmaps);
}

}
