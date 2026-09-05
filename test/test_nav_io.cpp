#include "test_assert.hpp"
#include "map_fixtures.hpp"

#include "map/bsp.hpp"
#include "map/bsp_analyze.hpp"
#include "map/nav_bake.hpp"
#include "map/nav_io.hpp"
#include "map/nav_navmesh_build.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>

namespace slopengine {

namespace {

bool navPortalLinkEqual(const NavPortalLink& a, const NavPortalLink& b) {
    constexpr float kEps = 1.0e-5f;
    return a.neighborLeaf == b.neighborLeaf && a.doorBrushId == b.doorBrushId &&
        std::fabs(a.portalCenter.x - b.portalCenter.x) < kEps &&
        std::fabs(a.portalCenter.y - b.portalCenter.y) < kEps &&
        std::fabs(a.portalCenter.z - b.portalCenter.z) < kEps &&
        std::fabs(a.portalHalfWidth - b.portalHalfWidth) < kEps &&
        std::fabs(a.cost - b.cost) < kEps &&
        std::fabs(a.climbHeight - b.climbHeight) < kEps;
}

}

void runNavIoTests() {
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
    CHECK(nav.leafCount > 0);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "sloptests_nav_io_roundtrip.nav";
    CHECK(writeNavFile(path, nav));

    const std::optional<MapNavigation> loaded = readNavFile(path);
    std::filesystem::remove(path);
    CHECK(loaded.has_value());
    if (!loaded.has_value()) {
        return;
    }

    CHECK_EQ(loaded->leafCount, nav.leafCount);
    CHECK(loaded->walkable == nav.walkable);
    CHECK(loaded->leafIsWater == nav.leafIsWater);

    CHECK_EQ(loaded->leafBoundary.size(), nav.leafBoundary.size());
    for (int i = 0; i < nav.leafCount; ++i) {
        CHECK_EQ(loaded->adjacency[static_cast<std::size_t>(i)].size(), nav.adjacency[static_cast<std::size_t>(i)].size());
        for (std::size_t j = 0; j < nav.adjacency[static_cast<std::size_t>(i)].size(); ++j) {
            CHECK(navPortalLinkEqual(
                loaded->adjacency[static_cast<std::size_t>(i)][j],
                nav.adjacency[static_cast<std::size_t>(i)][j]));
        }
        CHECK_EQ(
            loaded->reverseAdjacency[static_cast<std::size_t>(i)].size(),
            nav.reverseAdjacency[static_cast<std::size_t>(i)].size());

        const auto& boundary = nav.leafBoundary[static_cast<std::size_t>(i)];
        const auto& loadedBoundary = loaded->leafBoundary[static_cast<std::size_t>(i)];
        CHECK_EQ(loadedBoundary.size(), boundary.size());
        for (std::size_t j = 0; j < boundary.size() && j < loadedBoundary.size(); ++j) {
            CHECK(std::fabs(boundary[j].x - loadedBoundary[j].x) < 1.0e-5f);
            CHECK(std::fabs(boundary[j].y - loadedBoundary[j].y) < 1.0e-5f);
            CHECK(std::fabs(boundary[j].z - loadedBoundary[j].z) < 1.0e-5f);
        }
    }

    bool foundDoorEdge = false;
    for (const auto& links : loaded->adjacency) {
        for (const NavPortalLink& link : links) {
            if (link.doorBrushId == "door-1") {
                foundDoorEdge = true;
            }
        }
    }
    CHECK(foundDoorEdge);

    // Reading garbage / wrong-magic data should fail cleanly, not crash.
    const std::vector<std::byte> junk(8, std::byte{0xAB});
    CHECK_FALSE(readNavBytes(junk).has_value());
}

}
