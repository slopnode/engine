#include "map/nav_navmesh_build.hpp"

#include "map/door_portal_tag.hpp"
#include "map/pvs.hpp"

#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace slopengine {

namespace {

Vector3 polyCentroid(const std::vector<Vector3>& vertices) {
    Vector3 sum{};
    for (const Vector3& v : vertices) {
        sum = Vector3Add(sum, v);
    }
    return Vector3Scale(sum, 1.0f / static_cast<float>(vertices.size()));
}

// Recast only ever emits convex polygons, so a sign-consistency walk over the edges
// (every edge's cross product agreeing in sign) is a sufficient containment test.
bool pointInsideConvexPolygonXZ(const std::vector<Vector3>& poly, float x, float z) {
    if (poly.size() < 3) {
        return false;
    }
    constexpr float kEps = 1.0e-3f;
    bool sawPositive = false;
    bool sawNegative = false;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const Vector3& a = poly[i];
        const Vector3& b = poly[(i + 1) % poly.size()];
        const float cross = (b.x - a.x) * (z - a.z) - (b.z - a.z) * (x - a.x);
        if (cross > kEps) {
            sawPositive = true;
        } else if (cross < -kEps) {
            sawNegative = true;
        }
        if (sawPositive && sawNegative) {
            return false;
        }
    }
    return true;
}

bool waterBrushContains(const std::vector<Brush>& brushes, Vector3 point) {
    for (const Brush& brush : brushes) {
        if (brush.role == BrushRole::Water && pointInsideBrushInclusive(point, brush)) {
            return true;
        }
    }
    return false;
}

// Barycentric height lookup: true (with u/v/w set) if (x, z) falls inside triangle abc's
// XZ projection, ignoring the triangle's own Y (which is exactly what we want to solve for).
bool baryXZ(Vector3 a, Vector3 b, Vector3 c, float x, float z, float& u, float& v, float& w) {
    const float v0x = b.x - a.x, v0z = b.z - a.z;
    const float v1x = c.x - a.x, v1z = c.z - a.z;
    const float v2x = x - a.x, v2z = z - a.z;
    const float d00 = v0x * v0x + v0z * v0z;
    const float d01 = v0x * v1x + v0z * v1z;
    const float d11 = v1x * v1x + v1z * v1z;
    const float d20 = v2x * v0x + v2z * v0z;
    const float d21 = v2x * v1x + v2z * v1z;
    const float denom = d00 * d11 - d01 * d01;
    if (std::fabs(denom) < 1.0e-8f) {
        return false;
    }
    v = (d11 * d20 - d01 * d21) / denom;
    w = (d00 * d21 - d01 * d20) / denom;
    u = 1.0f - v - w;
    constexpr float kEps = -1.0e-3f;
    return u >= kEps && v >= kEps && w >= kEps;
}

float polySurfaceYAt(const std::vector<Vector3>& poly, float x, float z, float fallbackY) {
    for (std::size_t i = 1; i + 1 < poly.size(); ++i) {
        float u, v, w;
        if (baryXZ(poly[0], poly[i], poly[i + 1], x, z, u, v, w)) {
            return u * poly[0].y + v * poly[i].y + w * poly[i + 1].y;
        }
    }
    return fallbackY;
}

struct PolyEdgeSnap {
    float dist = std::numeric_limits<float>::infinity();
    float y = 0.0f;
};

PolyEdgeSnap closestEdgePointXZ(const std::vector<Vector3>& poly, float x, float z) {
    PolyEdgeSnap best;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const Vector3& a = poly[i];
        const Vector3& b = poly[(i + 1) % poly.size()];
        const float abx = b.x - a.x, abz = b.z - a.z;
        const float abLenSq = abx * abx + abz * abz;
        float t = abLenSq > 1.0e-8f ? ((x - a.x) * abx + (z - a.z) * abz) / abLenSq : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        const float dx = x - (a.x + t * abx);
        const float dz = z - (a.z + t * abz);
        const float dist = std::sqrt(dx * dx + dz * dz);
        if (dist < best.dist) {
            best.dist = dist;
            best.y = a.y + t * (b.y - a.y);
        }
    }
    return best;
}

