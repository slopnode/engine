#include "test_assert.hpp"
#include "map_fixtures.hpp"

#include "map/bsp.hpp"
#include "map/bsp_analyze.hpp"
#include "map/nav_graph.hpp"

#include <cmath>

namespace slopengine {

void runNavGraphTests() {
    {
        const std::vector<Vector3> waypoints = {
            {0.0f, 0.0f, 0.0f},
            {5.0f, 0.0f, 0.0f},
            {10.0f, 0.0f, 0.0f},
        };
        const Vector3 agentNear0 = {0.2f, 0.0f, 0.0f};
        CHECK_EQ(findResumeWaypointIndex(waypoints, agentNear0, 0.75f), 1);
    }

    {
        const std::vector<Vector3> waypoints = {
            {0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            {10.0f, 0.0f, 0.0f},
        };
        const Vector3 agentPastFirstTwo = {0.5f, 0.0f, 0.0f};
        CHECK_EQ(findResumeWaypointIndex(waypoints, agentPastFirstTwo, 0.75f), 2);
    }

    {
        const std::vector<Vector3> waypoints = {
            {0.0f, 0.0f, 0.0f},
            {5.0f, 0.0f, 0.0f},
        };
        const Vector3 agentFar = {20.0f, 0.0f, 0.0f};
        CHECK_EQ(findResumeWaypointIndex(waypoints, agentFar, 0.75f), 0);
    }

    {
        const std::vector<Vector3> waypoints = {{1.0f, 0.0f, 1.0f}};
        CHECK_EQ(findResumeWaypointIndex(waypoints, {1.0f, 0.0f, 1.0f}, 0.75f), 0);
    }

    {
        const std::vector<Vector3> empty{};
        CHECK_EQ(findResumeWaypointIndex(empty, {0.0f, 0.0f, 0.0f}, 0.75f), 0);
    }

    {
        const std::vector<Vector3> waypoints = {
            {0.0f, 0.0f, 0.0f},
            {5.0f, 0.0f, 0.0f},
            {10.0f, 0.0f, 0.0f},
        };
        const std::vector<int> leafPath = {10, 20, 30};
        const std::vector<int> waypointToLeaf = {20, 30, 30};
        CHECK(navWaypointCompleted(waypoints, waypointToLeaf, leafPath, {99.0f, 0.0f, 99.0f}, 20, 0, 0.75f));
        CHECK(navWaypointCompleted(waypoints, waypointToLeaf, leafPath, {99.0f, 0.0f, 99.0f}, 30, 0, 0.75f));
        CHECK(navWaypointCompleted(waypoints, waypointToLeaf, leafPath, {99.0f, 0.0f, 99.0f}, 30, 1, 0.75f));
        CHECK_FALSE(navWaypointCompleted(waypoints, waypointToLeaf, leafPath, {99.0f, 0.0f, 99.0f}, 30, 2, 0.75f));
        CHECK_FALSE(navWaypointCompleted(waypoints, waypointToLeaf, leafPath, {99.0f, 0.0f, 99.0f}, 10, 0, 0.75f));
        CHECK_EQ(
            findResumeWaypointIndex(waypoints, waypointToLeaf, leafPath, {99.0f, 0.0f, 99.0f}, 30, 0.75f),
            2);
    }

    {
        const std::vector<Vector3> waypoints = {
            {0.0f, 0.0f, 0.0f},
            {5.0f, 0.0f, 0.0f},
        };
        const std::vector<int> leafPath = {1, 2};
        const std::vector<int> waypointToLeaf = {2, 2};
        CHECK(navWaypointCompleted(waypoints, waypointToLeaf, leafPath, {20.0f, 0.0f, 0.0f}, 2, 0, 0.75f));
        CHECK_FALSE(navWaypointCompleted(waypoints, waypointToLeaf, leafPath, {20.0f, 0.0f, 0.0f}, 2, 1, 0.75f));
    }

    {
        const std::vector<Vector3> waypoints = {{10.0f, 0.0f, 0.0f}};
        const std::vector<int> leafPath = {5};
        const std::vector<int> waypointToLeaf = {5};
        CHECK_FALSE(navWaypointCompleted(
            waypoints, waypointToLeaf, leafPath, {0.0f, 0.0f, 0.0f}, 5, 0, 0.75f));
        CHECK(navWaypointCompleted(
            waypoints, waypointToLeaf, leafPath, {9.8f, 0.0f, 0.1f}, 5, 0, 0.75f));
        CHECK_EQ(
            findResumeWaypointIndex(waypoints, waypointToLeaf, leafPath, {0.0f, 0.0f, 0.0f}, 5, 0.75f),
            0);
    }

    {
        const std::vector<Vector3> waypoints = {{0.0f, 4.0f, 0.0f}};
        const std::vector<int> leafPath = {1};
        const std::vector<int> waypointToLeaf = {1};
        CHECK(navWaypointCompleted(
            waypoints, waypointToLeaf, leafPath, {5.0f, 4.3f, 5.0f}, -1, 0, 0.75f));
        CHECK(navWaypointCompleted(
            waypoints, waypointToLeaf, leafPath, {0.2f, 3.98f, 0.1f}, -1, 0, 0.75f));
        CHECK_FALSE(navWaypointCompleted(
            waypoints, waypointToLeaf, leafPath, {5.0f, 2.0f, 5.0f}, -1, 0, 0.75f));
    }

    {
        const std::vector<int> oldPath = {10, 20, 30, 40};
        const std::vector<int> newPath = {20, 30, 40};
        CHECK(navLeafPathRouteUnchanged(oldPath, 20, newPath));
        CHECK_FALSE(navLeafPathRouteUnchanged(oldPath, 10, newPath));
        CHECK(navLeafPathSuffixEqual(oldPath, 1, newPath, 0));
    }

    {
        const std::vector<Brush> brushes = mapfixtures::sealedRoomWithInteriorDoorway();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);

        const MapNavigation nav = buildMapNavigation(tree, &analysis.exteriorEmpty);
        CHECK(nav.leafCount == static_cast<int>(tree.leaves.size()));
        CHECK_FALSE(nav.walkable.empty());
        CHECK_EQ(static_cast<int>(nav.adjacency.size()), nav.leafCount);
        CHECK_EQ(static_cast<int>(nav.leafFloorY.size()), nav.leafCount);
        CHECK_EQ(static_cast<int>(nav.leafCeilingY.size()), nav.leafCount);

        const std::int32_t north = pointLeaf(tree, {0.0f, 1.0f, -3.0f});
        const std::int32_t south = pointLeaf(tree, {0.0f, 1.0f, 3.0f});
        CHECK(north >= 0);
        CHECK(south >= 0);
        CHECK(north != south);
        CHECK(nav.walkable[static_cast<std::size_t>(north)]);
        CHECK(nav.walkable[static_cast<std::size_t>(south)]);

        const std::vector<int> path = findLeafPath(nav, north, south);
        CHECK(path.size() >= 2);
        CHECK_EQ(path.front(), north);
        CHECK_EQ(path.back(), south);

        const Vector3 goalPos = {0.0f, 1.0f, 3.0f};
        const std::vector<Vector3> waypoints = leafPathToWaypoints(nav, path, goalPos);
        CHECK(waypoints.size() >= 2);
        CHECK_EQ(waypoints.back().x, goalPos.x);
        CHECK_EQ(waypoints.back().z, goalPos.z);
        CHECK_EQ(waypoints.front().y, nav.leafFloorY[static_cast<std::size_t>(path.front())]);
    }

    {
        const std::vector<Brush> brushes = mapfixtures::sealedHollowRoom();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);

        const MapNavigation nav = buildMapNavigation(tree, &analysis.exteriorEmpty);
        const std::int32_t leaf = pointLeaf(tree, {0.0f, 1.0f, 0.0f});
        CHECK(leaf >= 0);

        const std::vector<int> path = findLeafPath(nav, leaf, leaf);
        CHECK_EQ(path.size(), 1u);
        CHECK_EQ(path.front(), leaf);

        const Vector3 goalPos = {0.5f, 1.0f, 0.5f};
        const std::vector<Vector3> waypoints = leafPathToWaypoints(nav, path, goalPos);
        CHECK_EQ(waypoints.size(), 1u);
        CHECK_EQ(waypoints.front().x, goalPos.x);
        CHECK_EQ(waypoints.front().z, goalPos.z);
    }

