#include "render/skin_cut.hpp"

#include "assets/skeleton_loader.hpp"

#include <raymath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_map>
#include <utility>

namespace slopengine {

namespace {

using Influences = std::vector<std::pair<int, float>>;

struct CutVertex {
    Vector3 position{0.0f, 0.0f, 0.0f};
    Vector2 uv{0.0f, 0.0f};
    Vector2 uv2{0.0f, 0.0f};
    Vector3 normal{0.0f, 1.0f, 0.0f};
    Vector4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
    std::array<unsigned char, 4> color{255, 255, 255, 255};
    std::array<unsigned char, 4> bones{0, 0, 0, 0};
    std::array<float, 4> weights{0.0f, 0.0f, 0.0f, 0.0f};
};

struct MeshBuilder {
    bool hasUv2 = false;
    bool hasTangents = false;
    bool hasColors = false;
    std::vector<CutVertex> vertices;
    std::vector<std::uint64_t> weld;
    std::vector<int> indices;
};

constexpr std::uint64_t kCornerWeld = 0xffffffffull;

std::uint64_t cornerWeld(int group) {
    return (static_cast<std::uint64_t>(group) << 32) | kCornerWeld;
}

std::uint64_t edgeWeld(int a, int b) {
    return (static_cast<std::uint64_t>(std::min(a, b)) << 32) | static_cast<std::uint32_t>(std::max(a, b));
}

std::vector<int> weldGroups(const Mesh& mesh) {
    std::map<std::array<float, 3>, int> groups;
    std::vector<int> out(static_cast<std::size_t>(mesh.vertexCount), 0);
    for (int v = 0; v < mesh.vertexCount; ++v) {
        const std::array<float, 3> key{mesh.vertices[v * 3], mesh.vertices[v * 3 + 1], mesh.vertices[v * 3 + 2]};
        out[static_cast<std::size_t>(v)] = groups.emplace(key, static_cast<int>(groups.size())).first->second;
    }
    return out;
}

struct Side {
    const std::vector<std::uint8_t>& inside;
    bool keepInside;

    bool isInside(int bone) const {
        return bone >= 0 && static_cast<std::size_t>(bone) < inside.size() &&
            inside[static_cast<std::size_t>(bone)] != 0;
    }

