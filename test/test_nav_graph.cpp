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
        // A waypoint must never be marked completed off vertical position alone:
        // an agent standing on a higher floor elsewhere on the map (here 7 units
        // away horizontally) is not "past" this waypoint just because its Y is
        // higher. Only horizontal arrival (or, with real leaf data, having
        // actually entered the target leaf) counts as completion.
        const std::vector<Vector3> waypoints = {{0.0f, 4.0f, 0.0f}};
        const std::vector<int> leafPath = {1};
        const std::vector<int> waypointToLeaf = {1};
        CHECK_FALSE(navWaypointCompleted(
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
        // A real Door brush filling the doorway (not just a role change on the
        // jambs) must still connect the two rooms in the portal graph, and the
        // link must be gated by that door's live open/closed state.
        const std::vector<Brush> brushes = mapfixtures::sealedRoomWithInteriorDoor();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);

        const MapNavigation nav = buildMapNavigation(tree, &analysis.exteriorEmpty);
        const std::int32_t north = pointLeaf(tree, {0.0f, 1.0f, -3.0f});
        const std::int32_t south = pointLeaf(tree, {0.0f, 1.0f, 3.0f});
        CHECK(north >= 0);
        CHECK(south >= 0);
        CHECK(nav.walkable[static_cast<std::size_t>(north)]);
        CHECK(nav.walkable[static_cast<std::size_t>(south)]);

        const std::vector<int> pathUngated = findLeafPath(nav, north, south);
        CHECK(pathUngated.size() >= 2);

        const DoorOpenQuery open = [](const std::string&) { return true; };
        const std::vector<int> pathOpen = findLeafPath(nav, north, south, open);
        CHECK(pathOpen.size() >= 2);

        const DoorOpenQuery closed = [](const std::string&) { return false; };
        const std::vector<int> pathClosed = findLeafPath(nav, north, south, closed);
        CHECK(pathClosed.empty());
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

    {
        // Reported floor height must reflect real solid ground, not wherever a
        // leaf's own AABB happens to bottom out — a leaf that's merely an
        // upper slice of one open room (phantom-split by an unrelated brush's
        // face plane) must resolve to the same floor as the leaf beneath it.
        const std::vector<Brush> brushes = mapfixtures::tallRoomWithDistantHorizontalSplitter();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);

        const MapNavigation nav = buildMapNavigation(tree, &analysis.exteriorEmpty);

        const std::int32_t low = pointLeaf(tree, {0.0f, 0.5f, 0.0f});
        const std::int32_t high = pointLeaf(tree, {0.0f, 3.5f, 0.0f});
        CHECK(low >= 0);
        CHECK(high >= 0);
        CHECK(nav.walkable[static_cast<std::size_t>(low)]);
        CHECK(nav.walkable[static_cast<std::size_t>(high)]);
        // The fixture only proves anything if the phantom split actually
        // happened, i.e. the low and high probes landed in different leaves.
        CHECK(low != high);

        CHECK_EQ(nav.leafFloorY[static_cast<std::size_t>(low)], 0.0f);
        CHECK_EQ(nav.leafFloorY[static_cast<std::size_t>(high)], 0.0f);
    }

    {
        const std::vector<Brush> brushes = mapfixtures::sealedRoomWithInteriorDoor();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);

        const MapNavigation nav = buildMapNavigation(tree, &analysis.exteriorEmpty);
        CHECK_EQ(static_cast<int>(nav.reverseAdjacency.size()), nav.leafCount);

        const auto edgeCost = [&nav](int a, int b) -> float {
            for (const NavPortalLink& link : nav.adjacency[static_cast<std::size_t>(a)]) {
                if (link.neighborLeaf == b) {
                    return link.cost;
                }
            }
            return -1.0f;
        };
        const auto pathCost = [&](const std::vector<int>& path) -> float {
            float total = 0.0f;
            for (std::size_t i = 0; i + 1 < path.size(); ++i) {
                total += edgeCost(path[i], path[i + 1]);
            }
            return total;
        };
        const auto checkEquivalentRoute = [&](const std::vector<int>& fieldPath,
                                               const std::vector<int>& astarPath,
                                               int from,
                                               int goal) {
            CHECK_EQ(fieldPath.empty(), astarPath.empty());
            if (fieldPath.empty() || astarPath.empty()) {
                return;
            }
            CHECK_EQ(fieldPath.front(), from);
            CHECK_EQ(fieldPath.back(), goal);
            CHECK(std::fabs(pathCost(fieldPath) - pathCost(astarPath)) <= 1.0e-3f);
        };

        const DoorOpenQuery openQuery = [](const std::string&) { return true; };
        const DoorOpenQuery closedQuery = [](const std::string&) { return false; };

        for (int goal = 0; goal < nav.leafCount; ++goal) {
            if (!nav.walkable[static_cast<std::size_t>(goal)]) {
                continue;
            }
            const NavFlowField fieldOpen = buildNavFlowField(nav, goal, openQuery);
            const NavFlowField fieldClosed = buildNavFlowField(nav, goal, closedQuery);

            for (int from = 0; from < nav.leafCount; ++from) {
                if (!nav.walkable[static_cast<std::size_t>(from)]) {
                    continue;
                }
                checkEquivalentRoute(
                    flowFieldPathFrom(fieldOpen, from),
                    findLeafPath(nav, from, goal, openQuery),
                    from,
                    goal);
                checkEquivalentRoute(
                    flowFieldPathFrom(fieldClosed, from),
                    findLeafPath(nav, from, goal, closedQuery),
                    from,
                    goal);
            }
        }

        const NavFlowField validGoalField = buildNavFlowField(nav, 0, openQuery);
        CHECK(flowFieldPathFrom(validGoalField, -1).empty());
        const NavFlowField invalidGoalField = buildNavFlowField(nav, -1, openQuery);
        CHECK(flowFieldPathFrom(invalidGoalField, 0).empty());
    }

    {
        const std::vector<Brush> brushes = mapfixtures::sealedHollowRoomWithStairs();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);

        const MapNavigation nav = buildMapNavigation(tree, &analysis.exteriorEmpty);

        bool foundStep = false;
        for (int leafA = 0; leafA < nav.leafCount && !foundStep; ++leafA) {
            if (!nav.walkable[static_cast<std::size_t>(leafA)]) {
                continue;
            }
            for (const NavPortalLink& link : nav.adjacency[static_cast<std::size_t>(leafA)]) {
                const int leafB = link.neighborLeaf;
                if (leafB < 0 || !nav.walkable[static_cast<std::size_t>(leafB)]) {
                    continue;
                }
                const float floorA = nav.leafFloorY[static_cast<std::size_t>(leafA)];
                const float floorB = nav.leafFloorY[static_cast<std::size_t>(leafB)];
                const float rise = floorB - floorA;
                if (std::fabs(rise) <= 1.0e-3f) {
                    continue;
                }

                const int lower = rise > 0.0f ? leafA : leafB;
                const int higher = rise > 0.0f ? leafB : leafA;
                const float tinyClimb = std::fabs(rise) * 0.5f;

                const NavFlowField toLower = buildNavFlowField(nav, lower, {}, tinyClimb);
                const std::vector<int> descentField = flowFieldPathFrom(toLower, higher);
                const std::vector<int> descentAstar =
                    findLeafPath(nav, higher, lower, {}, tinyClimb);
                CHECK_FALSE(descentAstar.empty());
                CHECK_FALSE(descentField.empty());

                const NavFlowField toHigher = buildNavFlowField(nav, higher, {}, tinyClimb);
                const std::vector<int> climbField = flowFieldPathFrom(toHigher, lower);
                const std::vector<int> climbAstar = findLeafPath(nav, lower, higher, {}, tinyClimb);
                CHECK_EQ(climbField.empty(), climbAstar.empty());

                foundStep = true;
                break;
            }
        }
        CHECK(foundStep);
    }
}

}