constexpr float kSampleMaxBelow = 2.5f;
constexpr float kSampleMaxAbove = 1.0f;

bool withinSampleWindow(float surfaceY, float pointY, float maxBelow) {
    return surfaceY <= pointY + kSampleMaxAbove && surfaceY >= pointY - maxBelow;
}

}

float navHeightAt(const NavMeshHeightField& heightField, int poly, float x, float z, float fallbackY) {
    if (poly < 0 || poly >= static_cast<int>(heightField.polyDetail.size())) {
        return fallbackY;
    }
    for (const std::array<Vector3, 3>& tri : heightField.polyDetail[static_cast<std::size_t>(poly)].triangles) {
        float u, v, w;
        if (baryXZ(tri[0], tri[1], tri[2], x, z, u, v, w)) {
            return u * tri[0].y + v * tri[1].y + w * tri[2].y;
        }
    }
    return fallbackY;
}

MapNavigation buildMapNavigationFromPolyMesh(
    const NavPolyMesh& polyMesh,
    const std::vector<Brush>& brushes,
    NavMeshHeightField* heightFieldOut) {
    MapNavigation nav;
    const int n = static_cast<int>(polyMesh.polys.size());
    nav.leafCount = n;
    if (heightFieldOut != nullptr) {
        heightFieldOut->polyDetail = polyMesh.polyDetail;
    }
    if (n <= 0) {
        return nav;
    }

    nav.walkable.assign(static_cast<std::size_t>(n), true);
    nav.leafIsWater.assign(static_cast<std::size_t>(n), false);
    nav.leafCentroids.resize(static_cast<std::size_t>(n));
    nav.leafFloorY.resize(static_cast<std::size_t>(n));
    nav.leafCeilingY.resize(static_cast<std::size_t>(n));
    nav.leafBoundary.resize(static_cast<std::size_t>(n));
    nav.adjacency.assign(static_cast<std::size_t>(n), {});

    const NavBakeParams defaultParams{};

    for (int i = 0; i < n; ++i) {
        const NavBakePoly& poly = polyMesh.polys[static_cast<std::size_t>(i)];
        const Vector3 centroid = polyCentroid(poly.vertices);
        nav.leafCentroids[static_cast<std::size_t>(i)] = centroid;
        nav.leafBoundary[static_cast<std::size_t>(i)] = poly.vertices;

        float floorY = centroid.y;
        if (i < static_cast<int>(polyMesh.polyDetail.size())) {
            NavMeshHeightField probe;
            probe.polyDetail = {polyMesh.polyDetail[static_cast<std::size_t>(i)]};
            floorY = navHeightAt(probe, 0, centroid.x, centroid.z, centroid.y);
        }
        nav.leafFloorY[static_cast<std::size_t>(i)] = floorY;
        // No BSP-leaf-style solid ceiling exists for a navmesh polygon (it's a 2.5D floor
        // patch, not a bounded volume); nothing outside debug tooling (nav_script.cpp's
        // nav-leaf-ceiling binding) reads this today, so an agent-height-sized placeholder
        // above the floor is a safe stand-in rather than a real measurement.
        nav.leafCeilingY[static_cast<std::size_t>(i)] = floorY + defaultParams.agentHeight;

        nav.leafIsWater[static_cast<std::size_t>(i)] =
            waterBrushContains(brushes, {centroid.x, floorY, centroid.z});
    }

    for (int i = 0; i < n; ++i) {
        const NavBakePoly& poly = polyMesh.polys[static_cast<std::size_t>(i)];
        const int vertCount = static_cast<int>(poly.vertices.size());
        for (int j = 0; j < vertCount; ++j) {
            const int neighbor = poly.neighbors[static_cast<std::size_t>(j)];
            if (neighbor < 0 || neighbor >= n) {
                continue;
            }
            const Vector3& a = poly.vertices[static_cast<std::size_t>(j)];
            const Vector3& b = poly.vertices[static_cast<std::size_t>((j + 1) % vertCount)];
            const Vector3 edgeCenter = Vector3Scale(Vector3Add(a, b), 0.5f);
            const float cost = Vector3Distance(nav.leafCentroids[static_cast<std::size_t>(i)], edgeCenter) +
                Vector3Distance(edgeCenter, nav.leafCentroids[static_cast<std::size_t>(neighbor)]);
            const PortalSpread spread = computePortalHorizontalSpread({a, b});
            const std::string doorBrushId = doorBrushIdAtPoint(edgeCenter, brushes);

            // climbHeight left at its default (0): Recast only ever links two polygons here
            // when their real voxel-level step was already within the bake's walkableClimb
            // (NavBakeParams::agentMaxClimb, nav_bake.cpp), so this edge is climb-verified by
            // construction. Re-deriving a rise from nav.leafFloorY would be actively wrong on
            // a polygon Recast merged across a stair run -- that scalar is sampled once at the
            // polygon's centroid, which can sit far in height from this specific edge (see
            // NavPortalLink::climbHeight's doc comment).
            nav.adjacency[static_cast<std::size_t>(i)].push_back(
                NavPortalLink{neighbor, edgeCenter, spread.tangent, spread.halfWidth, cost, doorBrushId});
        }
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
                    link.doorBrushId, link.climbHeight});
        }
    }

    buildNavPolyIndex(nav);
    return nav;
}

