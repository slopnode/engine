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
    if (!leafIsOpen(tree.leaves[static_cast<std::size_t>(leaf)].contents)) {
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
    nav.adjacency.resize(static_cast<std::size_t>(n));

    for (int i = 0; i < n; ++i) {
        nav.leafCentroids[static_cast<std::size_t>(i)] =
            leafCentroid(tree.leaves[static_cast<std::size_t>(i)]);
        nav.leafFloorY[static_cast<std::size_t>(i)] =
            tree.leaves[static_cast<std::size_t>(i)].mins.y;
        nav.walkable[static_cast<std::size_t>(i)] = isNavWalkableLeaf(tree, i, exteriorEmpty);
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
            NavPortalLink{portal.leafB, center, costAB});
        nav.adjacency[static_cast<std::size_t>(portal.leafB)].push_back(
            NavPortalLink{portal.leafA, center, costAB});
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

std::vector<int> findLeafPath(const MapNavigation& nav, int fromLeaf, int toLeaf) {
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

std::vector<Vector3> leafPathToWaypoints(
    const MapNavigation& nav,
    const std::vector<int>& leafPath,
    Vector3 goalPos) {
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
            waypoints.push_back({center->x, floorY, center->z});
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
