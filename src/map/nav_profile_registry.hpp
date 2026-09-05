#pragma once

#include "core/package.hpp"
#include "map/nav_bake.hpp"

#include <string>
#include <string_view>
#include <vector>

struct s7_scheme;

namespace slopengine {

struct NavProfileDef {
    std::string name;
    NavBakeParams params;
    std::string packageId;
    PackageRole packageRole = PackageRole::Base;
};

class NavProfileRegistry {
public:
    void clear();
    bool registerDef(NavProfileDef def);

    int size() const {
        return static_cast<int>(defs_.size());
    }

    const NavProfileDef* find(std::string_view name) const;
    const std::vector<NavProfileDef>& defs() const {
        return defs_;
    }

    /** Every registered profile, or -- if none were ever registered -- a single synthetic
     *  "default" entry built from NavBakeParams{}, so a package that hasn't adopted the
     *  nav-profiles catalog keeps baking/loading exactly one file the way it always has. */
    std::vector<NavProfileDef> defsOrDefault() const;

private:
    std::vector<NavProfileDef> defs_;
};

NavProfileRegistry& navProfileRegistry();

/** Loads *package-nav-profiles* from Scheme and registers them (append, dup names ignored). */
bool registerPackageNavProfilesFromScheme(s7_scheme* scheme);

/** Smallest profile from @p profiles whose radius/height both cover an actor of the given
 *  size, so it's never routed through geometry too tight for its real body -- used when a
 *  thing-def doesn't name a nav-profile explicitly. Falls back to the single largest-radius
 *  profile if none cover it (an over-permissive route beats no route at all), and to nullptr
 *  only if @p profiles is empty (never happens for NavProfileRegistry::defsOrDefault()). */
const NavProfileDef* resolveAutoNavProfile(
    const std::vector<NavProfileDef>& profiles, float radius, float height);

}
