#include "map/nav_graph.hpp"

#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace slopengine {

namespace {

// How many extra units of routing cost a step "costs" per world-unit of drop beyond an agent's
// maxFall tolerance. Chosen so a several-meter ledge reliably outweighs a modest detour, without
// making the penalty effectively infinite (which would degenerate into the hard-block behavior
// this design deliberately avoids -- see nav_graph.hpp's findLeafPath doc comment).
constexpr float kExcessFallCostPerUnit = 8.0f;

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

constexpr float kFragmentFootprintArea = 2.0f;
constexpr std::size_t kMinMacroChainLength = 3;

bool isPlainEdge(const MapNavigation& nav, int fromLeaf, const NavPortalLink& link) {
    const int neighbor = link.neighborLeaf;
    if (neighbor < 0 || neighbor >= nav.leafCount) {
        return false;
    }
    if (!nav.walkable[static_cast<std::size_t>(neighbor)]) {
        return false;
    }
    if (!link.doorBrushId.empty()) {
        return false;
    }
    if (nav.leafIsWater[static_cast<std::size_t>(fromLeaf)] ||
        nav.leafIsWater[static_cast<std::size_t>(neighbor)]) {
        return false;
    }
    return true;
}

bool isFragmentLeaf(const BspTree& tree, int leaf) {
    if (leaf < 0 || leaf >= static_cast<int>(tree.leaves.size())) {
        return false;
    }
    const BspLeaf& bspLeaf = tree.leaves[static_cast<std::size_t>(leaf)];
    const float dx = bspLeaf.maxs.x - bspLeaf.mins.x;
    const float dz = bspLeaf.maxs.z - bspLeaf.mins.z;
    return dx * dz < kFragmentFootprintArea;
}

struct PortalSpread {
    Vector3 tangent{1.0f, 0.0f, 0.0f};
    float halfWidth = 0.0f;
};

PortalSpread computePortalHorizontalSpread(const std::vector<Vector3>& vertices) {
    constexpr float kSpreadFraction = 0.5f;
    constexpr float kEdgeClearance = 0.45f;
    constexpr float kFlatPortalYExtent = 0.05f;

    if (vertices.empty()) {
        return {};
    }
    float minY = vertices[0].y;
    float maxY = vertices[0].y;
    for (const Vector3& v : vertices) {
        minY = std::min(minY, v.y);
        maxY = std::max(maxY, v.y);
    }
    if (maxY - minY < kFlatPortalYExtent) {
        return {};
    }

    float bestDistSq = 0.0f;
    Vector3 bestDelta{1.0f, 0.0f, 0.0f};
    for (std::size_t a = 0; a < vertices.size(); ++a) {
        for (std::size_t b = a + 1; b < vertices.size(); ++b) {
            const float dx = vertices[b].x - vertices[a].x;
            const float dz = vertices[b].z - vertices[a].z;
            const float distSq = dx * dx + dz * dz;
            if (distSq > bestDistSq) {
                bestDistSq = distSq;
                bestDelta = {dx, 0.0f, dz};
            }
        }
    }

    const float dist = std::sqrt(bestDistSq);
    if (dist < 1.0e-4f) {
        return {};
    }
    constexpr float kMaxSpreadDist = 4.0f;
    const float clampedDist = std::min(dist, kMaxSpreadDist);
    const float halfWidth = std::max(0.0f, clampedDist * kSpreadFraction - kEdgeClearance);
    return {Vector3Scale(bestDelta, 1.0f / dist), halfWidth};
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

struct FunnelPortal {
    Vector3 left;
    Vector3 right;
};

struct FunnelCorner {
    Vector3 point;
    int corridorIndex = 0;
};

float triarea2(Vector3 a, Vector3 b, Vector3 c) {
    return (b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z);
}

bool sameXZ(Vector3 a, Vector3 b) {
    constexpr float kEps = 1.0e-5f;
    return std::fabs(a.x - b.x) < kEps && std::fabs(a.z - b.z) < kEps;
}

std::vector<FunnelCorner> runFunnel(const std::vector<FunnelPortal>& corridor) {
    std::vector<FunnelCorner> corners;
    if (corridor.size() < 2) {
        return corners;
    }

    Vector3 apex = corridor[0].left;
    Vector3 left = corridor[0].left;
    Vector3 right = corridor[0].right;
    int apexIndex = 0;
    int leftIndex = 0;
    int rightIndex = 0;
    corners.push_back({apex, 0});

    const int n = static_cast<int>(corridor.size());
    for (int i = 1; i < n; ++i) {
        const Vector3 pLeft = corridor[static_cast<std::size_t>(i)].left;
        const Vector3 pRight = corridor[static_cast<std::size_t>(i)].right;
        TraceLog(LOG_INFO, "FUNNEL: i=%d apex=(%.2f,%.2f) left=(%.2f,%.2f)[%d] right=(%.2f,%.2f)[%d] "
            "pLeft=(%.2f,%.2f) pRight=(%.2f,%.2f)",
            i, apex.x, apex.z, left.x, left.z, leftIndex, right.x, right.z, rightIndex,
            pLeft.x, pLeft.z, pRight.x, pRight.z);

        if (triarea2(apex, right, pRight) <= 0.0f) {
            if (sameXZ(apex, right) || triarea2(apex, left, pRight) > 0.0f) {
                right = pRight;
                rightIndex = i;
                TraceLog(LOG_INFO, "FUNNEL:   narrow right -> (%.2f,%.2f)[%d]", right.x, right.z, rightIndex);
            } else {
                corners.push_back({left, leftIndex});
                TraceLog(LOG_INFO, "FUNNEL:   LOCK CORNER (left branch) point=(%.2f,%.2f) corridorIdx=%d",
                    left.x, left.z, leftIndex);
                apex = left;
                apexIndex = leftIndex;
                left = apex;
                right = apex;
                leftIndex = apexIndex;
                rightIndex = apexIndex;
                i = apexIndex;
                continue;
            }
        }

        if (triarea2(apex, left, pLeft) >= 0.0f) {
            if (sameXZ(apex, left) || triarea2(apex, right, pLeft) < 0.0f) {
                left = pLeft;
                leftIndex = i;
                TraceLog(LOG_INFO, "FUNNEL:   narrow left -> (%.2f,%.2f)[%d]", left.x, left.z, leftIndex);
            } else {
                corners.push_back({right, rightIndex});
                TraceLog(LOG_INFO, "FUNNEL:   LOCK CORNER (right branch) point=(%.2f,%.2f) corridorIdx=%d",
                    right.x, right.z, rightIndex);
                apex = right;
                apexIndex = rightIndex;
                left = apex;
                right = apex;
                leftIndex = apexIndex;
                rightIndex = apexIndex;
                i = apexIndex;
                continue;
            }
        }
    }

    corners.push_back({corridor.back().left, n - 1});
    return corners;
}

FunnelPortal computePortalCorners(const MapNavigation& nav, int fromLeaf, int toLeaf, Vector3 prevPoint) {
    const NavPortalLink* link = portalLinkBetween(nav, fromLeaf, toLeaf);
    if (link == nullptr) {
        const Vector3& a = nav.leafCentroids[static_cast<std::size_t>(fromLeaf)];
        const Vector3& b = nav.leafCentroids[static_cast<std::size_t>(toLeaf)];
        const Vector3 mid{0.5f * (a.x + b.x), 0.5f * (a.y + b.y), 0.5f * (a.z + b.z)};
        TraceLog(LOG_INFO, "PORTALCORNERS: %d->%d NO LINK mid=(%.2f,%.2f,%.2f)", fromLeaf, toLeaf, mid.x, mid.y, mid.z);
        return {mid, mid};
    }
    if (link->portalHalfWidth <= 0.0f) {
        TraceLog(LOG_INFO, "PORTALCORNERS: %d->%d halfWidth<=0 center=(%.2f,%.2f,%.2f)", fromLeaf, toLeaf,
            link->portalCenter.x, link->portalCenter.y, link->portalCenter.z);
        return {link->portalCenter, link->portalCenter};
    }

    Vector3 dir{
        link->portalCenter.x - prevPoint.x,
        0.0f,
        link->portalCenter.z - prevPoint.z};
    float dirLen = std::sqrt(dir.x * dir.x + dir.z * dir.z);
    if (dirLen < 1.0e-4f) {
        dir = link->portalTangent;
        dirLen = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        if (dirLen < 1.0e-4f) {
            TraceLog(LOG_INFO, "PORTALCORNERS: %d->%d degenerate dir, center=(%.2f,%.2f,%.2f)", fromLeaf, toLeaf,
                link->portalCenter.x, link->portalCenter.y, link->portalCenter.z);
            return {link->portalCenter, link->portalCenter};
        }
    }
    dir = Vector3Scale(dir, 1.0f / dirLen);
    const Vector3 perp{dir.z, 0.0f, -dir.x};
    const float sign =
        (perp.x * link->portalTangent.x + perp.z * link->portalTangent.z) >= 0.0f ? 1.0f : -1.0f;
    const Vector3 offset = Vector3Scale(link->portalTangent, sign * link->portalHalfWidth);
    const FunnelPortal result{
        Vector3Subtract(link->portalCenter, offset), Vector3Add(link->portalCenter, offset)};
    TraceLog(
        LOG_INFO,
        "PORTALCORNERS: %d->%d center=(%.2f,%.2f,%.2f) tangent=(%.2f,%.2f,%.2f) halfWidth=%.2f dir=(%.2f,%.2f) "
        "perp=(%.2f,%.2f) sign=%.0f left=(%.2f,%.2f,%.2f) right=(%.2f,%.2f,%.2f)",
        fromLeaf, toLeaf, link->portalCenter.x, link->portalCenter.y, link->portalCenter.z,
        link->portalTangent.x, link->portalTangent.y, link->portalTangent.z, link->portalHalfWidth, dir.x, dir.z,
        perp.x, perp.z, sign, result.left.x, result.left.y, result.left.z, result.right.x, result.right.y,
        result.right.z);
    return result;
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
    nav.leafIsWater.assign(static_cast<std::size_t>(n), false);
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
        nav.leafIsWater[static_cast<std::size_t>(i)] = (leaf.contents & BspContents::Water) != 0;
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
        const PortalSpread spread = computePortalHorizontalSpread(portal.vertices);

        nav.adjacency[static_cast<std::size_t>(portal.leafA)].push_back(
            NavPortalLink{portal.leafB, center, spread.tangent, spread.halfWidth, costAB, portal.doorBrushId});
        nav.adjacency[static_cast<std::size_t>(portal.leafB)].push_back(
            NavPortalLink{portal.leafA, center, spread.tangent, spread.halfWidth, costAB, portal.doorBrushId});
    }

    nav.reverseAdjacency.assign(static_cast<std::size_t>(n), {});
    for (int i = 0; i < n; ++i) {
        for (const NavPortalLink& link : nav.adjacency[static_cast<std::size_t>(i)]) {
            if (link.neighborLeaf < 0 || link.neighborLeaf >= n) {
                continue;
            }
            nav.reverseAdjacency[static_cast<std::size_t>(link.neighborLeaf)].push_back(
                NavPortalLink{
                    i, link.portalCenter, link.portalTangent, link.portalHalfWidth, link.cost,
                    link.doorBrushId});
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

const NavPortalLink* portalLinkBetween(const MapNavigation& nav, int leafA, int leafB) {
    if (leafA < 0 || leafB < 0 || leafA >= nav.leafCount || leafB >= nav.leafCount) {
        return nullptr;
    }
    for (const NavPortalLink& link : nav.adjacency[static_cast<std::size_t>(leafA)]) {
        if (link.neighborLeaf == leafB) {
            return &link;
        }
    }
    return nullptr;
}

std::vector<NavMacroChainCandidate> findMacroChainCandidates(const MapNavigation& nav, const BspTree& tree) {
    std::vector<NavMacroChainCandidate> result;
    if (nav.leafCount <= 0) {
        return result;
    }

    std::vector<bool> visited(static_cast<std::size_t>(nav.leafCount), false);

    for (int start = 0; start < nav.leafCount; ++start) {
        if (visited[static_cast<std::size_t>(start)] ||
            !nav.walkable[static_cast<std::size_t>(start)] ||
            !isFragmentLeaf(tree, start)) {
            continue;
        }

        std::vector<int> cluster;
        std::vector<int> externalNeighbors;
        std::vector<int> stack{start};
        visited[static_cast<std::size_t>(start)] = true;

        while (!stack.empty()) {
            const int current = stack.back();
            stack.pop_back();
            cluster.push_back(current);

            for (const NavPortalLink& link : nav.adjacency[static_cast<std::size_t>(current)]) {
                if (!isPlainEdge(nav, current, link)) {
                    continue;
                }
                const int neighbor = link.neighborLeaf;
                if (!isFragmentLeaf(tree, neighbor)) {
                    externalNeighbors.push_back(neighbor);
                    continue;
                }
                if (!visited[static_cast<std::size_t>(neighbor)]) {
                    visited[static_cast<std::size_t>(neighbor)] = true;
                    stack.push_back(neighbor);
                }
            }
        }

        if (cluster.size() < kMinMacroChainLength) {
            continue;
        }

        std::sort(externalNeighbors.begin(), externalNeighbors.end());
        externalNeighbors.erase(
            std::unique(externalNeighbors.begin(), externalNeighbors.end()), externalNeighbors.end());

        std::unordered_set<int> remaining(externalNeighbors.begin(), externalNeighbors.end());
        std::vector<int> groupReps;
        while (!remaining.empty()) {
            const int groupStart = *remaining.begin();
            remaining.erase(remaining.begin());
            std::vector<int> groupStack{groupStart};
            while (!groupStack.empty()) {
                const int current = groupStack.back();
                groupStack.pop_back();
                for (const NavPortalLink& link : nav.adjacency[static_cast<std::size_t>(current)]) {
                    if (!isPlainEdge(nav, current, link)) {
                        continue;
                    }
                    const auto it = remaining.find(link.neighborLeaf);
                    if (it != remaining.end()) {
                        groupStack.push_back(*it);
                        remaining.erase(it);
                    }
                }
            }
            groupReps.push_back(groupStart);
        }

        if (groupReps.size() != 2) {
            continue;
        }

        result.push_back(NavMacroChainCandidate{groupReps[0], groupReps[1], std::move(cluster)});
    }

    return result;
}

std::vector<int> findLeafPath(
    const MapNavigation& nav,
    int fromLeaf,
    int toLeaf,
    const DoorOpenQuery& isDoorOpen,
    float maxClimb,
    float maxFall,
    float waterCostMultiplier) {
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
            const float drop = -rise;
            const float fallPenalty =
                drop > maxFall ? (drop - maxFall) * kExcessFallCostPerUnit : 0.0f;
            const float destWaterMultiplier =
                nav.leafIsWater[static_cast<std::size_t>(next)] ? waterCostMultiplier : 1.0f;
            const float tentative = currentG + link.cost * destWaterMultiplier + fallPenalty;
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
    float maxClimb,
    float maxFall,
    float waterCostMultiplier) {
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
            const float drop = -rise;
            const float fallPenalty =
                drop > maxFall ? (drop - maxFall) * kExcessFallCostPerUnit : 0.0f;
            const float destWaterMultiplier =
                nav.leafIsWater[static_cast<std::size_t>(current)] ? waterCostMultiplier : 1.0f;
            const float tentative = currentG + link.cost * destWaterMultiplier + fallPenalty;
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
    bool flyerWaypoints,
    float lateralBias,
    const Vector3* startPos) {
    if (leafPath.empty()) {
        return {};
    }
    if (leafPath.size() == 1) {
        return {goalPos};
    }

    const Vector3 start =
        startPos != nullptr ? *startPos : nav.leafCentroids[static_cast<std::size_t>(leafPath.front())];

    const std::size_t portalCount = leafPath.size() - 1;
    std::vector<FunnelPortal> corridor;
    corridor.reserve(portalCount + 2);
    corridor.push_back({start, start});
    Vector3 prevPoint = start;
    for (std::size_t i = 0; i < portalCount; ++i) {
        const FunnelPortal portal = computePortalCorners(nav, leafPath[i], leafPath[i + 1], prevPoint);
        prevPoint = {0.5f * (portal.left.x + portal.right.x), 0.0f, 0.5f * (portal.left.z + portal.right.z)};
        corridor.push_back(portal);
    }
    corridor.push_back({goalPos, goalPos});

    const std::vector<FunnelCorner> corners = runFunnel(corridor);

    std::vector<Vector3> waypoints;
    waypoints.reserve(portalCount + 1);
    std::size_t cornerCursor = 0;
    for (std::size_t i = 0; i < portalCount; ++i) {
        const int fromLeaf = leafPath[i];
        const int toLeaf = leafPath[i + 1];
        const int corridorIndex = static_cast<int>(i) + 1;
        const FunnelPortal& portal = corridor[static_cast<std::size_t>(corridorIndex)];
        const Vector3 portalPoint{
            0.5f * (portal.left.x + portal.right.x), 0.0f, 0.5f * (portal.left.z + portal.right.z)};

        while (cornerCursor + 1 < corners.size() &&
               corners[cornerCursor + 1].corridorIndex < corridorIndex) {
            ++cornerCursor;
        }
        const FunnelCorner& a = corners[cornerCursor];
        const FunnelCorner& b = corners[std::min(cornerCursor + 1, corners.size() - 1)];

        Vector3 pointXZ;
        if (a.corridorIndex == b.corridorIndex) {
            pointXZ = a.point;
        } else {
            const float dx = b.point.x - a.point.x;
            const float dz = b.point.z - a.point.z;
            const float segLenSq = dx * dx + dz * dz;
            float t = 0.0f;
            if (segLenSq > 1.0e-8f) {
                t = ((portalPoint.x - a.point.x) * dx + (portalPoint.z - a.point.z) * dz) / segLenSq;
                t = std::clamp(t, 0.0f, 1.0f);
            }
            pointXZ = {a.point.x + t * dx, 0.0f, a.point.z + t * dz};
        }
        TraceLog(
            LOG_INFO,
            "PROJECT: i=%zu from=%d to=%d corridorIdx=%d portalPoint=(%.2f,%.2f) cursor=%zu "
            "a=(%.2f,%.2f)[%d] b=(%.2f,%.2f)[%d] result=(%.2f,%.2f)",
            i, fromLeaf, toLeaf, corridorIndex, portalPoint.x, portalPoint.z, cornerCursor, a.point.x,
            a.point.z, a.corridorIndex, b.point.x, b.point.z, b.corridorIndex, pointXZ.x, pointXZ.z);

        const NavPortalLink* link = portalLinkBetween(nav, fromLeaf, toLeaf);
        const float floorY = nav.leafFloorY[static_cast<std::size_t>(fromLeaf)];
        float y = floorY;
        if (link != nullptr && flyerWaypoints) {
            y = link->portalCenter.y;
        }

        Vector3 point{pointXZ.x, y, pointXZ.z};
        if (link != nullptr && link->portalHalfWidth > 0.0f) {
            point = Vector3Add(point, Vector3Scale(link->portalTangent, lateralBias * link->portalHalfWidth));
        }
        waypoints.push_back(point);
    }
    waypoints.push_back(goalPos);
    return waypoints;
}

}
