#include "test_assert.hpp"

#include "map/nav_profile_registry.hpp"

#include <s7.h>

namespace slopengine {

void runNavProfileRegistryTests() {
    // Empty registry falls back to a single synthetic "default" entry matching today's
    // hardcoded pre-catalog behavior, so a package that hasn't adopted the catalog keeps
    // baking/loading exactly one file the way it always has.
    {
        navProfileRegistry().clear();
        const std::vector<NavProfileDef> defs = navProfileRegistry().defsOrDefault();
        CHECK_EQ(defs.size(), 1u);
        if (!defs.empty()) {
            CHECK_EQ(defs[0].name, std::string("default"));
            CHECK_EQ(defs[0].params.agentRadius, NavBakeParams{}.agentRadius);
            CHECK_EQ(defs[0].params.agentHeight, NavBakeParams{}.agentHeight);
        }
    }

    // Parsing *package-nav-profiles* round-trips names and per-field params, and clauses
    // left unset (medium's max-climb) keep NavBakeParams's own defaults.
    {
        navProfileRegistry().clear();
        s7_scheme* sc = s7_init();
        CHECK_TRUE(sc != nullptr);
        s7_eval_c_string(
            sc,
            "(define *package-nav-profiles* "
            "  (list "
            "    (cons \"small\"  '((radius . 0.35) (height . 1.0) (max-climb . 0.5))) "
            "    (cons \"medium\" '((radius . 0.75) (height . 0.95)))))");
        CHECK_TRUE(registerPackageNavProfilesFromScheme(sc));

        CHECK_EQ(navProfileRegistry().size(), 2);
        const NavProfileDef* small = navProfileRegistry().find("small");
        CHECK_TRUE(small != nullptr);
        if (small != nullptr) {
            CHECK_EQ(small->params.agentRadius, 0.35f);
            CHECK_EQ(small->params.agentHeight, 1.0f);
            CHECK_EQ(small->params.agentMaxClimb, 0.5f);
        }
        const NavProfileDef* medium = navProfileRegistry().find("medium");
        CHECK_TRUE(medium != nullptr);
        if (medium != nullptr) {
            CHECK_EQ(medium->params.agentRadius, 0.75f);
            CHECK_EQ(medium->params.agentHeight, 0.95f);
            // Unset in the catalog entry -- stays at NavBakeParams's own default.
            CHECK_EQ(medium->params.agentMaxClimb, NavBakeParams{}.agentMaxClimb);
        }
        CHECK_TRUE(navProfileRegistry().find("huge") == nullptr);

        // A non-empty registry is returned as-is, not padded with the synthetic fallback.
        CHECK_EQ(navProfileRegistry().defsOrDefault().size(), 2u);
    }

    // Duplicate names are ignored (first registration wins), same tolerance as
    // ThingDefRegistry::registerDef.
    {
        navProfileRegistry().clear();
        NavProfileDef first;
        first.name = "small";
        first.params.agentRadius = 0.3f;
        CHECK_TRUE(navProfileRegistry().registerDef(first));

        NavProfileDef duplicate;
        duplicate.name = "small";
        duplicate.params.agentRadius = 0.9f;
        CHECK_FALSE(navProfileRegistry().registerDef(duplicate));

        const NavProfileDef* found = navProfileRegistry().find("small");
        CHECK_TRUE(found != nullptr);
        if (found != nullptr) {
            CHECK_EQ(found->params.agentRadius, 0.3f);
        }
    }

    // resolveAutoNavProfile picks the smallest profile that still covers a given body size,
    // and falls back to the largest one available when nothing covers it.
    {
        std::vector<NavProfileDef> profiles;
        NavProfileDef small;
        small.name = "small";
        small.params.agentRadius = 0.35f;
        small.params.agentHeight = 1.0f;
        profiles.push_back(small);

        NavProfileDef medium;
        medium.name = "medium";
        medium.params.agentRadius = 0.75f;
        medium.params.agentHeight = 0.95f;
        profiles.push_back(medium);

        NavProfileDef huge;
        huge.name = "huge";
        huge.params.agentRadius = 1.75f;
        huge.params.agentHeight = 0.9f;
        profiles.push_back(huge);

        const NavProfileDef* forZombieman = resolveAutoNavProfile(profiles, 0.3f, 1.0f);
        CHECK_TRUE(forZombieman != nullptr);
        if (forZombieman != nullptr) {
            CHECK_EQ(forZombieman->name, std::string("small"));
        }

        const NavProfileDef* forCyberdemon = resolveAutoNavProfile(profiles, 1.15f, 0.75f);
        CHECK_TRUE(forCyberdemon != nullptr);
        if (forCyberdemon != nullptr) {
            // Nothing covers 1.15 except huge (1.75) -- rounds up past medium.
            CHECK_EQ(forCyberdemon->name, std::string("huge"));
        }

        const NavProfileDef* forOversized = resolveAutoNavProfile(profiles, 3.0f, 1.0f);
        CHECK_TRUE(forOversized != nullptr);
        if (forOversized != nullptr) {
            // Nothing covers a body bigger than every profile -- falls back to the largest.
            CHECK_EQ(forOversized->name, std::string("huge"));
        }
    }

    navProfileRegistry().clear();
}

}
