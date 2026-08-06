#include "test_assert.hpp"
#include "map_fixtures.hpp"

#include "map/bsp.hpp"
#include "map/bsp_analyze.hpp"
#include "map/nav_graph.hpp"

namespace slopengine {

void runNavGraphTests() {
    {
        const std::vector<Brush> brushes = mapfixtures::sealedRoomWithInteriorDoorway();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);

        const MapNavigation nav = buildMapNavigation(tree, &analysis.exteriorEmpty);
        CHECK(nav.leafCount == static_cast<int>(tree.leaves.size()));
        CHECK_FALSE(nav.walkable.empty());
        CHECK_EQ(static_cast<int>(nav.adjacency.size()), nav.leafCount);

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
}

}
