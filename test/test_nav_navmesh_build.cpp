#include "test_assert.hpp"
#include "map_fixtures.hpp"

#include "map/bsp.hpp"
#include "map/bsp_analyze.hpp"
#include "map/nav_bake.hpp"
#include "map/nav_navmesh_build.hpp"

#include <cmath>
#include <limits>

namespace slopengine {

namespace {

Vector3 polyCentroidOf(const MapNavigation& nav, int leaf) {
    return nav.leafCentroids[static_cast<std::size_t>(leaf)];
}

int closestLeafByZ(const MapNavigation& nav, float targetZ) {
    int best = -1;
    float bestDist = std::numeric_limits<float>::infinity();
    for (int i = 0; i < nav.leafCount; ++i) {
        const float d = std::fabs(polyCentroidOf(nav, i).z - targetZ);
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

}

void runNavNavmeshBuildTests() {
    {
        const std::vector<Brush> brushes = mapfixtures::straightStaircaseHallway(13);
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);
        const std::optional<NavPolyMesh> polyMesh = buildNavPolyMesh(tree, brushes, &analysis.exteriorEmpty);
        CHECK(polyMesh.has_value());
        if (!polyMesh.has_value()) {
            return;
        }

        NavMeshHeightField heightField;
        const MapNavigation nav = buildMapNavigationFromPolyMesh(*polyMesh, brushes, &heightField);
        CHECK_EQ(nav.leafCount, static_cast<int>(polyMesh->polys.size()));
        CHECK_EQ(heightField.polyDetail.size(), polyMesh->polys.size());

        const int startLeaf = closestLeafByZ(nav, -2.0f);
        const int endLeaf = closestLeafByZ(nav, 12.4f);
        CHECK(startLeaf >= 0);
        CHECK(endLeaf >= 0);

        // No maxClimb here, unlike the BSP-leaf builder's usual call pattern: Recast's
        // own walkableClimb config already guaranteed every adjacent-polygon step is
        // within the agent's real step height at bake time (that's the whole point of
        // baking to the agent's shape), so re-checking a coarse leaf-centroid floor
        // delta on top of that is not just redundant, it's actively wrong -- a single
        // merged polygon can legitimately span an entire staircase's rise, so its
        // centroid-to-centroid "climb" no longer corresponds to one physical step the
        // way it did for a BSP leaf per tread.
        const std::vector<int> path = findLeafPath(nav, startLeaf, endLeaf);
        CHECK_FALSE(path.empty());
        if (path.empty()) {
            return;
        }

        const Vector3 goalPos = polyCentroidOf(nav, endLeaf);
        const std::vector<Vector3> waypoints = leafPathToWaypoints(nav, path, goalPos);
        CHECK_FALSE(waypoints.empty());
        if (waypoints.empty()) {
            return;
        }
        CHECK(waypoints.back().y > waypoints.front().y);

        // The routing graph is now coarse (one polygon can cover the whole run), so
        // per-step ground height during actual movement has to come from the detail
        // mesh (navHeightAt), not from waypoint density. Confirm it really does track
        // the stairs rising rather than staying flat across the poly's footprint.
        const float lowY = navHeightAt(heightField, startLeaf, 0.0f, 0.5f, -1000.0f);
        const float highY = navHeightAt(heightField, endLeaf, 0.0f, 10.0f, -1000.0f);
        CHECK(highY > lowY + 4.0f);
    }

    {
        const std::vector<Brush> brushes = mapfixtures::sealedRoomWithInteriorDoor();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);
        const std::optional<NavPolyMesh> polyMesh = buildNavPolyMesh(tree, brushes, &analysis.exteriorEmpty);
        CHECK(polyMesh.has_value());
        if (!polyMesh.has_value()) {
            return;
        }

        const MapNavigation nav = buildMapNavigationFromPolyMesh(*polyMesh, brushes);

        bool foundDoorEdge = false;
        for (const auto& links : nav.adjacency) {
            for (const NavPortalLink& link : links) {
                if (link.doorBrushId == "door-1") {
                    foundDoorEdge = true;
                }
            }
        }
        CHECK(foundDoorEdge);

        const int northLeaf = closestLeafByZ(nav, -3.0f);
        const int southLeaf = closestLeafByZ(nav, 3.0f);
        const std::vector<int> path = findLeafPath(nav, northLeaf, southLeaf);
        CHECK_FALSE(path.empty());

        // navSamplePoly should find the same leaf a centroid-based lookup already
        // trusts, and correctly reject a point that's nowhere near any polygon.
        CHECK(!nav.leafBoundary.empty());
        const Vector3 northCentroid = polyCentroidOf(nav, northLeaf);
        CHECK_EQ(navSamplePoly(nav, northCentroid), northLeaf);
        CHECK_EQ(navSamplePoly(nav, Vector3{500.0f, 0.0f, 500.0f}), -1);
    }

    {
        // A BSP-leaf-built MapNavigation (no leafBoundary data) must make
        // navSamplePoly fail closed, not read past an empty leafBoundary vector.
        MapNavigation bspStyleNav;
        bspStyleNav.leafCount = 1;
        bspStyleNav.walkable = {true};
        bspStyleNav.leafCentroids = {Vector3{0.0f, 0.0f, 0.0f}};
        bspStyleNav.leafFloorY = {0.0f};
        CHECK_EQ(navSamplePoly(bspStyleNav, Vector3{0.0f, 0.0f, 0.0f}), -1);
    }
}

}
