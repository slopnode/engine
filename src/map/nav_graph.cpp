#include "map/nav_graph.hpp"

#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>

namespace slopengine {

namespace {

bool isNavWalkableLeaf(
    const BspTree& tree,
    int leaf,
    const std::vector<std::uint8_t>* exteriorEmpty) {
    if (leaf < 0 || leaf >= static_cast<int>(tree.leaves.size())) {
        return false;
    }
    if (!leafParticipatesInPortalGraph(tree.leaves[static_cast<std::size_t>(leaf)].contents)) {
        return false;
    }
    if (exteriorEmpty != nullptr &&
        leaf < static_cast<int>(exteriorEmpty->size()) &&
        (*exteriorEmpty)[static_cast<std::size_t>(leaf)] != 0) {
        return false;
    }
    return true;
}

Vector3 portalPolygonCenter(const std::vector<Vector3>& vertices) {
    if (vertices.empty()) {
        return {};
    }
    Vector3 sum{};
    for (const Vector3& v : vertices) {
        sum = Vector3Add(sum, v);
    }
    const float inv = 1.0f / static_cast<float>(vertices.size());
    return Vector3Scale(sum, inv);
}

float navDist3(Vector3 a, Vector3 b) {
    return Vector3Distance(a, b);
}

// Walks straight down from (x, startY, z) through open leaves (via exact BSP
// point classification, not an approximation) until it lands in solid content
// or leaves the map, returning the Y of the last open leaf reached — i.e. the
// real floor a ground agent would come to rest on there. A leaf's own
// mins.y is not trustworthy on its own: face-plane-driven BSP splitting lets
// a brush anywhere else in the level contribute a horizontal split plane that
// slices straight through unrelated open leaves, leaving some of them
// reporting a "floor" that's really just empty air over a leaf stacked
// beneath them.
float descendToRealFloor(const BspTree& tree, float x, float z, float startY) {
    constexpr float kStepEps = 0.01f;
    constexpr int kMaxSteps = 64;
    float y = startY;
    for (int step = 0; step < kMaxSteps; ++step) {
        const float probeY = y - kStepEps;
        if (probeY < tree.boundsMins.y) {
            return y;
        }
        const std::int32_t below = pointLeaf(tree, {x, probeY, z});
        if (below < 0) {
            return y;
        }
        const BspLeaf& leaf = tree.leaves[static_cast<std::size_t>(below)];
        if (leafBlocksFlood(leaf.contents)) {
            return y;
        }
        y = leaf.mins.y;
    }
    return y;
}

// Resolves leaf i's true floor by descending from several points spread
// across its footprint (its centroid, plus the midpoint toward every face
// vertex — all guaranteed inside this convex leaf), not just the centroid
// alone: a single probe can be fooled by a small local opening (a grate, a
// pipe gap, a window portal) that isn't representative of the leaf's floor
// as a whole. Only commits the corrected floor when every sample agrees;
// a leaf where samples disagree keeps its original mins.y rather than
// guessing.
float resolveLeafFloorY(const BspTree& tree, const BspLeaf& leaf, Vector3 centroid) {
    const float startY = leaf.mins.y;
    float lo = descendToRealFloor(tree, centroid.x, centroid.z, startY);
    float hi = lo;
    for (const auto& face : leaf.faces) {
        for (const Vector3& v : face) {
            const float sx = 0.5f * (centroid.x + v.x);
            const float sz = 0.5f * (centroid.z + v.z);
            const float y = descendToRealFloor(tree, sx, sz, startY);
            lo = std::min(lo, y);
            hi = std::max(hi, y);
        }
    }
    constexpr float kAgreementEps = 0.05f;
    if (hi - lo > kAgreementEps) {
        return leaf.mins.y; // samples disagree — leave the leaf's own floor alone
    }
    return lo;
}

} // namespace

MapNavigation buildMapNavigation(
    const BspTree& tree,
    const std::vector<std::uint8_t>* exteriorEmpty) {
    MapNavigation nav;
    const int n = static_cast<int>(tree.leaves.size());
    nav.leafCount = n;
    if (n <= 0) {
        return nav;
    }

    nav.walkable.assign(static_cast<std::size_t>(n), false);
    nav.leafCentroids.resize(static_cast<std::size_t>(n));
    nav.leafFloorY.resize(static_cast<std::size_t>(n));
    nav.leafCeilingY.resize(static_cast<std::size_t>(n));
    nav.adjacency.resize(static_cast<std::size_t>(n));

    for (int i = 0; i < n; ++i) {
        const BspLeaf& leaf = tree.leaves[static_cast<std::size_t>(i)];
        nav.leafCentroids[static_cast<std::size_t>(i)] = leafCentroid(leaf);
        nav.leafFloorY[static_cast<std::size_t>(i)] = leaf.mins.y;
        nav.leafCeilingY[static_cast<std::size_t>(i)] = leaf.maxs.y;
        nav.walkable[static_cast<std::size_t>(i)] = isNavWalkableLeaf(tree, i, exteriorEmpty);
    }

    for (int i = 0; i < n; ++i) {
        if (!nav.walkable[static_cast<std::size_t>(i)]) {
            continue;
        }
        nav.leafFloorY[static_cast<std::size_t>(i)] = resolveLeafFloorY(
            tree,
            tree.leaves[static_cast<std::size_t>(i)],
            nav.leafCentroids[static_cast<std::size_t>(i)]);
    }

    for (const BspPortal& portal : tree.portals) {
        if (portal.leafA < 0 || portal.leafB < 0 || portal.vertices.size() < 3) {
            continue;
        }
        if (portal.leafA >= n || portal.leafB >= n) {
            continue;
        }
        if (!nav.walkable[static_cast<std::size_t>(portal.leafA)] ||
            !nav.walkable[static_cast<std::size_t>(portal.leafB)]) {
            continue;
        }

        const Vector3 center = portalPolygonCenter(portal.vertices);
        const Vector3& centroidA = nav.leafCentroids[static_cast<std::size_t>(portal.leafA)];
        const Vector3& centroidB = nav.leafCentroids[static_cast<std::size_t>(portal.leafB)];
        const float costAB = navDist3(centroidA, center) + navDist3(center, centroidB);

        nav.adjacency[static_cast<std::size_t>(portal.leafA)].push_back(
            NavPortalLink{portal.leafB, center, costAB, portal.doorBrushId});
        nav.adjacency[static_cast<std::size_t>(portal.leafB)].push_back(
            NavPortalLink{portal.leafA, center, costAB, portal.doorBrushId});
    }

    nav.reverseAdjacency.assign(static_cast<std::size_t>(n), {});
    for (int i = 0; i < n; ++i) {
        for (const NavPortalLink& link : nav.adjacency[static_cast<std::size_t>(i)]) {
            if (link.neighborLeaf < 0 || link.neighborLeaf >= n) {
                continue;
            }
            nav.reverseAdjacency[static_cast<std::size_t>(link.neighborLeaf)].push_back(
                NavPortalLink{i, link.portalCenter, link.cost, link.doorBrushId});
        }
    }

    TraceLog(
        LOG_INFO,
        "NAV: built graph leaves=%d walkable=%d portals=%d",
        n,
        static_cast<int>(std::count(nav.walkable.begin(), nav.walkable.end(), true)),
        static_cast<int>(tree.portals.size()));

    return nav;
}

std::optional<Vector3> portalCenterBetween(const MapNavigation& nav, int leafA, int leafB) {
    if (leafA < 0 || leafB < 0 || leafA >= nav.leafCount || leafB >= nav.leafCount) {
        return std::nullopt;
    }
    for (const NavPortalLink& link : nav.adjacency[static_cast<std::size_t>(leafA)]) {
        if (link.neighborLeaf == leafB) {
            return link.portalCenter;
        }
    }
    return std::nullopt;
}

std::vector<int> findLeafPath(
    const MapNavigation& nav,
    int fromLeaf,
    int toLeaf,
    const DoorOpenQuery& isDoorOpen,
    float maxClimb) {
    if (fromLeaf < 0 || toLeaf < 0 || fromLeaf >= nav.leafCount || toLeaf >= nav.leafCount) {
        return {};
    }
    if (!nav.walkable[static_cast<std::size_t>(fromLeaf)] ||
        !nav.walkable[static_cast<std::size_t>(toLeaf)]) {
        return {};
    }
    if (fromLeaf == toLeaf) {
        return {fromLeaf};
    }

    struct Node {
        int leaf = -1;
        float f = 0.0f;
        bool operator>(const Node& other) const { return f > other.f; }
    };

    const auto heuristic = [&](int leaf) {
        return navHorizontalDist(
            nav.leafCentroids[static_cast<std::size_t>(leaf)],
            nav.leafCentroids[static_cast<std::size_t>(toLeaf)]);
    };

    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    std::unordered_map<int, int> cameFrom;
    std::unordered_map<int, float> gScore;
    gScore[fromLeaf] = 0.0f;
    open.push(Node{fromLeaf, heuristic(fromLeaf)});

    while (!open.empty()) {
        const int current = open.top().leaf;
        open.pop();
        if (current == toLeaf) {
            std::vector<int> path;
            for (int at = toLeaf;; at = cameFrom.at(at)) {
                path.push_back(at);
                if (at == fromLeaf) {
                    break;
                }
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        const float currentG = gScore[current];
        for (const NavPortalLink& link : nav.adjacency[static_cast<std::size_t>(current)]) {
            const int next = link.neighborLeaf;
            if (next < 0 || next >= nav.leafCount ||
                !nav.walkable[static_cast<std::size_t>(next)]) {
                continue;
            }
            if (isDoorOpen && !link.doorBrushId.empty() && !isDoorOpen(link.doorBrushId)) {
                continue;
            }
            const float rise = nav.leafFloorY[static_cast<std::size_t>(next)] -
                nav.leafFloorY[static_cast<std::size_t>(current)];
            if (rise > maxClimb) {
                continue;
            }
            const float tentative = currentG + link.cost;
            const auto it = gScore.find(next);
            if (it != gScore.end() && tentative >= it->second) {
                continue;
            }
            cameFrom[next] = current;
            gScore[next] = tentative;
            open.push(Node{next, tentative + heuristic(next)});
        }
    }

    return {};
}

NavFlowField buildNavFlowField(
    const MapNavigation& nav,
    int goalLeaf,
    const DoorOpenQuery& isDoorOpen,
    float maxClimb) {
    NavFlowField field;
    field.goalLeaf = goalLeaf;
    field.maxClimb = maxClimb;
    if (nav.leafCount <= 0) {
        return field;
    }
    field.nextLeaf.assign(static_cast<std::size_t>(nav.leafCount), -1);
    if (goalLeaf < 0 || goalLeaf >= nav.leafCount ||
        !nav.walkable[static_cast<std::size_t>(goalLeaf)]) {
        return field;
    }

    struct Node {
        int leaf = -1;
        float g = 0.0f;
        bool operator>(const Node& other) const { return g > other.g; }
    };

    std::vector<float> gScore(
        static_cast<std::size_t>(nav.leafCount), std::numeric_limits<float>::infinity());
    gScore[static_cast<std::size_t>(goalLeaf)] = 0.0f;

    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    open.push(Node{goalLeaf, 0.0f});

    while (!open.empty()) {
        const int current = open.top().leaf;
        open.pop();
        const float currentG = gScore[static_cast<std::size_t>(current)];

        for (const NavPortalLink& link : nav.reverseAdjacency[static_cast<std::size_t>(current)]) {
            const int prev = link.neighborLeaf;
            if (prev < 0 || prev >= nav.leafCount ||
                !nav.walkable[static_cast<std::size_t>(prev)]) {
                continue;
            }
            if (isDoorOpen && !link.doorBrushId.empty() && !isDoorOpen(link.doorBrushId)) {
                continue;
            }
            const float rise = nav.leafFloorY[static_cast<std::size_t>(current)] -
                nav.leafFloorY[static_cast<std::size_t>(prev)];
            if (rise > maxClimb) {
                continue;
            }
            const float tentative = currentG + link.cost;
            if (tentative < gScore[static_cast<std::size_t>(prev)]) {
                gScore[static_cast<std::size_t>(prev)] = tentative;
                field.nextLeaf[static_cast<std::size_t>(prev)] = current;
                open.push(Node{prev, tentative});
            }
        }
    }

    return field;
}

std::vector<int> flowFieldPathFrom(const NavFlowField& field, int fromLeaf) {
    if (fromLeaf < 0 || fromLeaf >= static_cast<int>(field.nextLeaf.size())) {
        return {};
    }
    if (fromLeaf == field.goalLeaf) {
        return {fromLeaf};
    }
    if (field.nextLeaf[static_cast<std::size_t>(fromLeaf)] < 0) {
        return {};
    }

    std::vector<int> path{fromLeaf};
    int current = fromLeaf;
    const int guard = static_cast<int>(field.nextLeaf.size()) + 1;
    for (int step = 0; step < guard; ++step) {
        current = field.nextLeaf[static_cast<std::size_t>(current)];
        if (current < 0) {
            return {};
        }
        path.push_back(current);
        if (current == field.goalLeaf) {
            return path;
        }
    }
    return {};
}

std::vector<Vector3> leafPathToWaypoints(
    const MapNavigation& nav,
    const std::vector<int>& leafPath,
    Vector3 goalPos,
    bool flyerWaypoints) {
    if (leafPath.empty()) {
        return {};
    }
    if (leafPath.size() == 1) {
        return {goalPos};
    }

    std::vector<Vector3> waypoints;
    waypoints.reserve(leafPath.size());
    for (std::size_t i = 0; i + 1 < leafPath.size(); ++i) {
        const int fromLeaf = leafPath[i];
        const int toLeaf = leafPath[i + 1];
        const float floorY = nav.leafFloorY[static_cast<std::size_t>(fromLeaf)];
        const std::optional<Vector3> center = portalCenterBetween(nav, fromLeaf, toLeaf);
        if (center.has_value()) {
            const float wpY = flyerWaypoints ? center->y : floorY;
            waypoints.push_back({center->x, wpY, center->z});
        } else {
            const Vector3& fromCentroid = nav.leafCentroids[static_cast<std::size_t>(fromLeaf)];
            const Vector3& toCentroid = nav.leafCentroids[static_cast<std::size_t>(toLeaf)];
            waypoints.push_back({
                0.5f * (fromCentroid.x + toCentroid.x),
                floorY,
                0.5f * (fromCentroid.z + toCentroid.z)});
        }
    }
    waypoints.push_back(goalPos);
    return waypoints;
}

}