void buildNavPolyIndex(MapNavigation& nav) {
    NavPolyIndex index{};
    nav.polyIndex = index;
    if (nav.leafCount <= 0 || nav.leafBoundary.size() != static_cast<std::size_t>(nav.leafCount)) {
        return;
    }
    float minX = std::numeric_limits<float>::infinity();
    float minZ = std::numeric_limits<float>::infinity();
    float maxX = -std::numeric_limits<float>::infinity();
    float maxZ = -std::numeric_limits<float>::infinity();
    for (const std::vector<Vector3>& boundary : nav.leafBoundary) {
        for (const Vector3& v : boundary) {
            minX = std::min(minX, v.x);
            minZ = std::min(minZ, v.z);
            maxX = std::max(maxX, v.x);
            maxZ = std::max(maxZ, v.z);
        }
    }
    if (!(minX <= maxX) || !(minZ <= maxZ)) {
        return;
    }
    const auto cellOf = [&](float v) { return static_cast<int>(std::floor(v / index.cellSize)); };
    index.minCellX = cellOf(minX);
    index.minCellZ = cellOf(minZ);
    index.width = cellOf(maxX) - index.minCellX + 1;
    index.height = cellOf(maxZ) - index.minCellZ + 1;
    index.leafCount = nav.leafCount;
    const std::size_t cellCount = static_cast<std::size_t>(index.width) * static_cast<std::size_t>(index.height);
    std::vector<int> counts(cellCount, 0);
    struct Span {
        int x0, z0, x1, z1;
    };
    std::vector<Span> spans(static_cast<std::size_t>(nav.leafCount), Span{0, 0, -1, -1});
    for (int i = 0; i < nav.leafCount; ++i) {
        const std::vector<Vector3>& boundary = nav.leafBoundary[static_cast<std::size_t>(i)];
        if (boundary.empty()) {
            continue;
        }
        float px0 = boundary[0].x, pz0 = boundary[0].z, px1 = px0, pz1 = pz0;
        for (const Vector3& v : boundary) {
            px0 = std::min(px0, v.x);
            pz0 = std::min(pz0, v.z);
            px1 = std::max(px1, v.x);
            pz1 = std::max(pz1, v.z);
        }
        Span& span = spans[static_cast<std::size_t>(i)];
        span.x0 = cellOf(px0) - index.minCellX;
        span.z0 = cellOf(pz0) - index.minCellZ;
        span.x1 = cellOf(px1) - index.minCellX;
        span.z1 = cellOf(pz1) - index.minCellZ;
        for (int z = span.z0; z <= span.z1; ++z) {
            for (int x = span.x0; x <= span.x1; ++x) {
                ++counts[static_cast<std::size_t>(z * index.width + x)];
            }
        }
    }
    index.cellStart.assign(cellCount + 1, 0);
    for (std::size_t c = 0; c < cellCount; ++c) {
        index.cellStart[c + 1] = index.cellStart[c] + counts[c];
    }
    index.cellPolys.assign(static_cast<std::size_t>(index.cellStart[cellCount]), -1);
    std::vector<int> fill(index.cellStart.begin(), index.cellStart.end() - 1);
    for (int i = 0; i < nav.leafCount; ++i) {
        const Span& span = spans[static_cast<std::size_t>(i)];
        for (int z = span.z0; z <= span.z1; ++z) {
            for (int x = span.x0; x <= span.x1; ++x) {
                index.cellPolys[static_cast<std::size_t>(fill[static_cast<std::size_t>(z * index.width + x)]++)] = i;
            }
        }
    }
    nav.polyIndex = std::move(index);
}

