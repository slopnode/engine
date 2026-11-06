#include "test_assert.hpp"
#include "map_fixtures.hpp"

#include "map/bsp.hpp"
#include "map/bsp_analyze.hpp"
#include "map/nav_graph.hpp"
#include "map/nav_macro_links.hpp"
#include "physics/components.hpp"
#include "physics/physics_world.hpp"

#include <algorithm>
#include <limits>

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
        int bottom = -1;
        int top = -1;
        float bottomZ = std::numeric_limits<float>::infinity();
        float topZ = -std::numeric_limits<float>::infinity();
        for (int i = 0; i < nav.leafCount; ++i) {
            if (!nav.walkable[static_cast<std::size_t>(i)]) {
                continue;
            }
            const float z = nav.leafCentroids[static_cast<std::size_t>(i)].z;
            if (z < bottomZ) {
                bottomZ = z;
                bottom = i;
            }
            if (z > topZ) {
                topZ = z;
                top = i;
            }
        }
        CHECK(bottom >= 0);
        CHECK(top >= 0);
        const std::vector<int> path = findLeafPath(nav, bottom, top, {}, 0.7f);
        CHECK_FALSE(path.empty());

        const std::vector<NavMacroChainCandidate> candidates = findMacroChainCandidates(nav, tree);
        for (const NavMacroChainCandidate& candidate : candidates) {
            CHECK(candidate.entryLeaf >= 0);
            CHECK(candidate.exitLeaf >= 0);
            CHECK_FALSE(candidate.leaves.empty());
        }

        PhysicsWorld physics;
        physics.addStaticBrushes(brushes);

        const std::vector<NavMovementProfile> profiles = {
            NavMovementProfile{0.3f, 0.7f},
            NavMovementProfile{2.0f, 0.5f},
        };
        const MapNavMacroLinks macroLinks = buildMapNavMacroLinks(physics, nav, tree, profiles);
        CHECK(macroLinks.byProfile.size() <= std::size_t{1});
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
