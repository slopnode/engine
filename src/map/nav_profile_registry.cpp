#include "map/nav_profile_registry.hpp"

#include "script/package_load_context.hpp"

#include <raylib.h>
#include <s7.h>

namespace slopengine {

namespace {

NavProfileRegistry g_navProfileRegistry;

bool readStringValue(s7_scheme* scheme, s7_pointer value, std::string& out) {
    (void)scheme;
    if (s7_is_string(value)) {
        out = s7_string(value);
        return true;
    }
    if (s7_is_symbol(value)) {
        out = s7_symbol_name(value);
        return true;
    }
    return false;
}

bool readAssoc(s7_scheme* scheme, s7_pointer alist, const char* key, s7_pointer& out) {
    if (!s7_is_pair(alist) && !s7_is_null(scheme, alist)) {
        return false;
    }
    const s7_pointer pair = s7_assoc(scheme, s7_make_symbol(scheme, key), alist);
    if (!s7_is_pair(pair)) {
        return false;
    }
    out = s7_cdr(pair);
    return true;
}

bool readAssocFloat(s7_scheme* scheme, s7_pointer alist, const char* key, float& out) {
    s7_pointer value = nullptr;
    if (!readAssoc(scheme, alist, key, value)) {
        return false;
    }
    if (s7_is_pair(value)) {
        value = s7_car(value);
    }
    if (!s7_is_number(value)) {
        return false;
    }
    out = static_cast<float>(s7_number_to_real(scheme, value));
    return true;
}

}

void NavProfileRegistry::clear() {
    defs_.clear();
}

bool NavProfileRegistry::registerDef(NavProfileDef def) {
    if (def.name.empty()) {
        return false;
    }
    if (find(def.name) != nullptr) {
        TraceLog(LOG_WARNING, "NAVPROFILES: duplicate name '%s' ignored", def.name.c_str());
        return false;
    }
    defs_.push_back(std::move(def));
    return true;
}

const NavProfileDef* NavProfileRegistry::find(std::string_view name) const {
    for (const NavProfileDef& def : defs_) {
        if (def.name == name) {
            return &def;
        }
    }
    return nullptr;
}

std::vector<NavProfileDef> NavProfileRegistry::defsOrDefault() const {
    if (!defs_.empty()) {
        return defs_;
    }
    NavProfileDef fallback;
    fallback.name = "default";
    return {fallback};
}

NavProfileRegistry& navProfileRegistry() {
    return g_navProfileRegistry;
}

const NavProfileDef* resolveAutoNavProfile(
    const std::vector<NavProfileDef>& profiles, float radius, float height) {
    const NavProfileDef* best = nullptr;
    const NavProfileDef* largest = nullptr;
    for (const NavProfileDef& def : profiles) {
        if (largest == nullptr || def.params.agentRadius > largest->params.agentRadius) {
            largest = &def;
        }
        if (def.params.agentRadius >= radius && def.params.agentHeight >= height) {
            if (best == nullptr || def.params.agentRadius < best->params.agentRadius) {
                best = &def;
            }
        }
    }
    return best != nullptr ? best : largest;
}

bool registerPackageNavProfilesFromScheme(s7_scheme* scheme) {
    if (scheme == nullptr) {
        return false;
    }

    const std::string packageId{currentPackageLoadId()};
    const PackageRole packageRole = currentPackageRole();

    const s7_pointer catalog = s7_name_to_value(scheme, "*package-nav-profiles*");
    if (catalog == s7_undefined(scheme) || s7_is_null(scheme, catalog)) {
        return true;
    }
    if (!s7_is_pair(catalog)) {
        return false;
    }

    for (s7_pointer cursor = catalog; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        const s7_pointer entry = s7_car(cursor);
        if (!s7_is_pair(entry)) {
            continue;
        }

        NavProfileDef def{};
        if (!readStringValue(scheme, s7_car(entry), def.name) || def.name.empty()) {
            continue;
        }

        const s7_pointer props = s7_cdr(entry);
        readAssocFloat(scheme, props, "radius", def.params.agentRadius);
        readAssocFloat(scheme, props, "height", def.params.agentHeight);
        readAssocFloat(scheme, props, "max-climb", def.params.agentMaxClimb);
        readAssocFloat(scheme, props, "max-slope", def.params.agentMaxSlopeDegrees);
        readAssocFloat(scheme, props, "cell-size", def.params.cellSize);
        readAssocFloat(scheme, props, "cell-height", def.params.cellHeight);

        def.packageId = packageId;
        def.packageRole = packageRole;
        g_navProfileRegistry.registerDef(std::move(def));
    }

    return true;
}

}
