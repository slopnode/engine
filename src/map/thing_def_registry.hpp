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

struct ThingFolderDef {
    std::string path;
    std::string label;
    std::string icon;
    std::string packageId;
    PackageRole packageRole = PackageRole::Base;
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
    CharacterHull motorHull = CharacterHull::Capsule;
    CharacterMoveMode motorMoveMode = CharacterMoveMode::Slide;
    bool haveMotor = false;

    std::vector<std::string> tags;

    HandlerBinding onEnter;
    HandlerBinding onUse;
    Vector3 triggerSize{1.0f, 1.5f, 1.0f};
    bool haveTriggerSize = false;

    std::optional<int> health;
    std::string idleAnim;
    std::string behavior;

    bool haveMelee = false;
    float meleeDamage = 0.0f;
    float meleeRange = 1.2f;
    float meleeCooldown = 1.0f;
    std::string meleeAnim;

    bool haveRanged = false;
    float rangedRange = 24.0f;
    float rangedMinRange = 1.5f;
    float rangedCooldown = 2.0f;
    std::string rangedAnim;

    bool haveSight = false;
    bool sightEnabled = true;
    float sightRange = 32.0f;
    float sightFovDegrees = 180.0f;
    float sightEyeLift = 0.75f;
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
