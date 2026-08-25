#pragma once

#include "map/bsp.hpp"

#include <raylib.h>

#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace slopengine {

struct NavPortalLink {
    int neighborLeaf = -1;
    Vector3 portalCenter{};
    /** Unit horizontal direction along the portal's widest span; only meaningful when
     *  portalHalfWidth > 0. */
    Vector3 portalTangent{1.0f, 0.0f, 0.0f};
    /** How far a waypoint may be nudged off-center along portalTangent and still clear
     *  the portal's edges, in world units. Zero for portals too narrow to spread agents
     *  across (doorways, thin gaps). */
    float portalHalfWidth = 0.0f;
    float cost = 0.0f;
    /** Brush id of the Door gating this link, or empty if the link is ungated. */
    std::string doorBrushId;
};

/** Answers whether the door with the given brush id currently allows sound/nav to pass.
 *  An empty/unset query treats every gated link as open (no gating). */
using DoorOpenQuery = std::function<bool(const std::string& doorBrushId)>;

/** Leaf portal graph built from sealed BSP hull. */
struct MapNavigation {
    int leafCount = 0;
    std::vector<bool> walkable;
    /** True for leaves whose BSP contents include Water. Read by pathing to apply a per-agent
     *  routing cost preference (see findLeafPath/buildNavFlowField's @p waterCostMultiplier) --
     *  water leaves stay walkable either way, this only biases which route is cheaper. */
    std::vector<bool> leafIsWater;
    std::vector<Vector3> leafCentroids;
    std::vector<float> leafFloorY;
    std::vector<float> leafCeilingY;
    std::vector<std::vector<NavPortalLink>> adjacency;
    std::vector<std::vector<NavPortalLink>> reverseAdjacency;
};

struct NavFlowField {
    int goalLeaf = -1;
    float maxClimb = 0.0f;
    std::vector<int> nextLeaf;
};

/** Builds walkable leaf adjacency from BSP portals (interior open leaves only). */
MapNavigation buildMapNavigation(
    const BspTree& tree,
    const std::vector<std::uint8_t>* exteriorEmpty = nullptr);

/** A* path over walkable leaves; empty if unreachable. Links gated by a closed door
 *  (per @p isDoorOpen) are skipped, same as an unwalkable leaf. @p maxClimb caps how far
 *  a step can rise from one leaf's floor to the next (a ground actor's step-height); pass
 *  infinity for flyers. Descending is never blocked -- instead, a drop exceeding @p maxFall
 *  accrues a routing cost penalty, and a step landing in a Water-content leaf has its cost
 *  scaled by @p waterCostMultiplier. Both default to a no-op (infinity / 1.0) so an agent
 *  that doesn't opt in reproduces the prior distance-only routing exactly. */
std::vector<int> findLeafPath(
    const MapNavigation& nav,
    int fromLeaf,
    int toLeaf,
    const DoorOpenQuery& isDoorOpen = {},
    float maxClimb = std::numeric_limits<float>::infinity(),
    float maxFall = std::numeric_limits<float>::infinity(),
    float waterCostMultiplier = 1.0f);

NavFlowField buildNavFlowField(
    const MapNavigation& nav,
    int goalLeaf,
    const DoorOpenQuery& isDoorOpen = {},
    float maxClimb = std::numeric_limits<float>::infinity(),
    float maxFall = std::numeric_limits<float>::infinity(),
    float waterCostMultiplier = 1.0f);

std::vector<int> flowFieldPathFrom(const NavFlowField& field, int fromLeaf);

/** Portal centers between consecutive leaves, ending at goalPos. @p lateralBias in
 *  [-1, 1] nudges each portal-crossing waypoint off-center along that portal's
 *  tangent (scaled by its portalHalfWidth), so agents sharing a route don't all
 *  converge on the exact same point; 0 reproduces the plain portal center. */
std::vector<Vector3> leafPathToWaypoints(
    const MapNavigation& nav,
    const std::vector<int>& leafPath,
    Vector3 goalPos,
    bool flyerWaypoints = false,
    float lateralBias = 0.0f);

/** Portal center on the edge between two adjacent leaves, if linked. */
std::optional<Vector3> portalCenterBetween(
    const MapNavigation& nav,
    int leafA,
    int leafB);

/** Full portal link (center, tangent, half-width) between two adjacent leaves, if linked.
 *  Returned pointer aliases @p nav's adjacency storage. */
const NavPortalLink* portalLinkBetween(
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
