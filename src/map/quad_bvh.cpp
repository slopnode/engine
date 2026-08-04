#include "map/quad_bvh.hpp"

#include "map/brush.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace slopengine {

namespace {

constexpr int kLeafSize = 4;

float dot3(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 cross3(Vector3 a, Vector3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

Vector3 sub3(Vector3 a, Vector3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vector3 add3(Vector3 a, Vector3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 scale3(Vector3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}

Vector3 min3(Vector3 a, Vector3 b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}

Vector3 max3(Vector3 a, Vector3 b) {
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

void primBounds(QuadBvh::Prim& prim) {
    prim.mins = prim.tri[0];
    prim.maxs = prim.tri[0];
    for (int i = 1; i < 3; ++i) {
        prim.mins = min3(prim.mins, prim.tri[i]);
        prim.maxs = max3(prim.maxs, prim.tri[i]);
    }
    prim.centroid = scale3(add3(prim.mins, prim.maxs), 0.5f);
}

bool rayTriangle(
    Vector3 origin,
    Vector3 direction,
    Vector3 v0,
    Vector3 v1,
    Vector3 v2,
    float maxDistance,
    float& outT) {
    constexpr float kEpsilon = 1e-6f;
    const Vector3 e1 = sub3(v1, v0);
    const Vector3 e2 = sub3(v2, v0);
    const Vector3 pvec = cross3(direction, e2);
    const float det = dot3(e1, pvec);
    if (std::fabs(det) < kEpsilon) {
        return false;
    }
    const float invDet = 1.0f / det;
    const Vector3 tvec = sub3(origin, v0);
    const float u = dot3(tvec, pvec) * invDet;
    if (u < 0.0f || u > 1.0f) {
        return false;
    }
    const Vector3 qvec = cross3(tvec, e1);
    const float v = dot3(direction, qvec) * invDet;
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }
    const float t = dot3(e2, qvec) * invDet;
    if (t <= kEpsilon || t >= maxDistance) {
        return false;
    }
    outT = t;
    return true;
}

bool rayAabb(
    Vector3 origin,
    Vector3 invDir,
    Vector3 mins,
    Vector3 maxs,
    float maxDistance) {
    float tmin = 0.0f;
    float tmax = maxDistance;
    const float bounds[2][3] = {
        {mins.x, mins.y, mins.z},
        {maxs.x, maxs.y, maxs.z},
    };
    const float originA[3] = {origin.x, origin.y, origin.z};
    const float invA[3] = {invDir.x, invDir.y, invDir.z};
    for (int axis = 0; axis < 3; ++axis) {
        float t0 = (bounds[0][axis] - originA[axis]) * invA[axis];
        float t1 = (bounds[1][axis] - originA[axis]) * invA[axis];
        if (invA[axis] < 0.0f) {
            std::swap(t0, t1);
        }
        tmin = std::max(tmin, t0);
        tmax = std::min(tmax, t1);
        if (tmax < tmin) {
            return false;
        }
    }
    return true;
}

std::int32_t buildRecursive(
    QuadBvh& bvh,
    std::vector<std::int32_t>& indices,
    std::int32_t start,
    std::int32_t end) {
    QuadBvh::Node node;
    node.mins = {1e30f, 1e30f, 1e30f};
    node.maxs = {-1e30f, -1e30f, -1e30f};
    for (std::int32_t i = start; i < end; ++i) {
        const QuadBvh::Prim& prim = bvh.prims[static_cast<std::size_t>(indices[static_cast<std::size_t>(i)])];
        node.mins = min3(node.mins, prim.mins);
        node.maxs = max3(node.maxs, prim.maxs);
    }

    const std::int32_t count = end - start;
    if (count <= kLeafSize) {
        node.firstPrim = start;
        node.primCount = count;
        const std::int32_t nodeIndex = static_cast<std::int32_t>(bvh.nodes.size());
        bvh.nodes.push_back(node);
        return nodeIndex;
    }

    Vector3 centroidMins = {1e30f, 1e30f, 1e30f};
    Vector3 centroidMaxs = {-1e30f, -1e30f, -1e30f};
    for (std::int32_t i = start; i < end; ++i) {
        const Vector3 c = bvh.prims[static_cast<std::size_t>(indices[static_cast<std::size_t>(i)])].centroid;
        centroidMins = min3(centroidMins, c);
        centroidMaxs = max3(centroidMaxs, c);
    }
    const Vector3 extent = sub3(centroidMaxs, centroidMins);
    int axis = 0;
    if (extent.y > extent.x) {
        axis = 1;
    }
    if (extent.z > (axis == 0 ? extent.x : extent.y)) {
        axis = 2;
    }
    const float mid =
        0.5f
        * ((axis == 0 ? centroidMins.x : (axis == 1 ? centroidMins.y : centroidMins.z))
           + (axis == 0 ? centroidMaxs.x : (axis == 1 ? centroidMaxs.y : centroidMaxs.z)));

    std::int32_t pivot = start;
    for (std::int32_t i = start; i < end; ++i) {
        const Vector3 c = bvh.prims[static_cast<std::size_t>(indices[static_cast<std::size_t>(i)])].centroid;
        const float value = axis == 0 ? c.x : (axis == 1 ? c.y : c.z);
        if (value < mid) {
            std::swap(indices[static_cast<std::size_t>(pivot)], indices[static_cast<std::size_t>(i)]);
            ++pivot;
        }
    }
    if (pivot == start || pivot == end) {
        pivot = start + count / 2;
    }

    const std::int32_t nodeIndex = static_cast<std::int32_t>(bvh.nodes.size());
    bvh.nodes.push_back(node);
    const std::int32_t left = buildRecursive(bvh, indices, start, pivot);
    const std::int32_t right = buildRecursive(bvh, indices, pivot, end);
    bvh.nodes[static_cast<std::size_t>(nodeIndex)].left = left;
    bvh.nodes[static_cast<std::size_t>(nodeIndex)].right = right;
    return nodeIndex;
}

} // namespace

QuadBvh buildTriangleBvh(
    const std::array<Vector3, 3>* tris,
    const Vector3* normals,
    const std::int32_t* faceIndices,
    std::size_t count) {
    QuadBvh bvh;
    if (tris == nullptr || normals == nullptr || faceIndices == nullptr || count == 0) {
        return bvh;
    }
    bvh.prims.resize(count);
    std::vector<std::int32_t> buildIndices(count);
    for (std::size_t i = 0; i < count; ++i) {
        QuadBvh::Prim& prim = bvh.prims[i];
        prim.tri = tris[i];
        prim.normal = normals[i];
        prim.faceIndex = faceIndices[i];
        primBounds(prim);
        buildIndices[i] = static_cast<std::int32_t>(i);
    }
    bvh.root = buildRecursive(bvh, buildIndices, 0, static_cast<std::int32_t>(count));

    std::vector<QuadBvh::Prim> packed(count);
    for (std::size_t i = 0; i < count; ++i) {
        packed[i] = bvh.prims[static_cast<std::size_t>(buildIndices[i])];
    }
    bvh.prims = std::move(packed);
    return bvh;
}

QuadBvh buildBspSurfaceBvh(const BspTree& tree) {
    std::vector<std::array<Vector3, 3>> tris;
    std::vector<Vector3> normals;
    std::vector<std::int32_t> faceIndices;
    for (std::int32_t faceIndex = 0; faceIndex < static_cast<std::int32_t>(tree.surfaceFaces.size());
         ++faceIndex) {
        const BspSurfaceFace& face = tree.surfaceFaces[static_cast<std::size_t>(faceIndex)];
        const auto faceTris = triangulateFace(face.vertices);
        for (const auto& tri : faceTris) {
            tris.push_back(tri);
            normals.push_back(face.normal);
            faceIndices.push_back(faceIndex);
        }
    }
    return buildTriangleBvh(tris.data(), normals.data(), faceIndices.data(), tris.size());
}

QuadBvh buildLightmapFaceBvh(const std::vector<LightmapFace>& faces) {
    std::vector<std::array<Vector3, 3>> tris;
    std::vector<Vector3> normals;
    std::vector<std::int32_t> faceIndices;
    for (std::int32_t faceIndex = 0; faceIndex < static_cast<std::int32_t>(faces.size()); ++faceIndex) {
        const LightmapFace& face = faces[static_cast<std::size_t>(faceIndex)];
        const auto faceTris = triangulateFace(face.vertices);
        for (const auto& tri : faceTris) {
            tris.push_back(tri);
            normals.push_back(face.normal);
            faceIndices.push_back(faceIndex);
        }
    }
    return buildTriangleBvh(tris.data(), normals.data(), faceIndices.data(), tris.size());
}

namespace {

bool faceShouldSkip(std::int32_t faceIndex, const std::vector<char>* skipFaces) {
    if (skipFaces == nullptr || faceIndex < 0) {
        return false;
    }
    return static_cast<std::size_t>(faceIndex) < skipFaces->size()
        && (*skipFaces)[static_cast<std::size_t>(faceIndex)] != 0;
}

} // namespace

std::optional<QuadBvhHit> raycastQuadBvh(
    const QuadBvh& bvh,
    Vector3 origin,
    Vector3 direction,
    float maxDistance,
    std::int32_t ignoreFaceIndex,
    const std::vector<char>* skipFaces) {
    if (bvh.empty()) {
        return std::nullopt;
    }
    const float dirLen = std::sqrt(dot3(direction, direction));
    if (dirLen < 1e-8f || maxDistance <= 0.0f) {
        return std::nullopt;
    }
    const Vector3 dir = scale3(direction, 1.0f / dirLen);
    const Vector3 invDir{
        std::fabs(dir.x) < 1e-20f ? (dir.x >= 0.0f ? 1e20f : -1e20f) : 1.0f / dir.x,
        std::fabs(dir.y) < 1e-20f ? (dir.y >= 0.0f ? 1e20f : -1e20f) : 1.0f / dir.y,
        std::fabs(dir.z) < 1e-20f ? (dir.z >= 0.0f ? 1e20f : -1e20f) : 1.0f / dir.z,
    };

    std::optional<QuadBvhHit> best;
    float bestT = maxDistance;
    std::int32_t stack[64];
    int stackSize = 0;
    stack[stackSize++] = bvh.root;

    while (stackSize > 0) {
        const std::int32_t nodeIndex = stack[--stackSize];
        const QuadBvh::Node& node = bvh.nodes[static_cast<std::size_t>(nodeIndex)];
        if (!rayAabb(origin, invDir, node.mins, node.maxs, bestT)) {
            continue;
        }
        if (node.primCount > 0) {
            for (std::int32_t i = 0; i < node.primCount; ++i) {
                const QuadBvh::Prim& prim =
                    bvh.prims[static_cast<std::size_t>(node.firstPrim + i)];
                if (prim.faceIndex == ignoreFaceIndex) {
                    continue;
                }
                if (faceShouldSkip(prim.faceIndex, skipFaces)) {
                    continue;
                }
                float t = 0.0f;
                if (!rayTriangle(origin, dir, prim.tri[0], prim.tri[1], prim.tri[2], bestT, t)) {
                    continue;
                }
                if (best && t >= best->distance) {
                    continue;
                }
                QuadBvhHit hit;
                hit.distance = t;
                hit.point = add3(origin, scale3(dir, t));
                hit.normal = prim.normal;
                hit.faceIndex = prim.faceIndex;
                best = hit;
                bestT = t;
            }
            continue;
        }
        if (node.right >= 0 && stackSize < 64) {
            stack[stackSize++] = node.right;
        }
        if (node.left >= 0 && stackSize < 64) {
            stack[stackSize++] = node.left;
        }
    }
    return best;
}

bool quadSegmentOccluded(
    const QuadBvh& bvh,
    Vector3 from,
    Vector3 to,
    std::int32_t ignoreFaceA,
    std::int32_t ignoreFaceB,
    const std::vector<char>* skipFaces) {
    const Vector3 delta = sub3(to, from);
    const float distance = std::sqrt(dot3(delta, delta));
    if (distance < 1e-5f) {
        return false;
    }
    const auto hit =
        raycastQuadBvh(bvh, from, delta, distance * 0.999f, ignoreFaceA, skipFaces);
    if (!hit) {
        return false;
    }
    return hit->faceIndex != ignoreFaceB;
}

}
