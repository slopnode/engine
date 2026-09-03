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
        // Standing inside the Door leaf itself (e.g. mid-doorway at eye height)
        // must resolve the camera's PVS sample to that leaf directly rather
        // than nudging upward past its low ceiling: an upward nudge can escape
        // into unrelated, poorly-connected geometry (regression covered here by
        // asserting the sample lands on the Door leaf, not -1 or some other
        // leaf reached by the nudge search).
        const std::vector<Brush> brushes = mapfixtures::sealedRoomWithInteriorDoor();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        const PvsFile pvs = buildPvs(tree, &analysis.exteriorEmpty);
        const std::int32_t doorLeaf = pointLeaf(tree, {0.0f, 1.0f, 0.0f});
        CHECK(doorLeaf >= 0);
        CHECK((tree.leaves[static_cast<std::size_t>(doorLeaf)].contents & BspContents::Door) != 0);

        for (float eyeY : {1.0f, 1.7f}) {
            const Vector3 camPos{0.0f, eyeY, 0.0f};
            CHECK(pointLeaf(tree, camPos) == doorLeaf);
            CHECK(pvsSampleLeaf(tree, camPos) == doorLeaf);
            CHECK(pvsVisiblePoints(tree, pvs, camPos, {0.0f, 1.0f, -3.0f}));
            CHECK(pvsVisiblePoints(tree, pvs, camPos, {0.0f, 1.0f, 3.0f}));
        }
    }

    {
        // A real Door brush filling the doorway must not sever PVS
        // connectivity between the rooms it separates: PVS is a static,
        // precomputed set with no live door-state query, so it has to stay
        // conservatively visible through a doorway regardless of whether the
        // door happens to be open or closed at runtime. Otherwise anything
        // behind an open door gets permanently culled.
        const std::vector<Brush> brushes = mapfixtures::sealedRoomWithInteriorDoor();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);

        const PvsFile pvs = buildPvs(tree, &analysis.exteriorEmpty);
        const std::int32_t north = pointLeaf(tree, {0.0f, 1.0f, -3.0f});
        const std::int32_t south = pointLeaf(tree, {0.0f, 1.0f, 3.0f});
        CHECK(north >= 0);
        CHECK(south >= 0);
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