namespace {

template <typename Visit>
void forEachPolyNear(const MapNavigation& nav, float x0, float z0, float x1, float z1, Visit&& visit) {
    const NavPolyIndex& index = nav.polyIndex;
    if (!index.matches(nav.leafCount)) {
        for (int i = 0; i < nav.leafCount; ++i) {
            visit(i);
        }
        return;
    }
    const auto cellOf = [&](float v) { return static_cast<int>(std::floor(v / index.cellSize)); };
    const int cx0 = std::max(cellOf(x0) - index.minCellX, 0);
    const int cz0 = std::max(cellOf(z0) - index.minCellZ, 0);
    const int cx1 = std::min(cellOf(x1) - index.minCellX, index.width - 1);
    const int cz1 = std::min(cellOf(z1) - index.minCellZ, index.height - 1);
    for (int z = cz0; z <= cz1; ++z) {
        for (int x = cx0; x <= cx1; ++x) {
            const std::size_t cell = static_cast<std::size_t>(z * index.width + x);
            for (int k = index.cellStart[cell]; k < index.cellStart[cell + 1]; ++k) {
                visit(index.cellPolys[static_cast<std::size_t>(k)]);
            }
        }
    }
}


int samplePolyInWindow(const MapNavigation& nav, Vector3 point, int preferLeaf, float maxBelow) {
    if (nav.leafBoundary.size() != static_cast<std::size_t>(nav.leafCount)) {
        return -1; // BSP-leaf-built graph: no boundary data, caller should fall back
    }
    // How close two candidates' floor distances have to be for preferLeaf to win the tie
    // instead of the strict closest-floor winner -- see navSamplePoly's doc comment on the
    // overlapping-polygon case this resolves. An agent stuck bouncing against a step it
    // can't quite climb doesn't just jitter by a couple centimeters: real captures show a
    // vertical bounce of a third of a world unit or more per replan cycle while genuinely
    // stuck in place, well past a naively "safe-looking" margin. What actually bounds this
    // safely is that two polygons Recast ever connects as adjacent steps differ in floor
    // height by at most the bake's walkableClimb (real single-step rise, typically ~0.5 in
    // this game's nav profiles) -- so a margin comfortably above that still can't mistake a
    // real multi-step transition for jitter, while reliably absorbing an unconnected,
    // meters-apart overlap pocket like the one above (see navSamplePoly's doc comment).
    constexpr float kPreferLeafMargin = 1.0f;
    int best = -1;
    float bestFloorDist = 0.0f;
    bool preferIsCandidate = false;
    float preferFloorDist = 0.0f;
    forEachPolyNear(nav, point.x, point.z, point.x, point.z, [&](int i) {
        if (!nav.walkable[static_cast<std::size_t>(i)]) {
            return;
        }
        const std::vector<Vector3>& boundary = nav.leafBoundary[static_cast<std::size_t>(i)];
        if (boundary.empty() || !pointInsideConvexPolygonXZ(boundary, point.x, point.z)) {
            return;
        }
        const float surfaceY =
            polySurfaceYAt(boundary, point.x, point.z, nav.leafFloorY[static_cast<std::size_t>(i)]);
        if (!withinSampleWindow(surfaceY, point.y, maxBelow)) {
            return;
        }
        const float floorDist = std::fabs(surfaceY - point.y);
        if (i == preferLeaf) {
            preferIsCandidate = true;
            preferFloorDist = floorDist;
        }
        if (best < 0 || floorDist < bestFloorDist) {
            best = i;
            bestFloorDist = floorDist;
        }
    });
    if (preferIsCandidate && preferLeaf != best && preferFloorDist <= bestFloorDist + kPreferLeafMargin) {
        return preferLeaf;
    }
    return best;
}

}

