#include "test_assert.hpp"
#include "map_fixtures.hpp"

#include "map/bsp.hpp"
#include "map/bsp_analyze.hpp"
#include "map/nav_graph.hpp"
#include "map/nav_macro_links.hpp"
#include "physics/components.hpp"
#include "physics/physics_world.hpp"

#include <algorithm>

namespace slopengine {

void runNavMacroLinksTests() {
    {
        const std::vector<Brush> brushes = mapfixtures::sealedHollowRoom();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);

        const MapNavigation nav = buildMapNavigation(tree, &analysis.exteriorEmpty);
        const std::vector<NavMacroChainCandidate> candidates = findMacroChainCandidates(nav, tree);
        CHECK(candidates.empty());
    }

    {
        const std::vector<Brush> brushes = mapfixtures::straightStaircaseHallway(13);
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);

        const MapNavigation nav = buildMapNavigation(tree, &analysis.exteriorEmpty);
        const std::vector<NavMacroChainCandidate> candidates = findMacroChainCandidates(nav, tree);
        CHECK_FALSE(candidates.empty());

        std::size_t longest = 0;
        for (const NavMacroChainCandidate& candidate : candidates) {
            longest = std::max(longest, candidate.leaves.size());
        }
        CHECK(longest >= 3u);
    }

    {
        const std::vector<Brush> brushes = mapfixtures::straightStaircaseHallway(13);
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);

        const MapNavigation nav = buildMapNavigation(tree, &analysis.exteriorEmpty);

        PhysicsWorld physics;
        physics.addStaticBrushes(brushes);

        const std::vector<NavMovementProfile> profiles = {
            NavMovementProfile{0.3f, 0.7f},
            NavMovementProfile{2.0f, 0.5f},
        };
        const MapNavMacroLinks macroLinks = buildMapNavMacroLinks(physics, nav, tree, profiles);

        CHECK_EQ(macroLinks.byProfile.size(), std::size_t{1});
        if (!macroLinks.byProfile.empty()) {
            const auto& [profile, links] = macroLinks.byProfile.front();
            CHECK_EQ(profile.radius, 0.3f);
            CHECK_FALSE(links.empty());
            for (const NavMacroLink& link : links) {
                CHECK(link.entryLeaf >= 0);
                CHECK(link.exitLeaf >= 0);
                CHECK(link.entryLeaf != link.exitLeaf);
                CHECK_FALSE(link.innerWaypoints.empty());
                CHECK_EQ(link.innerWaypoints.size(), link.innerWaypointToLeaf.size());
                CHECK_FALSE(link.swallowedLeaves.empty());
            }
        }
    }

    {
        const std::vector<Brush> brushes = mapfixtures::straightStaircaseHallway(13);
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);

        const MapNavigation nav = buildMapNavigation(tree, &analysis.exteriorEmpty);

        PhysicsWorld physics;
        physics.addStaticBrushes(brushes);

        const std::vector<NavMovementProfile> profiles = {NavMovementProfile{0.3f, 0.05f}};
        const MapNavMacroLinks macroLinks = buildMapNavMacroLinks(physics, nav, tree, profiles);
        CHECK(macroLinks.byProfile.empty());
    }

    {
        const std::vector<Brush> brushes = mapfixtures::spiralStaircaseShaft(8);
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);

        const MapNavigation nav = buildMapNavigation(tree, &analysis.exteriorEmpty);
        const std::vector<NavMacroChainCandidate> candidates = findMacroChainCandidates(nav, tree);
        for (const NavMacroChainCandidate& candidate : candidates) {
            CHECK(candidate.entryLeaf >= 0);
            CHECK(candidate.exitLeaf >= 0);
            CHECK(candidate.entryLeaf != candidate.exitLeaf);
        }
    }
}

}
