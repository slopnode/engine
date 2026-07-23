#include "test_assert.hpp"
#include "map_fixtures.hpp"

#include "map/bsp.hpp"
#include "map/bsp_analyze.hpp"
#include "map/pvs.hpp"
#include "map/pvs_io.hpp"

#include <filesystem>
#include <vector>

namespace slopengine {

void runPvsBuildTests() {
    {
        const std::vector<Brush> brushes = mapfixtures::sealedRoomWithInteriorDoorway();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);

        const PvsFile pvs = buildPvs(tree, &analysis.exteriorEmpty);
        CHECK(pvs.leafCount == static_cast<int>(tree.leaves.size()));
        CHECK(pvs.wordsPerRow > 0);
        CHECK_FALSE(pvs.bits.empty());

        const std::int32_t north = pointLeaf(tree, {0.0f, 1.0f, -3.0f});
        const std::int32_t south = pointLeaf(tree, {0.0f, 1.0f, 3.0f});
        CHECK(north >= 0);
        CHECK(south >= 0);
        CHECK(leafIsOpen(tree.leaves[static_cast<std::size_t>(north)].contents));
        CHECK(leafIsOpen(tree.leaves[static_cast<std::size_t>(south)].contents));
        CHECK(pvsCanSee(pvs, north, north));
        CHECK(pvsCanSee(pvs, south, south));
        CHECK(pvsCanSee(pvs, north, south));
        CHECK(pvsCanSee(pvs, south, north));
    }

    {
        std::vector<Brush> brushes = mapfixtures::sealedHollowRoom();
        brushes.push_back(makeBrushBox(
            "wall-mid",
            {-1.75f, 0.0f, -0.15f},
            {1.75f, 2.5f, 0.15f},
            "mat/a",
            {}));
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);

        const PvsFile pvs = buildPvs(tree, &analysis.exteriorEmpty);
        const std::int32_t north = pointLeaf(tree, {0.0f, 1.0f, -1.0f});
        const std::int32_t south = pointLeaf(tree, {0.0f, 1.0f, 1.0f});
        CHECK(north >= 0);
        CHECK(south >= 0);
        CHECK(north != south);
        CHECK(pvsCanSee(pvs, north, north));
        CHECK(pvsCanSee(pvs, south, south));
        CHECK_FALSE(pvsCanSee(pvs, north, south));
        CHECK_FALSE(pvsCanSee(pvs, south, north));
    }

    {
        const std::vector<Brush> brushes = mapfixtures::sealedHollowRoom();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const PvsFile pvs = buildPvs(tree);
        std::vector<std::byte> bytes;
        {
            const auto path = std::filesystem::temp_directory_path() / "slop_pvs_test.vis";
            CHECK(writePvsFile(path, pvs));
            auto loaded = readPvsFile(path);
            CHECK(loaded.has_value());
            CHECK(loaded->leafCount == pvs.leafCount);
            CHECK(loaded->wordsPerRow == pvs.wordsPerRow);
            CHECK(loaded->bits == pvs.bits);
            std::filesystem::remove(path);
        }
    }
}

}
