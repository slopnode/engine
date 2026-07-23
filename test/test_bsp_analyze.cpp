#include "test_assert.hpp"
#include "map_fixtures.hpp"

#include "map/bsp.hpp"
#include "map/bsp_analyze.hpp"
#include "map/brush.hpp"

#include <string>
#include <vector>

namespace slopengine {

void runBspAnalyzeTests() {
    {
        const std::vector<Brush> brushes = mapfixtures::sealedHollowRoom();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);
        CHECK(analysis.leakPathFaceIds.empty());
        CHECK_FALSE(analysis.exteriorEmpty.empty());

        bool anyExterior = false;
        for (std::uint8_t bit : analysis.exteriorEmpty) {
            if (bit != 0) {
                anyExterior = true;
                break;
            }
        }
        CHECK(anyExterior);
        CHECK(mapfixtures::countInteriorOpenLeaves(tree, analysis) >= 1);
    }

    {
        const std::vector<Brush> brushes = mapfixtures::leakyHollowRoom();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK_FALSE(analysis.sealed);
        CHECK_FALSE(analysis.leakPathFaceIds.empty());
    }

    {
        std::vector<Brush> brushes = mapfixtures::sealedHollowRoom();
        brushes.push_back(makeBrushBox(
            "crate-inside",
            {-0.5f, 0.5f, -0.5f},
            {0.5f, 1.5f, 0.5f},
            "mat/a",
            {},
            BrushRole::Detail));
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);
        CHECK(analysis.detailOutsideWarnings.empty());
    }

    {
        std::vector<Brush> brushes = mapfixtures::sealedHollowRoom();
        brushes.push_back(makeBrushBox(
            "crate-outside",
            {5.0f, 0.0f, 5.0f},
            {6.0f, 1.0f, 6.0f},
            "mat/a",
            {},
            BrushRole::Detail));
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);
        CHECK_FALSE(analysis.detailOutsideWarnings.empty());
    }

    {
        std::vector<Brush> brushes = mapfixtures::leakyHollowRoom();
        brushes.push_back(makeBrushBox(
            "crate-plug",
            {-0.5f, 0.5f, -0.5f},
            {0.5f, 1.5f, 0.5f},
            "mat/a",
            {},
            BrushRole::Detail));
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK_FALSE(analysis.sealed);
    }
}

}
