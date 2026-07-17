#include "map/csg_compile.hpp"

#include "map/uv_math.hpp"

#include <algorithm>
#include <unordered_map>

namespace slopengine {

CsgCompileResult compileBrushesToGeo(
    const std::vector<Brush>& brushes,
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

    for (const Brush& brush : brushes) {
        for (const BrushFace& face : brush.faces) {
            if (face.nodraw || face.vertices.size() < 3) {
                continue;
            }
            GeoPrimitive primitive;
            primitive.name = face.id;
            primitive.material = face.material;
            primitive.vertexOffset = result.buffer.positions.size();
            primitive.vertexCount = face.vertices.size();
            primitive.indexOffset = result.buffer.indices.size();
            primitive.indexCount = (face.vertices.size() - 2) * 3;

            MaterialUvInfo materialUv{};
            if (resolveMaterialUv) {
                materialUv = resolveMaterialUv(face.material);
            }

            Vector3 uAxis{};
            Vector3 vAxis{};
            axialUvAxes(face.normal, uAxis, vAxis);

            const LightmapChart* chart = nullptr;
            const auto chartIt = chartByFaceId.find(face.id);
            if (chartIt != chartByFaceId.end()) {
                chart = chartIt->second;
            }

            float uMin = 0.0f;
            float uMax = 0.0f;
            float vMin = 0.0f;
            float vMax = 0.0f;
            for (std::size_t i = 0; i < face.vertices.size(); ++i) {
                const float u =
                    face.vertices[i].x * uAxis.x + face.vertices[i].y * uAxis.y + face.vertices[i].z * uAxis.z;
                const float v =
                    face.vertices[i].x * vAxis.x + face.vertices[i].y * vAxis.y + face.vertices[i].z * vAxis.z;
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

            for (const Vector3& corner : face.vertices) {
                result.buffer.positions.push_back(corner);
                result.buffer.normals.push_back(face.normal);
                result.buffer.texcoords.push_back(
                    worldPlanarUv(corner, uAxis, vAxis, face.uvShiftPixels, materialUv));

                Vector2 lightUv{0.0f, 0.0f};
                if (chart != nullptr) {
                    const float u =
                        corner.x * uAxis.x + corner.y * uAxis.y + corner.z * uAxis.z;
                    const float v =
                        corner.x * vAxis.x + corner.y * vAxis.y + corner.z * vAxis.z;
                    const float fu = (u - uMin) / uSpan;
                    const float fv = (v - vMin) / vSpan;
                    lightUv.x = chart->u0 + (chart->u1 - chart->u0) * fu;
                    lightUv.y = chart->v0 + (chart->v1 - chart->v0) * fv;
                }
                result.buffer.texcoords2.push_back(lightUv);
            }

            const std::uint32_t base = static_cast<std::uint32_t>(primitive.vertexOffset);
            for (std::uint32_t i = 1; i + 1 < static_cast<std::uint32_t>(face.vertices.size()); ++i) {
                result.buffer.indices.push_back(base);
                result.buffer.indices.push_back(base + i);
                result.buffer.indices.push_back(base + i + 1);
            }

            result.asset.primitives.push_back(std::move(primitive));
        }
    }

    result.asset.verticesImplicit = false;
    return result;
}

}
