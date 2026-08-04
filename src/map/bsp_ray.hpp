#pragma once

#include "map/bsp.hpp"
#include "map/quad_bvh.hpp"

#include <raylib.h>

#include <optional>

namespace slopengine {

/** Hit against a BSP surface face. */
struct BspRayHit {
    float distance = 0.0f;
    Vector3 point{};
    Vector3 normal{};
    std::int32_t faceIndex = -1;
};

/** Raycasts against surface faces in @p bvh. */
std::optional<BspRayHit> raycastBspSurfaces(
    const QuadBvh& bvh,
    Vector3 origin,
    Vector3 direction,
    float maxDistance,
    std::int32_t ignoreFaceIndex = -1,
    const std::vector<char>* skipFaces = nullptr);

/** True if the segment from @p from to @p to is occluded by a surface. */
bool bspSegmentOccluded(
    const QuadBvh& bvh,
    Vector3 from,
    Vector3 to,
    std::int32_t ignoreFaceA = -1,
    std::int32_t ignoreFaceB = -1,
    const std::vector<char>* skipFaces = nullptr);

}