    {
        const std::vector<Brush> brushes = mapfixtures::sealedHollowRoomWithStairs();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);

        const MapNavigation nav = buildMapNavigation(tree, &analysis.exteriorEmpty);
        CHECK(nav.walkable.size() == nav.leafFloorY.size());

        bool foundVerticalPortal = false;
        for (int leafA = 0; leafA < nav.leafCount && !foundVerticalPortal; ++leafA) {
            if (!nav.walkable[static_cast<std::size_t>(leafA)]) {
                continue;
            }
            for (const NavPortalLink& link : nav.adjacency[static_cast<std::size_t>(leafA)]) {
                const int leafB = link.neighborLeaf;
                if (leafB < 0 || !nav.walkable[static_cast<std::size_t>(leafB)]) {
                    continue;
                }
                const float floorY = nav.leafFloorY[static_cast<std::size_t>(leafA)];
                if (std::fabs(link.portalCenter.y - floorY) <= 1.0e-3f) {
                    continue;
                }

                const std::vector<int> path = {leafA, leafB};
                const Vector3 goal = nav.leafCentroids[static_cast<std::size_t>(leafB)];
                const std::vector<Vector3> waypoints = leafPathToWaypoints(nav, path, goal);
                std::vector<int> waypointToLeaf;
                buildWaypointToLeaf(path, leafB, waypointToLeaf);
                CHECK_EQ(waypoints.size(), 2u);
                CHECK_EQ(waypointToLeaf.size(), 2u);
                CHECK_EQ(waypoints.front().y, floorY);
                CHECK(std::fabs(waypoints.front().y - link.portalCenter.y) > 1.0e-3f);
                CHECK(navWaypointCompleted(
                    waypoints, waypointToLeaf, path, {999.0f, 0.0f, 999.0f}, leafB, 0, 0.75f));
                foundVerticalPortal = true;
                break;
            }
        }
        CHECK(foundVerticalPortal);
    }

    {
        const std::vector<Brush> brushes = mapfixtures::sealedHollowRoomWithStairs();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        const MapNavigation nav = buildMapNavigation(tree, &analysis.exteriorEmpty);

        bool foundVerticalPortal = false;
        for (int leafA = 0; leafA < nav.leafCount && !foundVerticalPortal; ++leafA) {
            if (!nav.walkable[static_cast<std::size_t>(leafA)]) {
                continue;
            }
            for (const NavPortalLink& link : nav.adjacency[static_cast<std::size_t>(leafA)]) {
                const int leafB = link.neighborLeaf;
                if (leafB < 0 || !nav.walkable[static_cast<std::size_t>(leafB)]) {
                    continue;
                }
                const float floorY = nav.leafFloorY[static_cast<std::size_t>(leafA)];
                if (std::fabs(link.portalCenter.y - floorY) <= 1.0e-3f) {
                    continue;
                }

                const std::vector<int> path = {leafA, leafB};
                const Vector3 goal = nav.leafCentroids[static_cast<std::size_t>(leafB)];
                const std::vector<Vector3> groundWps = leafPathToWaypoints(nav, path, goal, false);
                const std::vector<Vector3> flyWps = leafPathToWaypoints(nav, path, goal, true);
                CHECK_EQ(groundWps.size(), 2u);
                CHECK_EQ(flyWps.size(), 2u);
                CHECK(std::fabs(groundWps.front().y - floorY) <= 1.0e-3f);
                CHECK(std::fabs(flyWps.front().y - link.portalCenter.y) <= 1.0e-3f);
                if (std::fabs(link.portalCenter.y - floorY) > 1.0e-3f) {
                    CHECK(std::fabs(flyWps.front().y - groundWps.front().y) > 1.0e-3f);
                }
                foundVerticalPortal = true;
                break;
            }
        }
        CHECK(foundVerticalPortal);
    }
}

}
