#include "map/emitter_bvh.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace slopengine {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr int kLeafSize = 8;

Vector3 min3(Vector3 a, Vector3 b) {
    return {
        std::min(a.x, b.x),
        std::min(a.y, b.y),
        std::min(a.z, b.z),
    };
}

Vector3 max3(Vector3 a, Vector3 b) {
    return {
        std::max(a.x, b.x),
        std::max(a.y, b.y),
        std::max(a.z, b.z),
    };
}

Vector3 sub3(Vector3 a, Vector3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vector3 add3(Vector3 a, Vector3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

bool aabbOverlapsSphere(Vector3 mins, Vector3 maxs, Vector3 center, float radius) {
    const float dx = std::max({mins.x - center.x, 0.0f, center.x - maxs.x});
    const float dy = std::max({mins.y - center.y, 0.0f, center.y - maxs.y});
    const float dz = std::max({mins.z - center.z, 0.0f, center.z - maxs.z});
    const float radius2 = radius * radius;
    return dx * dx + dy * dy + dz * dz <= radius2;
}

std::int32_t buildRecursive(
    EmitterBvh& bvh,
    std::vector<std::int32_t>& indices,
    std::int32_t start,
    std::int32_t end) {
    EmitterBvh::Node node;
    node.mins = {1e30f, 1e30f, 1e30f};
    node.maxs = {-1e30f, -1e30f, -1e30f};
    for (std::int32_t i = start; i < end; ++i) {
        const EmitterBvh::Prim& prim =
            bvh.prims[static_cast<std::size_t>(indices[static_cast<std::size_t>(i)])];
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
        const Vector3 c =
            bvh.prims[static_cast<std::size_t>(indices[static_cast<std::size_t>(i)])].centroid;
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
        const Vector3 c =
            bvh.prims[static_cast<std::size_t>(indices[static_cast<std::size_t>(i)])].centroid;
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

float emitterInfluenceRadius(Vector3 radiance, float area, float minPad) {
    const float lum = emitterLuminance(radiance);
    const float radius2 = lum * area / (kMinEmitterContrib * kPi);
    return std::max(minPad, std::sqrt(std::max(0.0f, radius2)));
}

float maxEmitterInfluenceRadius(const std::vector<EmitterPatch>& emitters, float minPad) {
    float maxRadius = minPad;
    for (const EmitterPatch& emitter : emitters) {
        maxRadius = std::max(
            maxRadius,
            emitterInfluenceRadius(emitter.radiance, emitter.area, minPad));
    }
    return maxRadius;
}

EmitterBvh buildEmitterBvh(const std::vector<EmitterPatch>& emitters, float minPad) {
    EmitterBvh bvh;
    if (emitters.empty()) {
        return bvh;
    }

    bvh.prims.resize(emitters.size());
    std::vector<std::int32_t> buildIndices(emitters.size());
    for (std::size_t i = 0; i < emitters.size(); ++i) {
        const EmitterPatch& emitter = emitters[i];
        const float radius = emitterInfluenceRadius(emitter.radiance, emitter.area, minPad);
        EmitterBvh::Prim& prim = bvh.prims[i];
        prim.mins = sub3(emitter.position, Vector3{radius, radius, radius});
        prim.maxs = add3(emitter.position, Vector3{radius, radius, radius});
        prim.centroid = emitter.position;
        prim.emitterIndex = static_cast<std::int32_t>(i);
        buildIndices[i] = static_cast<std::int32_t>(i);
    }

    bvh.root = buildRecursive(
        bvh,
        buildIndices,
        0,
        static_cast<std::int32_t>(emitters.size()));

    std::vector<EmitterBvh::Prim> packed(emitters.size());
    for (std::size_t i = 0; i < emitters.size(); ++i) {
        packed[i] = bvh.prims[static_cast<std::size_t>(buildIndices[i])];
    }
    bvh.prims = std::move(packed);
    return bvh;
}

void forEachEmitterNear(
    const EmitterBvh& bvh,
    Vector3 position,
    float queryRadius,
    const std::function<void(std::int32_t emitterIndex)>& fn) {
    if (bvh.empty() || queryRadius <= 0.0f) {
        return;
    }

    std::vector<std::int32_t> stack;
    stack.push_back(bvh.root);
    while (!stack.empty()) {
        const std::int32_t nodeIndex = stack.back();
        stack.pop_back();
        const EmitterBvh::Node& node = bvh.nodes[static_cast<std::size_t>(nodeIndex)];
        if (!aabbOverlapsSphere(node.mins, node.maxs, position, queryRadius)) {
            continue;
        }
        if (node.primCount > 0) {
            for (std::int32_t i = 0; i < node.primCount; ++i) {
                const EmitterBvh::Prim& prim =
                    bvh.prims[static_cast<std::size_t>(node.firstPrim + i)];
                if (!aabbOverlapsSphere(prim.mins, prim.maxs, position, queryRadius)) {
                    continue;
                }
                fn(prim.emitterIndex);
            }
            continue;
        }
        if (node.right >= 0) {
            stack.push_back(node.right);
        }
        if (node.left >= 0) {
            stack.push_back(node.left);
        }
    }
}

}
