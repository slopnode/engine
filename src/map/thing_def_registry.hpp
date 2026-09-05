#pragma once

#include "core/package.hpp"
#include "map/thing.hpp"
#include "physics/components.hpp"

#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct s7_scheme;

namespace slopengine {

struct ThingFolderDef {
    std::string path;
    std::string label;
    std::string icon;
    std::string packageId;
    PackageRole packageRole = PackageRole::Base;
};

struct ThingDefBehaviorParam {
    enum class Kind { Float, String, Bool } kind = Kind::Float;
    float f = 0.0f;
    std::string s;
    bool b = false;
};

struct ThingDefBehavior {
    std::string name;
    std::vector<std::pair<std::string, ThingDefBehaviorParam>> params;

    const ThingDefBehaviorParam* find(std::string_view key) const {
        for (const auto& [k, v] : params) {
            if (k == key) {
                return &v;
            }
        }
        return nullptr;
    }
};

struct ThingDef {
    std::string id;
    std::string label;
    std::string icon;
    std::string path;
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
    float motorVerticalSpeed = 3.0f;
    float motorHoverHeight = 0.0f;
    /** Nav pathing preference, not a physics clamp: excess drop beyond this (in world units)
     *  onto the next leaf accrues a routing cost penalty rather than blocking the step outright,
     *  so a ground actor prefers detouring around ledges without ever getting stranded when the
     *  drop is the only route. Infinity (default) reproduces pre-existing behavior exactly. */
    float motorMaxFall = std::numeric_limits<float>::infinity();
    /** Nav pathing cost multiplier applied to routing through Water-content leaves; 1.0 (default)
     *  is no preference. Values above 1 make an actor prefer a dry detour when one exists, while
     *  still crossing water leaves rather than failing to path at all. */
    float motorWaterAversion = 1.0f;
    CharacterHull motorHull = CharacterHull::Capsule;
    CharacterMoveMode motorMoveMode = CharacterMoveMode::Slide;
    /** Named entry in the nav-profile catalog (data/nav-profiles.s7) this actor paths
     *  against; empty means auto-select the smallest profile that still fits its own
     *  motorRadius/motorHeight. See NavigationAgent::navProfile. */
    std::string motorNavProfile;
    bool haveMotor = false;

    std::vector<std::string> tags;

    HandlerBinding onEnter;
    HandlerBinding onUse;
    Vector3 triggerSize{1.0f, 1.5f, 1.0f};
    bool haveTriggerSize = false;

    std::optional<int> health;
    std::string idleAnim;
    std::string behavior;

    std::optional<float> painChance;
    std::optional<float> painThreshold;

    std::vector<ThingDefBehavior> behaviors;

    bool haveSight = false;
    bool sightEnabled = true;
    float sightRange = 32.0f;
    float sightFovDegrees = 180.0f;
    float sightEyeLift = 0.85f;
    std::vector<std::string> sightSeeTags;
    std::vector<std::string> sightIgnoreTags;
    std::string sightFilterProc;

    std::string packageId;
    PackageRole packageRole = PackageRole::Base;
};

class ThingDefRegistry {
public:
    void clear();
    bool registerDef(ThingDef def);
    bool registerFolder(ThingFolderDef folder);

    int size() const {
        return static_cast<int>(defs_.size());
    }

    const ThingDef* find(std::string_view id) const;
    const ThingFolderDef* findFolder(
        std::string_view path,
        PackageRole role,
        std::string_view packageId) const;
    const std::vector<ThingDef>& defs() const {
        return defs_;
    }

    std::vector<const ThingDef*> defsForRole(PackageRole role) const;

private:
    std::vector<ThingDef> defs_;
    std::vector<ThingFolderDef> folders_;
};

ThingDefRegistry& thingDefRegistry();

void applyThingDef(const ThingDef& def, Thing& out);

/** Loads *package-things* from Scheme and registers them (append, dup ids ignored). */
bool registerPackageThingsFromScheme(s7_scheme* scheme);

}
