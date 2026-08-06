#pragma once

#include "map/bsp.hpp"

#include <raylib.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace slopengine {

struct NavPortalLink {
    int neighborLeaf = -1;
    Vector3 portalCenter{};
    float cost = 0.0f;
};

/** Leaf portal graph built from sealed BSP hull. */
struct MapNavigation {
    int leafCount = 0;
    std::vector<bool> walkable;
    std::vector<Vector3> leafCentroids;
    std::vector<std::vector<NavPortalLink>> adjacency;
};

/** Builds walkable leaf adjacency from BSP portals (interior open leaves only). */
MapNavigation buildMapNavigation(
    const BspTree& tree,
    const std::vector<std::uint8_t>* exteriorEmpty = nullptr);

/** A* path over walkable leaves; empty if unreachable. */
std::vector<int> findLeafPath(const MapNavigation& nav, int fromLeaf, int toLeaf);

/** Portal centers between consecutive leaves, ending at goalPos. */
std::vector<Vector3> leafPathToWaypoints(
    const MapNavigation& nav,
    const std::vector<int>& leafPath,
    Vector3 goalPos);

/** Portal center on the edge between two adjacent leaves, if linked. */
std::optional<Vector3> portalCenterBetween(
    const MapNavigation& nav,
    int leafA,
    int leafB);

inline float navHorizontalDist(Vector3 a, Vector3 b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

inline float navHorizontalDistSq(Vector3 a, Vector3 b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return dx * dx + dz * dz;
}

}