    bool keeps(int bone) const {
        return isInside(bone) == keepInside;
    }
};

bool skinnedMesh(const Mesh& mesh) {
    return mesh.boneWeights != nullptr && mesh.boneIndices != nullptr && mesh.vertices != nullptr;
}

Influences influencesOf(const Mesh& mesh, int vertex) {
    Influences out;
    for (int k = 0; k < 4; ++k) {
        const float weight = mesh.boneWeights[vertex * 4 + k];
        if (weight > 0.0f) {
            out.emplace_back(mesh.boneIndices[vertex * 4 + k], weight);
        }
    }
    return out;
}

void addInfluence(Influences& influences, int bone, float weight) {
    for (auto& [existing, value] : influences) {
        if (existing == bone) {
            value += weight;
            return;
        }
    }
    influences.emplace_back(bone, weight);
}

void assignWeights(CutVertex& vertex, const Influences& influences, const Side& side, int fallbackBone) {
    Influences kept;
    for (const auto& [bone, weight] : influences) {
        if (weight > 0.0f && side.keeps(bone)) {
            kept.emplace_back(bone, weight);
        }
    }
    std::sort(kept.begin(), kept.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (kept.size() > 4) {
        kept.resize(4);
    }
    float sum = 0.0f;
    for (const auto& entry : kept) {
        sum += entry.second;
    }
    vertex.bones = {0, 0, 0, 0};
    vertex.weights = {0.0f, 0.0f, 0.0f, 0.0f};
    if (sum <= 1e-6f) {
        vertex.bones[0] = static_cast<unsigned char>(std::clamp(fallbackBone, 0, 255));
        vertex.weights[0] = 1.0f;
        return;
    }
    for (std::size_t k = 0; k < kept.size(); ++k) {
        vertex.bones[k] = static_cast<unsigned char>(kept[k].first);
        vertex.weights[k] = kept[k].second / sum;
    }
}

CutVertex readVertex(const Mesh& mesh, int index) {
    CutVertex vertex;
    vertex.position = {mesh.vertices[index * 3], mesh.vertices[index * 3 + 1], mesh.vertices[index * 3 + 2]};
    if (mesh.texcoords != nullptr) {
        vertex.uv = {mesh.texcoords[index * 2], mesh.texcoords[index * 2 + 1]};
    }
    if (mesh.texcoords2 != nullptr) {
        vertex.uv2 = {mesh.texcoords2[index * 2], mesh.texcoords2[index * 2 + 1]};
    }
    if (mesh.normals != nullptr) {
        vertex.normal = {mesh.normals[index * 3], mesh.normals[index * 3 + 1], mesh.normals[index * 3 + 2]};
    }
    if (mesh.tangents != nullptr) {
        vertex.tangent = {
            mesh.tangents[index * 4],
            mesh.tangents[index * 4 + 1],
            mesh.tangents[index * 4 + 2],
            mesh.tangents[index * 4 + 3],
        };
    }
    if (mesh.colors != nullptr) {
        std::memcpy(vertex.color.data(), mesh.colors + index * 4, 4);
    }
    return vertex;
}

CutVertex lerpVertex(const CutVertex& a, const CutVertex& b, float t) {
    CutVertex out = a;
    out.position = Vector3Lerp(a.position, b.position, t);
    out.uv = Vector2Lerp(a.uv, b.uv, t);
    out.uv2 = Vector2Lerp(a.uv2, b.uv2, t);
    const Vector3 normal = Vector3Lerp(a.normal, b.normal, t);
    out.normal = Vector3LengthSqr(normal) > 1e-12f ? Vector3Normalize(normal) : a.normal;
    const Vector3 tangent = Vector3Lerp(
        Vector3{a.tangent.x, a.tangent.y, a.tangent.z}, Vector3{b.tangent.x, b.tangent.y, b.tangent.z}, t);
    if (Vector3LengthSqr(tangent) > 1e-12f) {
        const Vector3 unit = Vector3Normalize(tangent);
        out.tangent = {unit.x, unit.y, unit.z, a.tangent.w};
    }
    for (int k = 0; k < 4; ++k) {
        out.color[static_cast<std::size_t>(k)] = static_cast<unsigned char>(std::lround(
            a.color[static_cast<std::size_t>(k)] +
            (b.color[static_cast<std::size_t>(k)] - a.color[static_cast<std::size_t>(k)]) * t));
    }
    return out;
}

Mesh buildMesh(const MeshBuilder& builder, int boneCount) {
    Mesh mesh{};
    const int count = static_cast<int>(builder.vertices.size());
    mesh.vertexCount = count;
    mesh.triangleCount = static_cast<int>(builder.indices.size() / 3);
    mesh.boneCount = boneCount;
    mesh.vertices = static_cast<float*>(RL_MALLOC(count * 3 * sizeof(float)));
    mesh.texcoords = static_cast<float*>(RL_MALLOC(count * 2 * sizeof(float)));
    mesh.normals = static_cast<float*>(RL_MALLOC(count * 3 * sizeof(float)));
    mesh.animVertices = static_cast<float*>(RL_MALLOC(count * 3 * sizeof(float)));
    mesh.animNormals = static_cast<float*>(RL_MALLOC(count * 3 * sizeof(float)));
    mesh.boneIndices = static_cast<unsigned char*>(RL_MALLOC(count * 4 * sizeof(unsigned char)));
    mesh.boneWeights = static_cast<float*>(RL_MALLOC(count * 4 * sizeof(float)));
    if (builder.hasUv2) {
        mesh.texcoords2 = static_cast<float*>(RL_MALLOC(count * 2 * sizeof(float)));
    }
    if (builder.hasTangents) {
        mesh.tangents = static_cast<float*>(RL_MALLOC(count * 4 * sizeof(float)));
    }
    if (builder.hasColors) {
        mesh.colors = static_cast<unsigned char*>(RL_MALLOC(count * 4 * sizeof(unsigned char)));
    }
    for (int i = 0; i < count; ++i) {
        const CutVertex& vertex = builder.vertices[static_cast<std::size_t>(i)];
        mesh.vertices[i * 3] = vertex.position.x;
        mesh.vertices[i * 3 + 1] = vertex.position.y;
        mesh.vertices[i * 3 + 2] = vertex.position.z;
        mesh.texcoords[i * 2] = vertex.uv.x;
        mesh.texcoords[i * 2 + 1] = vertex.uv.y;
        mesh.normals[i * 3] = vertex.normal.x;
        mesh.normals[i * 3 + 1] = vertex.normal.y;
        mesh.normals[i * 3 + 2] = vertex.normal.z;
        if (mesh.texcoords2 != nullptr) {
            mesh.texcoords2[i * 2] = vertex.uv2.x;
            mesh.texcoords2[i * 2 + 1] = vertex.uv2.y;
        }
        if (mesh.tangents != nullptr) {
            mesh.tangents[i * 4] = vertex.tangent.x;
            mesh.tangents[i * 4 + 1] = vertex.tangent.y;
            mesh.tangents[i * 4 + 2] = vertex.tangent.z;
            mesh.tangents[i * 4 + 3] = vertex.tangent.w;
        }
        if (mesh.colors != nullptr) {
            std::memcpy(mesh.colors + i * 4, vertex.color.data(), 4);
        }
        for (int k = 0; k < 4; ++k) {
            mesh.boneIndices[i * 4 + k] = vertex.bones[static_cast<std::size_t>(k)];
            mesh.boneWeights[i * 4 + k] = vertex.weights[static_cast<std::size_t>(k)];
        }
    }
    std::memcpy(mesh.animVertices, mesh.vertices, count * 3 * sizeof(float));
    std::memcpy(mesh.animNormals, mesh.normals, count * 3 * sizeof(float));
    mesh.indices = static_cast<unsigned short*>(RL_MALLOC(builder.indices.size() * sizeof(unsigned short)));
    for (std::size_t i = 0; i < builder.indices.size(); ++i) {
        mesh.indices[i] = static_cast<unsigned short>(builder.indices[i]);
    }
    return mesh;
}

std::uint64_t findRoot(std::unordered_map<std::uint64_t, std::uint64_t>& parent, std::uint64_t v) {
    while (parent[v] != v) {
        parent[v] = parent[parent[v]];
        v = parent[v];
    }
    return v;
}

void buildCaps(
    const MeshBuilder& kept,
    const std::vector<std::pair<int, int>>& cutEdges,
    const Side& side,
    int fallbackBone,
    MeshBuilder& cap) {
    std::unordered_map<std::uint64_t, std::uint64_t> parent;
    std::vector<std::pair<int, int>> welded;
    for (const auto& [a, b] : cutEdges) {
        const std::uint64_t wa = kept.weld[static_cast<std::size_t>(a)];
        const std::uint64_t wb = kept.weld[static_cast<std::size_t>(b)];
        if (wa == wb) {
            continue;
        }
        welded.emplace_back(a, b);
        parent.try_emplace(wa, wa);
        parent.try_emplace(wb, wb);
        const std::uint64_t ra = findRoot(parent, wa);
        const std::uint64_t rb = findRoot(parent, wb);
        if (ra != rb) {
            parent[ra] = rb;
        }
    }
    std::unordered_map<std::uint64_t, std::vector<std::pair<int, int>>> loops;
    for (const auto& edge : welded) {
        loops[findRoot(parent, kept.weld[static_cast<std::size_t>(edge.first)])].push_back(edge);
    }
    for (const auto& [root, edges] : loops) {
        if (edges.size() < 3) {
            continue;
        }
        std::unordered_map<std::uint64_t, int> firstOf;
        for (const auto& [a, b] : edges) {
            firstOf.try_emplace(kept.weld[static_cast<std::size_t>(a)], a);
            firstOf.try_emplace(kept.weld[static_cast<std::size_t>(b)], b);
        }
        std::vector<int> ring;
        for (const auto& entry : firstOf) {
            ring.push_back(entry.second);
        }
        Vector3 center{0.0f, 0.0f, 0.0f};
        for (int v : ring) {
            center = Vector3Add(center, kept.vertices[static_cast<std::size_t>(v)].position);
        }
        center = Vector3Scale(center, 1.0f / static_cast<float>(ring.size()));
        Vector3 normal{0.0f, 0.0f, 0.0f};
        for (const auto& [a, b] : edges) {
            const Vector3 pa = kept.vertices[static_cast<std::size_t>(a)].position;
            const Vector3 pb = kept.vertices[static_cast<std::size_t>(b)].position;
            normal = Vector3Add(normal, Vector3CrossProduct(Vector3Subtract(pa, pb), Vector3Subtract(center, pb)));
        }
        if (Vector3LengthSqr(normal) < 1e-14f) {
            continue;
        }
        normal = Vector3Normalize(normal);
        float radius = 0.0f;
        for (int v : ring) {
            radius += Vector3Distance(kept.vertices[static_cast<std::size_t>(v)].position, center);
        }
        radius /= static_cast<float>(ring.size());
        const Vector3 axis = std::fabs(normal.y) < 0.9f ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f};
        const Vector3 u = Vector3Normalize(Vector3CrossProduct(normal, axis));
        const Vector3 w = Vector3CrossProduct(normal, u);

        std::unordered_map<std::uint64_t, int> capIndex;
        Influences centerInfluences;
        for (int v : ring) {
            CutVertex vertex = kept.vertices[static_cast<std::size_t>(v)];
            const Vector3 d = Vector3Subtract(vertex.position, center);
            const float angle = std::atan2(Vector3DotProduct(d, w), Vector3DotProduct(d, u));
            vertex.uv = {0.5f + 0.48f * std::cos(angle), 0.5f + 0.48f * std::sin(angle)};
            vertex.uv2 = {0.0f, 0.0f};
            vertex.normal = normal;
            vertex.tangent = {u.x, u.y, u.z, 1.0f};
            vertex.color = {255, 255, 255, 255};
            for (int k = 0; k < 4; ++k) {
                if (vertex.weights[static_cast<std::size_t>(k)] > 0.0f) {
                    addInfluence(
                        centerInfluences,
                        vertex.bones[static_cast<std::size_t>(k)],
                        vertex.weights[static_cast<std::size_t>(k)]);
                }
            }
            capIndex[kept.weld[static_cast<std::size_t>(v)]] = static_cast<int>(cap.vertices.size());
            cap.vertices.push_back(vertex);
        }
        CutVertex middle;
        middle.position = Vector3Add(center, Vector3Scale(normal, 0.2f * radius));
        middle.uv = {0.5f, 0.5f};
        middle.normal = normal;
        middle.tangent = {u.x, u.y, u.z, 1.0f};
        assignWeights(middle, centerInfluences, side, fallbackBone);
        const int middleIndex = static_cast<int>(cap.vertices.size());
        cap.vertices.push_back(middle);
        for (const auto& [a, b] : edges) {
            cap.indices.push_back(capIndex[kept.weld[static_cast<std::size_t>(b)]]);
            cap.indices.push_back(capIndex[kept.weld[static_cast<std::size_t>(a)]]);
            cap.indices.push_back(middleIndex);
        }
    }
}

}

bool cutSkinnedMesh(
    const Mesh& source,
    const std::vector<std::uint8_t>& inside,
    bool keepInside,
    int fallbackBone,
    Mesh& kept,
    Mesh& cap) {
    if (!skinnedMesh(source) || source.indices == nullptr || source.vertexCount <= 0) {
        return false;
    }
    const Side side{inside, keepInside};
    const int vertexCount = source.vertexCount;
    const std::vector<int> group = weldGroups(source);
    std::vector<float> groupScore(static_cast<std::size_t>(vertexCount), 0.0f);
    std::vector<int> groupSize(static_cast<std::size_t>(vertexCount), 0);
    for (int v = 0; v < vertexCount; ++v) {
        const std::size_t g = static_cast<std::size_t>(group[static_cast<std::size_t>(v)]);
        for (int k = 0; k < 4; ++k) {
            const float weight = source.boneWeights[v * 4 + k];
            if (weight > 0.0f) {
                groupScore[g] += side.isInside(source.boneIndices[v * 4 + k]) ? weight : -weight;
            }
        }
        ++groupSize[g];
    }
    std::vector<float> score(static_cast<std::size_t>(vertexCount), 0.0f);
    for (int v = 0; v < vertexCount; ++v) {
        const std::size_t g = static_cast<std::size_t>(group[static_cast<std::size_t>(v)]);
        score[static_cast<std::size_t>(v)] = groupScore[g] / static_cast<float>(groupSize[g]);
    }
    auto keeps = [&](int v) {
        const float s = score[static_cast<std::size_t>(v)];
        return keepInside ? s > 0.0f : s <= 0.0f;
    };

    MeshBuilder out;
    out.hasUv2 = source.texcoords2 != nullptr;
    out.hasTangents = source.tangents != nullptr;
    out.hasColors = source.colors != nullptr;
    std::vector<int> remap(static_cast<std::size_t>(vertexCount), -1);
    std::unordered_map<std::uint64_t, int> edgeVertices;
    std::vector<std::pair<int, int>> cutEdges;

    auto mapVertex = [&](int v) {
        int& slot = remap[static_cast<std::size_t>(v)];
        if (slot < 0) {
            CutVertex vertex = readVertex(source, v);
            assignWeights(vertex, influencesOf(source, v), side, fallbackBone);
            slot = static_cast<int>(out.vertices.size());
            out.vertices.push_back(vertex);
            out.weld.push_back(cornerWeld(group[static_cast<std::size_t>(v)]));
        }
        return slot;
    };
    auto edgeVertex = [&](int a, int b) {
        const int ga = group[static_cast<std::size_t>(a)];
        const int gb = group[static_cast<std::size_t>(b)];
        const int lo = ga < gb || (ga == gb && a < b) ? a : b;
        const int hi = lo == a ? b : a;
        const std::uint64_t key = (static_cast<std::uint64_t>(std::min(a, b)) << 32) | static_cast<std::uint32_t>(std::max(a, b));
        const auto found = edgeVertices.find(key);
        if (found != edgeVertices.end()) {
            return found->second;
        }
        const float sLo = score[static_cast<std::size_t>(lo)];
        const float sHi = score[static_cast<std::size_t>(hi)];
        const float denom = sLo - sHi;
        const float t = std::fabs(denom) > 1e-8f ? std::clamp(sLo / denom, 0.0f, 1.0f) : 0.5f;
        CutVertex vertex = lerpVertex(readVertex(source, lo), readVertex(source, hi), t);
        Influences influences;
        for (const auto& [bone, weight] : influencesOf(source, lo)) {
            addInfluence(influences, bone, weight * (1.0f - t));
        }
        for (const auto& [bone, weight] : influencesOf(source, hi)) {
            addInfluence(influences, bone, weight * t);
        }
        assignWeights(vertex, influences, side, fallbackBone);
        const int index = static_cast<int>(out.vertices.size());
        out.vertices.push_back(vertex);
        const int gLo = group[static_cast<std::size_t>(lo)];
        const int gHi = group[static_cast<std::size_t>(hi)];
        out.weld.push_back(t <= 0.0f ? cornerWeld(gLo) : t >= 1.0f ? cornerWeld(gHi) : edgeWeld(gLo, gHi));
        edgeVertices.emplace(key, index);
        return index;
    };

    for (int tri = 0; tri < source.triangleCount; ++tri) {
        const int corner[3] = {
            source.indices[tri * 3],
            source.indices[tri * 3 + 1],
            source.indices[tri * 3 + 2],
        };
        if (corner[0] >= vertexCount || corner[1] >= vertexCount || corner[2] >= vertexCount ||
            (corner[0] == corner[1] && corner[1] == corner[2])) {
            continue;
        }
        const bool k0 = keeps(corner[0]);
        const bool k1 = keeps(corner[1]);
        const bool k2 = keeps(corner[2]);
        if (k0 && k1 && k2) {
            for (int c : corner) {
                out.indices.push_back(mapVertex(c));
            }
            continue;
        }
        if (!k0 && !k1 && !k2) {
            continue;
        }
        std::vector<int> polygon;
        int exitVertex = -1;
        int entryVertex = -1;
        for (int e = 0; e < 3; ++e) {
            const int a = corner[e];
            const int b = corner[(e + 1) % 3];
            const bool ka = keeps(a);
            if (ka) {
                polygon.push_back(mapVertex(a));
            }
            if (ka != keeps(b)) {
                const int crossing = edgeVertex(a, b);
                polygon.push_back(crossing);
                (ka ? exitVertex : entryVertex) = crossing;
            }
        }
        for (std::size_t k = 1; k + 1 < polygon.size(); ++k) {
            out.indices.push_back(polygon[0]);
            out.indices.push_back(polygon[k]);
            out.indices.push_back(polygon[k + 1]);
        }
        if (exitVertex >= 0 && entryVertex >= 0 && exitVertex != entryVertex) {
            cutEdges.emplace_back(exitVertex, entryVertex);
        }
    }
    if (out.indices.empty()) {
        const int v = mapVertex(0);
        out.indices.insert(out.indices.end(), {v, v, v});
    }

    MeshBuilder capOut;
    capOut.hasUv2 = out.hasUv2;
    capOut.hasTangents = out.hasTangents;
    capOut.hasColors = out.hasColors;
    buildCaps(out, cutEdges, side, fallbackBone, capOut);
    if (out.vertices.size() > 65535 || capOut.vertices.size() > 65535) {
        return false;
    }
    kept = buildMesh(out, source.boneCount);
    cap = capOut.indices.empty() ? Mesh{} : buildMesh(capOut, source.boneCount);
    return true;
}

void freeCutMesh(Mesh& mesh) {
    RL_FREE(mesh.vertices);
    RL_FREE(mesh.texcoords);
    RL_FREE(mesh.texcoords2);
    RL_FREE(mesh.normals);
    RL_FREE(mesh.tangents);
    RL_FREE(mesh.colors);
    RL_FREE(mesh.indices);
    RL_FREE(mesh.boneIndices);
    RL_FREE(mesh.boneWeights);
    RL_FREE(mesh.animVertices);
    RL_FREE(mesh.animNormals);
    mesh = {};
}

bool cutSkinnedModel(
    Model& model,
    const std::vector<std::uint8_t>& inside,
    bool keepInside,
    int fallbackBone,
    const Material& capMaterial) {
    if (model.meshes == nullptr || model.meshCount <= 0) {
        return false;
    }
    bool changed = false;
    std::vector<Mesh> caps;
    for (int meshIndex = 0; meshIndex < model.meshCount; ++meshIndex) {
        Mesh& mesh = model.meshes[meshIndex];
        if (!skinnedMesh(mesh)) {
            continue;
        }
        Mesh kept{};
        Mesh cap{};
        if (!cutSkinnedMesh(mesh, inside, keepInside, fallbackBone, kept, cap)) {
            continue;
        }
        UnloadMesh(mesh);
        UploadMesh(&kept, true);
        mesh = kept;
        changed = true;
        if (cap.vertexCount > 0) {
            UploadMesh(&cap, true);
            caps.push_back(cap);
        }
    }
    if (caps.empty()) {
        return changed;
    }
    const int oldMeshes = model.meshCount;
    const int meshCount = oldMeshes + static_cast<int>(caps.size());
    const int materialCount = std::max(model.materialCount, meshCount);
    Mesh* meshes = static_cast<Mesh*>(RL_CALLOC(meshCount, sizeof(Mesh)));
    int* meshMaterial = static_cast<int*>(RL_CALLOC(meshCount, sizeof(int)));
    Material* materials = static_cast<Material*>(RL_CALLOC(materialCount, sizeof(Material)));
    for (int i = 0; i < oldMeshes; ++i) {
        meshes[i] = model.meshes[i];
        meshMaterial[i] = model.meshMaterial != nullptr ? model.meshMaterial[i] : i;
    }
    for (int i = 0; i < model.materialCount; ++i) {
        materials[i] = model.materials[i];
    }
    for (std::size_t k = 0; k < caps.size(); ++k) {
        const int index = oldMeshes + static_cast<int>(k);
        meshes[index] = caps[k];
        meshMaterial[index] = index;
        materials[index] = capMaterial;
    }
    RL_FREE(model.meshes);
    RL_FREE(model.meshMaterial);
    RL_FREE(model.materials);
    model.meshes = meshes;
    model.meshMaterial = meshMaterial;
    model.materials = materials;
    model.meshCount = meshCount;
    model.materialCount = materialCount;
    return true;
}

void restoreSkinnedModel(Model& model, const Model& source) {
    if (model.meshes == nullptr || source.meshes == nullptr) {
        return;
    }
    const int shared = std::min(model.meshCount, source.meshCount);
    for (int meshIndex = 0; meshIndex < shared; ++meshIndex) {
        Mesh& mesh = model.meshes[meshIndex];
        const Mesh& original = source.meshes[meshIndex];
        if (skinnedMesh(mesh) && skinnedMesh(original) && mesh.vaoId != original.vaoId) {
            UnloadMesh(mesh);
            mesh = cloneSkinnedMeshInstance(original);
        }
    }
    for (int meshIndex = shared; meshIndex < model.meshCount; ++meshIndex) {
        if (skinnedMesh(model.meshes[meshIndex])) {
            UnloadMesh(model.meshes[meshIndex]);
        }
        model.meshes[meshIndex] = {};
    }
    model.meshCount = shared;
    model.materialCount = std::min(model.materialCount, std::max(source.materialCount, shared));
}

}