int navSamplePoly(const MapNavigation& nav, Vector3 point, int preferLeaf) {
    return samplePolyInWindow(nav, point, preferLeaf, kSampleMaxBelow);
}

int nearestWalkableNavPoly(const MapNavigation& nav, Vector3 point, float maxSnapDistance) {
    if (nav.leafBoundary.size() != static_cast<std::size_t>(nav.leafCount)) {
        return -1;
    }
    int best = -1;
    float bestDist = maxSnapDistance;
    forEachPolyNear(
        nav,
        point.x - maxSnapDistance,
        point.z - maxSnapDistance,
        point.x + maxSnapDistance,
        point.z + maxSnapDistance,
        [&](int i) {
            if (!nav.walkable[static_cast<std::size_t>(i)]) {
                return;
            }
            const std::vector<Vector3>& boundary = nav.leafBoundary[static_cast<std::size_t>(i)];
            if (boundary.size() < 3) {
                return;
            }
            const PolyEdgeSnap snap = closestEdgePointXZ(boundary, point.x, point.z);
            if (snap.dist < bestDist && withinSampleWindow(snap.y, point.y, kSampleMaxBelow)) {
                best = i;
                bestDist = snap.dist;
            }
        });
    return best;
}

int sampleNavLeaf(const MapNavigation& nav, const BspTree& tree, Vector3 point, int preferLeaf) {
    if (nav.leafBoundary.size() != static_cast<std::size_t>(nav.leafCount)) {
        // BSP-leaf-built graph: no boundary data, nav indices already are BSP leaf ids.
        return pvsSampleLeaf(tree, point);
    }
    const int poly = navSamplePoly(nav, point, preferLeaf);
    if (poly >= 0) {
        return poly;
    }
    // Baked navmesh graph but the point missed every polygon's containment test -- most
    // often the eroded margin Recast leaves along a wall or doorway (see buildNavPolyMesh's
    // walkableRadius erosion), which is exactly where an agent's feet sample lands while
    // passing through a chokepoint. Snap to the nearest walkable polygon rather than
    // falling back to pvsSampleLeaf: a raw BSP leaf id would silently be fed into this
    // nav's poly-indexed tables (leafFloorY, adjacency, ...) as a wrong-space index.
    constexpr float kMaxSnapDistance = 1.0f;
    const int snapped = nearestWalkableNavPoly(nav, point, kMaxSnapDistance);
    if (snapped >= 0) {
        return snapped;
    }
    return samplePolyInWindow(nav, point, preferLeaf, std::numeric_limits<float>::infinity());
}

}
