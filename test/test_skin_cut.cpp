#include "test_assert.hpp"

#include "render/skin_cut.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <vector>

#include <raymath.h>

namespace slopengine {

namespace {

struct Tube {
    std::vector<float> vertices;
    std::vector<float> normals;
    std::vector<float> texcoords;
    std::vector<unsigned char> boneIndices;
    std::vector<float> boneWeights;
    std::vector<unsigned short> indices;
    Mesh mesh{};

    int add(Vector3 p) {
        const float limb = std::clamp((p.y - 0.3f) / 1.4f, 0.0f, 1.0f);
        vertices.insert(vertices.end(), {p.x, p.y, p.z});
        normals.insert(normals.end(), {p.x, 0.0f, p.z});
        texcoords.insert(texcoords.end(), {p.x, p.y});
        boneIndices.insert(boneIndices.end(), {0, 1, 0, 0});
        boneWeights.insert(boneWeights.end(), {1.0f - limb, limb, 0.0f, 0.0f});
        return static_cast<int>(vertices.size() / 3) - 1;
    }

    void tri(int a, int b, int c) {
        indices.insert(
            indices.end(),
            {static_cast<unsigned short>(a), static_cast<unsigned short>(b), static_cast<unsigned short>(c)});
    }

    Tube() {
        constexpr int rings = 5;
        constexpr int segments = 8;
        for (int ring = 0; ring < rings; ++ring) {
            for (int seg = 0; seg < segments; ++seg) {
                const float angle = 2.0f * PI * static_cast<float>(seg) / segments;
                add({std::cos(angle), 0.45f * static_cast<float>(ring), std::sin(angle)});
            }
        }
        const int bottom = add({0.0f, 0.0f, 0.0f});
        const int top = add({0.0f, 0.45f * (rings - 1), 0.0f});
        auto at = [&](int ring, int seg) { return ring * segments + (seg % segments); };
        for (int ring = 0; ring + 1 < rings; ++ring) {
            for (int seg = 0; seg < segments; ++seg) {
                tri(at(ring, seg), at(ring + 1, seg), at(ring + 1, seg + 1));
                tri(at(ring, seg), at(ring + 1, seg + 1), at(ring, seg + 1));
            }
        }
        for (int seg = 0; seg < segments; ++seg) {
            tri(bottom, at(0, seg), at(0, seg + 1));
            tri(top, at(rings - 1, seg + 1), at(rings - 1, seg));
        }
        mesh.vertexCount = static_cast<int>(vertices.size() / 3);
        mesh.triangleCount = static_cast<int>(indices.size() / 3);
        mesh.vertices = vertices.data();
        mesh.normals = normals.data();
        mesh.texcoords = texcoords.data();
        mesh.boneIndices = boneIndices.data();
        mesh.boneWeights = boneWeights.data();
        mesh.indices = indices.data();
        mesh.boneCount = 2;
    }
};

using Key = std::tuple<long, long, long>;

Key keyOf(const Mesh& mesh, int v) {
    return {
        std::lround(mesh.vertices[v * 3] * 1e4f),
        std::lround(mesh.vertices[v * 3 + 1] * 1e4f),
        std::lround(mesh.vertices[v * 3 + 2] * 1e4f),
    };
}

void collectEdges(const Mesh& mesh, std::map<std::pair<Key, Key>, int>& edges) {
    for (int tri = 0; tri < mesh.triangleCount; ++tri) {
        for (int e = 0; e < 3; ++e) {
            const Key a = keyOf(mesh, mesh.indices[tri * 3 + e]);
            const Key b = keyOf(mesh, mesh.indices[tri * 3 + (e + 1) % 3]);
            if (a != b) {
                ++edges[{a, b}];
            }
        }
    }
}

bool closed(const std::vector<const Mesh*>& meshes) {
    std::map<std::pair<Key, Key>, int> edges;
    for (const Mesh* mesh : meshes) {
        collectEdges(*mesh, edges);
    }
    for (const auto& [edge, count] : edges) {
        const auto reverse = edges.find({edge.second, edge.first});
        if (count != 1 || reverse == edges.end() || reverse->second != 1) {
            return false;
        }
    }
    return true;
}

bool weightsOnSide(const Mesh& mesh, int bone) {
    for (int v = 0; v < mesh.vertexCount; ++v) {
        float sum = 0.0f;
        for (int k = 0; k < 4; ++k) {
            const float weight = mesh.boneWeights[v * 4 + k];
            if (weight > 0.0f && mesh.boneIndices[v * 4 + k] != bone) {
                return false;
            }
            sum += weight;
        }
        if (std::fabs(sum - 1.0f) > 1e-4f) {
            return false;
        }
    }
    return true;
}

}

void runSkinCutTests() {
    Tube tube;
    CHECK(closed({&tube.mesh}));
    const std::vector<std::uint8_t> limb{0, 1};

    Mesh piece{};
    Mesh pieceCap{};
    CHECK(cutSkinnedMesh(tube.mesh, limb, true, 1, piece, pieceCap));
    CHECK(pieceCap.vertexCount > 0);
    CHECK(closed({&piece, &pieceCap}));
    CHECK(weightsOnSide(piece, 1));
    CHECK(weightsOnSide(pieceCap, 1));

    Mesh body{};
    Mesh bodyCap{};
    CHECK(cutSkinnedMesh(tube.mesh, limb, false, 0, body, bodyCap));
    CHECK(bodyCap.vertexCount > 0);
    CHECK(closed({&body, &bodyCap}));
    CHECK(weightsOnSide(body, 0));
    CHECK(weightsOnSide(bodyCap, 0));

    float pieceLow = 1e9f;
    for (int v = 0; v < piece.vertexCount; ++v) {
        pieceLow = std::min(pieceLow, piece.vertices[v * 3 + 1]);
    }
    float bodyHigh = -1e9f;
    for (int v = 0; v < body.vertexCount; ++v) {
        bodyHigh = std::max(bodyHigh, body.vertices[v * 3 + 1]);
    }
    CHECK(std::fabs(pieceLow - 1.0f) < 1e-3f);
    CHECK(std::fabs(bodyHigh - 1.0f) < 1e-3f);

    float capTop = -1e9f;
    for (int v = 0; v < bodyCap.vertexCount; ++v) {
        capTop = std::max(capTop, bodyCap.vertices[v * 3 + 1]);
    }
    CHECK(capTop > 1.05f);

    freeCutMesh(piece);
    freeCutMesh(pieceCap);
    freeCutMesh(body);
    freeCutMesh(bodyCap);
}

}
