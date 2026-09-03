#include "test_assert.hpp"
#include "map_fixtures.hpp"

#include "map/bsp.hpp"
#include "map/bsp_analyze.hpp"
#include "map/brush.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace slopengine {

void runBspBuildTests() {
    {
        const std::vector<Brush> brushes = mapfixtures::sealedHollowRoom();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        CHECK(tree.root >= 0);
        CHECK_FALSE(tree.nodes.empty());
        CHECK_FALSE(tree.leaves.empty());
        CHECK(tree.portals.empty());

        const std::int32_t interiorLeaf = pointLeaf(tree, {0.0f, 1.25f, 0.0f});
        CHECK(interiorLeaf >= 0);
        CHECK(leafIsEmpty(tree, interiorLeaf));

        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);

        const std::int32_t exteriorLeaf = pointLeaf(
            tree,
            {tree.boundsMins.x + 0.1f, tree.boundsMins.y + 0.1f, tree.boundsMins.z + 0.1f});
        CHECK(exteriorLeaf >= 0);
        CHECK(leafIsEmpty(tree, exteriorLeaf));
        CHECK(static_cast<std::size_t>(exteriorLeaf) < analysis.exteriorEmpty.size());
        CHECK_EQ(analysis.exteriorEmpty[static_cast<std::size_t>(exteriorLeaf)], 1u);
    }

    {
        const std::vector<Brush> brushes = mapfixtures::leakyHollowRoom();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        CHECK(tree.root >= 0);
        CHECK_FALSE(tree.leaves.empty());
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK_FALSE(analysis.sealed);
    }

    {
        // buildBspFromHullBrushes's optional surfaceBrushes must only affect
        // emitted surface geometry -- tree structure/solidity classification has
        // to come from the primary (uncarved) brushes list. pointInsideBrush
        // requires every one of a brush's faces to bound it, so a brush missing
        // a face (as carveBrushes produces for an embedded face) describes an
        // unbounded region for containment tests; if surfaceBrushes were ever
        // used for classification instead, this decapitated brush would leak
        // solidity through its missing side and the room would stop sealing.
        std::vector<Brush> raw = mapfixtures::sealedHollowRoom();
        std::vector<Brush> decapitated = raw;
        CHECK_FALSE(decapitated.empty());
        if (!decapitated.empty()) {
            CHECK_FALSE(decapitated[0].faces.empty());
            if (!decapitated[0].faces.empty()) {
                decapitated[0].faces.erase(decapitated[0].faces.begin());
            }
        }

        const BspTree rawOnlyTree = buildBspFromHullBrushes(raw);
        const BspTree tree = buildBspFromHullBrushes(raw, &decapitated);
        CHECK(tree.root >= 0);
        const MapHullAnalysis analysis = analyzeMapHull(tree, raw);
        CHECK(analysis.sealed);
        CHECK(tree.surfaceFaces.size() < rawOnlyTree.surfaceFaces.size());
    }

    {
        const Brush solid = makeBrushBox(
            "solid",
            {-1.0f, -1.0f, -1.0f},
            {1.0f, 1.0f, 1.0f},
            "mat/a",
            {});
        const BspTree tree = buildBspFromHullBrushes({solid});
        CHECK(tree.root >= 0);
        CHECK(mapfixtures::hasSolidLeaf(tree));
        const std::int32_t inside = pointLeaf(tree, {0.0f, 0.0f, 0.0f});
        CHECK(inside >= 0);
        CHECK_FALSE(leafIsEmpty(tree, inside));
        CHECK((tree.leaves[static_cast<std::size_t>(inside)].contents & BspContents::Solid) != 0);
    }

    {
        const std::vector<Brush> brushes = mapfixtures::sealedRoomWithWindow();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        CHECK(tree.root >= 0);
        CHECK(mapfixtures::hasGlassLeaf(tree));
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);
    }

    {
        std::vector<Brush> withoutHint = mapfixtures::sealedHollowRoom();
        const BspTree treeNoHint = buildBspFromHullBrushes(withoutHint);
        const int openNoHint = mapfixtures::countOpenLeaves(treeNoHint);

        std::vector<Brush> withHint = withoutHint;
        withHint.push_back(mapfixtures::hintMidPlane());
        const BspTree treeHint = buildBspFromHullBrushes(withHint);
        const int openHint = mapfixtures::countOpenLeaves(treeHint);
        CHECK(openHint > openNoHint);

        const MapHullAnalysis analysis = analyzeMapHull(treeHint, withHint);
        CHECK(analysis.sealed);
    }

    {
        const Brush detail = makeBrushBox(
            "crate",
            {-0.5f, 0.0f, -0.5f},
            {0.5f, 1.0f, 0.5f},
            "mat/a",
            {},
            BrushRole::Detail);
        const BspTree tree = buildBspFromHullBrushes({detail});
        CHECK(tree.root < 0);
        CHECK(tree.leaves.empty());
        CHECK(tree.nodes.empty());
    }

    {
        const std::vector<Brush> brushes = mapfixtures::sealedHollowRoom();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        CHECK_FALSE(tree.surfaceFaces.empty());

        // Inner hull fully inside outer solid: outward probes from the inner faces land in solid.
        Brush outer = makeBrushBox("outer", {-2.0f, -2.0f, -2.0f}, {2.0f, 2.0f, 2.0f}, "mat/a", {});
        Brush inner = makeBrushBox("inner", {-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, "mat/a", {});
        const BspTree nested = buildBspFromHullBrushes({outer, inner});
        const auto hasSurface = [&](const std::string& id) {
            return std::any_of(
                nested.surfaceFaces.begin(),
                nested.surfaceFaces.end(),
                [&](const BspSurfaceFace& face) { return face.id == id; });
        };
        CHECK(hasSurface("outer/east") || hasSurface("outer/west") || hasSurface("outer/top"));
        CHECK_FALSE(hasSurface("inner/east"));
        CHECK_FALSE(hasSurface("inner/west"));
        CHECK_FALSE(hasSurface("inner/top"));
        CHECK_FALSE(hasSurface("inner/bottom"));
    }

    {
        const std::vector<Brush> brushes = mapfixtures::sealedHollowRoomWithStairs();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        CHECK(tree.root >= 0);
        CHECK(mapfixtures::countOpenLeaves(tree) > 1);
        CHECK_FALSE(tree.portals.empty());
        for (const BspPortal& portal : tree.portals) {
            CHECK(leafIsEmpty(tree, portal.leafA) || (tree.leaves[static_cast<std::size_t>(portal.leafA)].contents & BspContents::Door) != 0);
            CHECK(leafIsEmpty(tree, portal.leafB) || (tree.leaves[static_cast<std::size_t>(portal.leafB)].contents & BspContents::Door) != 0);
        }
    }

    {
        const std::vector<Brush> brushes = mapfixtures::sealedHollowRoomWithStairs();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);

        const int interiorOpenLeaves = mapfixtures::countInteriorOpenLeaves(tree, analysis);
        CHECK(interiorOpenLeaves > 1);

        int exteriorLeafCount = 0;
        for (std::uint8_t bit : analysis.exteriorEmpty) {
            if (bit != 0) {
                ++exteriorLeafCount;
            }
        }
        // Exterior void covers far more volume than the stairwell interior
        // but, once merged, should not carry more leaves than it: pre-merge
        // it split one leaf per stair-plane crossing extended out into open
        // space, well past the interior's own leaf count.
        CHECK(exteriorLeafCount <= interiorOpenLeaves);
    }
}

}
