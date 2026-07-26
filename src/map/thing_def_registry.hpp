#pragma once

#include "core/package.hpp"
#include "map/thing.hpp"
#include "physics/components.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct s7_scheme;

namespace slopengine {

struct ThingDef {
    std::string id;
    std::string label;
    std::string icon;
    ThingKind kind = ThingKind::Prop;

    std::string sprite;
    std::string geo;
    std::string frame = "A";
    std::string animClip;
    bool animLoop = true;
    bool haveAnim = false;

    float motorRadius = 0.3f;
    float motorHeight = 1.1f;
    float motorSpeed = 6.0f;
    float motorGravity = 9.81f;
    float motorStepHeight = 0.4f;
    CharacterHull motorHull = CharacterHull::Capsule;
    CharacterMoveMode motorMoveMode = CharacterMoveMode::Slide;
    bool haveMotor = false;

    std::vector<std::string> tags;

    std::optional<int> health;
    std::string idleAnim;
    std::string behavior;

    std::string packageId;
    PackageRole packageRole = PackageRole::Base;
};

class ThingDefRegistry {
public:
    void clear();
    bool registerDef(ThingDef def);

    int size() const {
        return static_cast<int>(defs_.size());
    }

    const ThingDef* find(std::string_view id) const;
    const std::vector<ThingDef>& defs() const {
        return defs_;
    }

    std::vector<const ThingDef*> defsForRole(PackageRole role) const;

private:
    std::vector<ThingDef> defs_;
};

ThingDefRegistry& thingDefRegistry();

void applyThingDef(const ThingDef& def, Thing& out);

/** Loads *package-things* from Scheme and registers them (append, dup ids ignored). */
bool registerPackageThingsFromScheme(s7_scheme* scheme);

}
