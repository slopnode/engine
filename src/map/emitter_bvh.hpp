#pragma once

#include "map/radiosity_emitters.hpp"

#include <raylib.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace slopengine {

struct EmitterBvh {
    struct Node {
        Vector3 mins{};
        Vector3 maxs{};
        std::int32_t left = -1;
        std::int32_t right = -1;
        std::int32_t firstPrim = -1;
        std::int32_t primCount = 0;
    };

    struct Prim {
        Vector3 mins{};
        Vector3 maxs{};
        Vector3 centroid{};
        std::int32_t emitterIndex = -1;
    };

    std::vector<Node> nodes;
    std::vector<Prim> prims;
    std::int32_t root = -1;

    bool empty() const { return root < 0 || prims.empty(); }
};

/** Influence radius used when building emitter BVH leaf bounds. */
float emitterInfluenceRadius(Vector3 radiance, float area, float minPad);

/** Maximum influence radius across all emitters (for luxel query spheres). */
float maxEmitterInfluenceRadius(
    const std::vector<EmitterPatch>& emitters,
    float minPad);

EmitterBvh buildEmitterBvh(const std::vector<EmitterPatch>& emitters, float minPad);

/** Invokes @p fn(emitterIndex) for emitters whose BVH bounds overlap the query sphere. */
void forEachEmitterNear(
    const EmitterBvh& bvh,
    Vector3 position,
    float queryRadius,
    const std::function<void(std::int32_t emitterIndex)>& fn);

}
