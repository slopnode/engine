#include "map/bsp_ray.hpp"

namespace slopengine {

std::optional<BspRayHit> raycastBspSurfaces(
    const QuadBvh& bvh,
    Vector3 origin,
    Vector3 direction,
    float maxDistance,
    std::int32_t ignoreFaceIndex) {
    const auto hit = raycastQuadBvh(bvh, origin, direction, maxDistance, ignoreFaceIndex);
    if (!hit) {
        return std::nullopt;
    }
    BspRayHit out;
    out.distance = hit->distance;
    out.point = hit->point;
    out.normal = hit->normal;
    out.faceIndex = hit->faceIndex;
    return out;
}

bool bspSegmentOccluded(
    const QuadBvh& bvh,
    Vector3 from,
    Vector3 to,
    std::int32_t ignoreFaceA,
    std::int32_t ignoreFaceB) {
    return quadSegmentOccluded(bvh, from, to, ignoreFaceA, ignoreFaceB);
}

}
