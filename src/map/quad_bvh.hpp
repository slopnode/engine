#pragma once

#include "map/bsp.hpp"
#include "map/lightmap.hpp"

#include <raylib.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace slopengine {

struct QuadBvhHit {
    float distance = 0.0f;
    Vector3 point{};
    Vector3 normal{};
    std::int32_t faceIndex = -1;
};

struct QuadBvh {
    struct Node {
        Vector3 mins{};
        Vector3 maxs{};
        std::int32_t left = -1;
        std::int32_t right = -1;
        std::int32_t firstPrim = -1;
        std::int32_t primCount = 0;
    };

    struct Prim {
        std::array<Vector3, 3> tri{};
        Vector3 normal{};
        std::int32_t faceIndex = -1;
        Vector3 mins{};
        Vector3 maxs{};
        Vector3 centroid{};
    };

    std::vector<Node> nodes;
    std::vector<Prim> prims;
    std::int32_t root = -1;

    bool empty() const { return root < 0 || prims.empty(); }
};

QuadBvh buildTriangleBvh(
    const std::array<Vector3, 3>* tris,
    const Vector3* normals,
    const std::int32_t* faceIndices,
    std::size_t count);

QuadBvh buildBspSurfaceBvh(const BspTree& tree);
QuadBvh buildLightmapFaceBvh(const std::vector<LightmapFace>& faces);

std::optional<QuadBvhHit> raycastQuadBvh(
    const QuadBvh& bvh,
    Vector3 origin,
    Vector3 direction,
    float maxDistance,
    std::int32_t ignoreFaceIndex = -1,
    const std::vector<char>* skipFaces = nullptr);

bool quadSegmentOccluded(
    const QuadBvh& bvh,
    Vector3 from,
    Vector3 to,
    std::int32_t ignoreFaceA = -1,
    std::int32_t ignoreFaceB = -1,
    const std::vector<char>* skipFaces = nullptr);

}
