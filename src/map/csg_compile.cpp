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
    bool transparent = false;
    bool twoSided = false;
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
        primitive.transparent = face.transparent;
        primitive.twoSided = face.twoSided;
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
        if (chart != nullptr && chart->groupUMax > chart->groupUMin) {
            uMin = chart->groupUMin;
            uMax = chart->groupUMax;
            vMin = chart->groupVMin;
            vMax = chart->groupVMax;
        } else {
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
                const float fx = chart->rotated ? fv : fu;
                const float fy = chart->rotated ? fu : fv;
                lightUv.x = chart->u0 + (chart->u1 - chart->u0) * fx;
                lightUv.y = chart->v0 + (chart->v1 - chart->v0) * fy;
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
            input.transparent = brush.role == BrushRole::Transparent;
            input.twoSided = brush.role == BrushRole::Water;
            faces.push_back(std::move(input));
        }
    }
    return compileFacesToGeo(faces, resolveMaterialUv, lightmaps);
}

void mergeGeoPrimitivesByKey(GeoAsset& asset, VertBuffer& buffer, const PrimitiveKeyFn& keyOf) {
    constexpr std::size_t kMaxVertsPerPrimitive = 65535;

    std::vector<std::string> groupOrder;
    std::unordered_map<std::string, std::vector<std::size_t>> groups;
    groupOrder.reserve(asset.primitives.size());
    for (std::size_t i = 0; i < asset.primitives.size(); ++i) {
        std::string key = keyOf(asset.primitives[i]);
        auto [it, inserted] = groups.try_emplace(std::move(key));
        if (inserted) {
            groupOrder.push_back(it->first);
        }
        it->second.push_back(i);
    }

    GeoAsset mergedAsset;
    mergedAsset.skeletonId = asset.skeletonId;
    mergedAsset.verticesImplicit = asset.verticesImplicit;
    mergedAsset.weightsImplicit = asset.weightsImplicit;
    mergedAsset.primitives.reserve(asset.primitives.size());

    VertBuffer mergedBuffer;
    mergedBuffer.positions.reserve(buffer.positions.size());
    mergedBuffer.normals.reserve(buffer.normals.size());
    mergedBuffer.texcoords.reserve(buffer.texcoords.size());
    mergedBuffer.texcoords2.reserve(buffer.texcoords2.size());
    mergedBuffer.indices.reserve(buffer.indices.size());

    for (const std::string& key : groupOrder) {
        const std::vector<std::size_t>& members = groups[key];

        GeoPrimitive current;
        bool currentOpen = false;

        for (std::size_t memberIndex : members) {
            const GeoPrimitive& src = asset.primitives[memberIndex];

            if (currentOpen && current.vertexCount + src.vertexCount > kMaxVertsPerPrimitive) {
                mergedAsset.primitives.push_back(std::move(current));
                current = GeoPrimitive{};
                currentOpen = false;
            }

            if (!currentOpen) {
                current.name = src.name;
                current.material = src.material;
                current.rigidBone = src.rigidBone;
                current.transparent = src.transparent;
                current.twoSided = src.twoSided;
                current.vertexOffset = mergedBuffer.positions.size();
                current.indexOffset = mergedBuffer.indices.size();
                currentOpen = true;
            }

            const std::uint32_t destBase =
                static_cast<std::uint32_t>(mergedBuffer.positions.size() - current.vertexOffset);

            mergedBuffer.positions.insert(
                mergedBuffer.positions.end(),
                buffer.positions.begin() + static_cast<std::ptrdiff_t>(src.vertexOffset),
                buffer.positions.begin() + static_cast<std::ptrdiff_t>(src.vertexOffset + src.vertexCount));
            mergedBuffer.normals.insert(
                mergedBuffer.normals.end(),
                buffer.normals.begin() + static_cast<std::ptrdiff_t>(src.vertexOffset),
                buffer.normals.begin() + static_cast<std::ptrdiff_t>(src.vertexOffset + src.vertexCount));
            mergedBuffer.texcoords.insert(
                mergedBuffer.texcoords.end(),
                buffer.texcoords.begin() + static_cast<std::ptrdiff_t>(src.vertexOffset),
                buffer.texcoords.begin() + static_cast<std::ptrdiff_t>(src.vertexOffset + src.vertexCount));
            mergedBuffer.texcoords2.insert(
                mergedBuffer.texcoords2.end(),
                buffer.texcoords2.begin() + static_cast<std::ptrdiff_t>(src.vertexOffset),
                buffer.texcoords2.begin() + static_cast<std::ptrdiff_t>(src.vertexOffset + src.vertexCount));

            for (std::size_t i = 0; i < src.indexCount; ++i) {
                const std::uint32_t srcIndex = buffer.indices[src.indexOffset + i];
                const std::uint32_t localIndex = srcIndex - static_cast<std::uint32_t>(src.vertexOffset);
                mergedBuffer.indices.push_back(
                    static_cast<std::uint32_t>(current.vertexOffset) + destBase + localIndex);
            }

            current.vertexCount += src.vertexCount;
            current.indexCount += src.indexCount;
        }

        if (currentOpen) {
            mergedAsset.primitives.push_back(std::move(current));
        }
    }

    asset = std::move(mergedAsset);
    buffer = std::move(mergedBuffer);
}

}
