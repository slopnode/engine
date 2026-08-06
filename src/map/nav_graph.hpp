#pragma once

#include "map/bsp.hpp"

#include <raylib.h>

#include <cmath>
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
    std::vector<float> leafFloorY;
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

inline bool navLeafPassedWaypoint(
    int agentLeaf,
    const std::vector<int>& leafPath,
    int waypointIndex) {
    if (agentLeaf < 0 || waypointIndex < 0 || leafPath.empty()) {
        return false;
    }
    for (std::size_t j = static_cast<std::size_t>(waypointIndex) + 1; j < leafPath.size(); ++j) {
        if (leafPath[j] == agentLeaf) {
            return true;
        }
    }
    return false;
}

inline int navFindLeafInPath(const std::vector<int>& leafPath, int leaf, int startIndex = 0) {
    if (leaf < 0) {
        return -1;
    }
    for (int i = startIndex; i < static_cast<int>(leafPath.size()); ++i) {
        if (leafPath[static_cast<std::size_t>(i)] == leaf) {
            return i;
        }
    }
    return -1;
}

inline bool navLeafPathSuffixEqual(
    const std::vector<int>& oldPath,
    int oldStart,
    const std::vector<int>& newPath,
    int newStart) {
    if (oldStart < 0 || newStart < 0) {
        return false;
    }
    const int oldRemain = static_cast<int>(oldPath.size()) - oldStart;
    const int newRemain = static_cast<int>(newPath.size()) - newStart;
    if (oldRemain != newRemain || oldRemain <= 0) {
        return false;
    }
    for (int i = 0; i < oldRemain; ++i) {
        if (oldPath[static_cast<std::size_t>(oldStart + i)] !=
            newPath[static_cast<std::size_t>(newStart + i)]) {
            return false;
        }
    }
    return true;
}

/** True when a replan from @p agentLeaf to @p goalLeaf keeps the same route ahead. */
inline bool navLeafPathRouteUnchanged(
    const std::vector<int>& currentPath,
    int agentLeaf,
    const std::vector<int>& newPath) {
    if (currentPath.empty() || newPath.empty()) {
        return false;
    }
    if (currentPath == newPath) {
        return true;
    }
    const int oldIdx = navFindLeafInPath(currentPath, agentLeaf);
    const int newIdx = navFindLeafInPath(newPath, agentLeaf);
    return navLeafPathSuffixEqual(currentPath, oldIdx, newPath, newIdx);
}

inline bool navWaypointCompleted(
    const std::vector<Vector3>& waypoints,
    const std::vector<int>& waypointToLeaf,
    const std::vector<int>& leafPath,
    Vector3 agentPos,
    int agentLeaf,
    int waypointIndex,
    float arriveRadius) {
    if (waypointIndex < 0 || waypointIndex >= static_cast<int>(waypoints.size())) {
        return false;
    }

    const Vector3& wp = waypoints[static_cast<std::size_t>(waypointIndex)];
    const float arriveRadiusSq = arriveRadius * arriveRadius;
    if (navHorizontalDistSq(agentPos, wp) <= arriveRadiusSq) {
        return true;
    }

    constexpr float kStairVertPassed = 0.25f;
    constexpr float kStairVertAlign = 0.05f;
    constexpr float kStairHorizScale = 1.5f;
    if (agentPos.y >= wp.y + kStairVertPassed) {
        return true;
    }
    if (agentPos.y >= wp.y - kStairVertAlign) {
        const float relaxed = arriveRadius * kStairHorizScale;
        if (navHorizontalDistSq(agentPos, wp) <= relaxed * relaxed) {
            return true;
        }
    }

    const int lastIndex = static_cast<int>(waypoints.size()) - 1;
    if (waypointIndex >= lastIndex) {
        return false;
    }

    if (agentLeaf < 0 || waypointToLeaf.size() != waypoints.size()) {
        return false;
    }

    const int toLeaf = waypointToLeaf[static_cast<std::size_t>(waypointIndex)];
    if (toLeaf >= 0 && agentLeaf == toLeaf) {
        return true;
    }

    return navLeafPassedWaypoint(agentLeaf, leafPath, waypointIndex);
}

inline void buildWaypointToLeaf(
    const std::vector<int>& leafPath,
    int goalLeaf,
    std::vector<int>& out) {
    out.clear();
    if (leafPath.empty()) {
        return;
    }
    if (leafPath.size() == 1) {
        out.push_back(goalLeaf);
        return;
    }
    out.resize(leafPath.size());
    for (std::size_t i = 0; i + 1 < leafPath.size(); ++i) {
        out[i] = leafPath[i + 1];
    }
    out.back() = goalLeaf;
}

inline int findResumeWaypointIndex(
    const std::vector<Vector3>& waypoints,
    const std::vector<int>& waypointToLeaf,
    const std::vector<int>& leafPath,
    Vector3 agentPos,
    int agentLeaf,
    float arriveRadius) {
    if (waypoints.empty()) {
        return 0;
    }

    int index = 0;
    while (index < static_cast<int>(waypoints.size()) &&
           navWaypointCompleted(
               waypoints,
               waypointToLeaf,
               leafPath,
               agentPos,
               agentLeaf,
               index,
               arriveRadius)) {
        ++index;
    }

    const int last = static_cast<int>(waypoints.size()) - 1;
    return index > last ? last : index;
}

/** Horizontal-only resume; empty leaf context. */
inline int findResumeWaypointIndex(
    const std::vector<Vector3>& waypoints,
    Vector3 agentPos,
    float arriveRadius) {
    static const std::vector<int> kEmptyLeaves{};
    return findResumeWaypointIndex(
        waypoints, kEmptyLeaves, kEmptyLeaves, agentPos, -1, arriveRadius);
}

}
