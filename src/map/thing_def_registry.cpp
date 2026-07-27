#include "map/thing_def_registry.hpp"

#include "map/handler_binding.hpp"
#include "script/package_load_context.hpp"

#include <raylib.h>
#include <s7.h>

#include <cstring>

namespace slopengine {

namespace {

ThingDefRegistry g_thingDefRegistry;

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

bool readAssocString(s7_scheme* scheme, s7_pointer alist, const char* key, std::string& out) {
    s7_pointer value = nullptr;
    if (!readAssoc(scheme, alist, key, value)) {
        return false;
    }
    if (s7_is_pair(value)) {
        return readStringValue(scheme, s7_car(value), out);
    }
    return readStringValue(scheme, value, out);
}

bool readAssocBool(s7_scheme* scheme, s7_pointer alist, const char* key, bool& out) {
    s7_pointer value = nullptr;
    if (!readAssoc(scheme, alist, key, value)) {
        return false;
    }
    if (s7_is_pair(value)) {
        value = s7_car(value);
    }
    if (s7_is_boolean(value)) {
        out = s7_boolean(scheme, value);
        return true;
    }
    if (s7_is_integer(value)) {
        out = s7_integer(value) != 0;
        return true;
    }
    return false;
}

bool readAssocInt(s7_scheme* scheme, s7_pointer alist, const char* key, int& out) {
    s7_pointer value = nullptr;
    if (!readAssoc(scheme, alist, key, value)) {
        return false;
    }
    if (s7_is_pair(value)) {
        value = s7_car(value);
    }
    if (!s7_is_integer(value)) {
        return false;
    }
    out = static_cast<int>(s7_integer(value));
    return true;
}

bool parseKindName(std::string_view name, ThingKind& out) {
    if (name == "prop") {
        out = ThingKind::Prop;
        return true;
    }
    if (name == "actor") {
        out = ThingKind::Actor;
        return true;
    }
    if (name == "pickup") {
        out = ThingKind::Pickup;
        return true;
    }
    return false;
}

bool readAssocVec3(s7_scheme* scheme, s7_pointer alist, const char* key, Vector3& out) {
    s7_pointer value = nullptr;
    if (!readAssoc(scheme, alist, key, value) || !s7_is_pair(value)) {
        return false;
    }
    s7_pointer x = s7_car(value);
    if (!s7_is_pair(s7_cdr(value))) {
        return false;
    }
    s7_pointer y = s7_cadr(value);
    if (!s7_is_pair(s7_cddr(value))) {
        return false;
    }
    s7_pointer z = s7_caddr(value);
    if (!s7_is_number(x) || !s7_is_number(y) || !s7_is_number(z)) {
        return false;
    }
    out.x = static_cast<float>(s7_number_to_real(scheme, x));
    out.y = static_cast<float>(s7_number_to_real(scheme, y));
    out.z = static_cast<float>(s7_number_to_real(scheme, z));
    return true;
}

bool parseMotorClauses(s7_scheme* scheme, s7_pointer rest, ThingDef& def) {
    def.haveMotor = true;
    for (s7_pointer cursor = rest; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer clause = s7_car(cursor);
        if (!s7_is_pair(clause) || !s7_is_symbol(s7_car(clause))) {
            return false;
        }
        const char* tag = s7_symbol_name(s7_car(clause));
        s7_pointer values = s7_cdr(clause);
        if (!s7_is_pair(values)) {
            return false;
        }
        if (std::strcmp(tag, "hull") == 0) {
            std::string hull;
            if (!readStringValue(scheme, s7_car(values), hull) ||
                (hull != "box" && hull != "capsule")) {
                return false;
            }
            def.motorHull = (hull == "box") ? CharacterHull::Box : CharacterHull::Capsule;
            continue;
        }
        if (std::strcmp(tag, "move") == 0) {
            std::string move;
            if (!readStringValue(scheme, s7_car(values), move) ||
                (move != "try-move" && move != "slide")) {
                return false;
            }
            def.motorMoveMode =
                (move == "try-move") ? CharacterMoveMode::TryMove : CharacterMoveMode::Slide;
            continue;
        }
        if (!s7_is_number(s7_car(values))) {
            return false;
        }
        const float value = static_cast<float>(s7_number_to_real(scheme, s7_car(values)));
        if (std::strcmp(tag, "radius") == 0) {
            def.motorRadius = value;
        } else if (std::strcmp(tag, "height") == 0) {
            def.motorHeight = value;
        } else if (std::strcmp(tag, "speed") == 0) {
            def.motorSpeed = value;
        } else if (std::strcmp(tag, "gravity") == 0) {
            def.motorGravity = value;
        } else if (std::strcmp(tag, "step-height") == 0) {
            def.motorStepHeight = value;
        } else {
            return false;
        }
    }
    return true;
}

bool parseSightTagList(s7_scheme* scheme, s7_pointer values, std::vector<std::string>& out) {
    out.clear();
    for (s7_pointer cursor = values; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        std::string tag;
        if (readStringValue(scheme, s7_car(cursor), tag) && !tag.empty()) {
            out.push_back(std::move(tag));
        }
    }
    return true;
}

bool parseMeleeClauses(s7_scheme* scheme, s7_pointer rest, ThingDef& def) {
    def.haveMelee = true;
    for (s7_pointer cursor = rest; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer clause = s7_car(cursor);
        if (!s7_is_pair(clause) || !s7_is_symbol(s7_car(clause))) {
            return false;
        }
        const char* tag = s7_symbol_name(s7_car(clause));
        s7_pointer values = s7_cdr(clause);
        if (!s7_is_pair(values)) {
            return false;
        }
        if (std::strcmp(tag, "anim") == 0) {
            if (!readStringValue(scheme, s7_car(values), def.meleeAnim)) {
                return false;
            }
            continue;
        }
        if (!s7_is_number(s7_car(values))) {
            return false;
        }
        const float value = static_cast<float>(s7_number_to_real(scheme, s7_car(values)));
        if (std::strcmp(tag, "damage") == 0) {
            def.meleeDamage = value;
        } else if (std::strcmp(tag, "range") == 0) {
            def.meleeRange = value;
        } else if (std::strcmp(tag, "cooldown") == 0) {
            def.meleeCooldown = value;
        } else {
            return false;
        }
    }
    return true;
}

bool parseRangedClauses(s7_scheme* scheme, s7_pointer rest, ThingDef& def) {
    def.haveRanged = true;
    for (s7_pointer cursor = rest; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer clause = s7_car(cursor);
        if (!s7_is_pair(clause) || !s7_is_symbol(s7_car(clause))) {
            return false;
        }
        const char* tag = s7_symbol_name(s7_car(clause));
        s7_pointer values = s7_cdr(clause);
        if (!s7_is_pair(values)) {
            return false;
        }
        if (std::strcmp(tag, "anim") == 0) {
            if (!readStringValue(scheme, s7_car(values), def.rangedAnim)) {
                return false;
            }
            continue;
        }
        if (!s7_is_number(s7_car(values))) {
            return false;
        }
        const float value = static_cast<float>(s7_number_to_real(scheme, s7_car(values)));
        if (std::strcmp(tag, "range") == 0) {
            def.rangedRange = value;
        } else if (std::strcmp(tag, "min-range") == 0) {
            def.rangedMinRange = value;
        } else if (std::strcmp(tag, "cooldown") == 0) {
            def.rangedCooldown = value;
        } else {
            return false;
        }
    }
    return true;
}

template <typename SightOwner>
bool parseSightClauses(s7_scheme* scheme, s7_pointer rest, SightOwner& out) {
    out.haveSight = true;
    for (s7_pointer cursor = rest; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer clause = s7_car(cursor);
        if (!s7_is_pair(clause) || !s7_is_symbol(s7_car(clause))) {
            return false;
        }
        const char* tag = s7_symbol_name(s7_car(clause));
        s7_pointer values = s7_cdr(clause);
        if (std::strcmp(tag, "see-tags") == 0) {
            parseSightTagList(scheme, values, out.sightSeeTags);
            continue;
        }
        if (std::strcmp(tag, "ignore-tags") == 0) {
            parseSightTagList(scheme, values, out.sightIgnoreTags);
            continue;
        }
        if (!s7_is_pair(values)) {
            return false;
        }
        if (std::strcmp(tag, "filter") == 0) {
            if (!readStringValue(scheme, s7_car(values), out.sightFilterProc)) {
                return false;
            }
            continue;
        }
        if (std::strcmp(tag, "enabled") == 0) {
            s7_pointer value = s7_car(values);
            if (s7_is_boolean(value)) {
                out.sightEnabled = s7_boolean(scheme, value);
            } else if (s7_is_integer(value)) {
                out.sightEnabled = s7_integer(value) != 0;
            } else {
                return false;
            }
            continue;
        }
        if (!s7_is_number(s7_car(values))) {
            return false;
        }
        const float value = static_cast<float>(s7_number_to_real(scheme, s7_car(values)));
        if (std::strcmp(tag, "range") == 0) {
            out.sightRange = value;
        } else if (std::strcmp(tag, "fov") == 0) {
            out.sightFovDegrees = value;
        } else if (std::strcmp(tag, "eye-lift") == 0) {
            out.sightEyeLift = value;
        } else {
            return false;
        }
    }
    return true;
}

} // namespace

ThingDefRegistry& thingDefRegistry() {
    return g_thingDefRegistry;
}

void ThingDefRegistry::clear() {
    defs_.clear();
    folders_.clear();
}

bool ThingDefRegistry::registerFolder(ThingFolderDef folder) {
    if (folder.path.empty()) {
        return false;
    }
    for (ThingFolderDef& existing : folders_) {
        if (existing.path == folder.path && existing.packageId == folder.packageId &&
            existing.packageRole == folder.packageRole) {
            if (!folder.icon.empty()) {
                existing.icon = std::move(folder.icon);
            }
            if (!folder.label.empty()) {
                existing.label = std::move(folder.label);
            }
            return true;
        }
    }
    folders_.push_back(std::move(folder));
    return true;
}

bool ThingDefRegistry::registerDef(ThingDef def) {
    if (def.id.empty()) {
        return false;
    }
    if (find(def.id) != nullptr) {
        TraceLog(LOG_WARNING, "THINGDEFS: duplicate id '%s' ignored", def.id.c_str());
        return false;
    }
    if (def.kind != ThingKind::Prop && def.kind != ThingKind::Actor &&
        def.kind != ThingKind::Pickup) {
        TraceLog(
            LOG_WARNING,
            "THINGDEFS: '%s' kind must be prop, actor, or pickup; ignored",
            def.id.c_str());
        return false;
    }
    if (def.label.empty()) {
        def.label = def.id;
    }
    if (def.kind == ThingKind::Actor && !def.haveMotor) {
        def.haveMotor = true;
    }
    if (def.kind == ThingKind::Actor && def.tags.empty()) {
        def.tags.push_back("actor");
    }
    if (def.kind == ThingKind::Pickup) {
        const bool hasSprite = !def.sprite.empty();
        const bool hasGeo = !def.geo.empty();
        if (hasSprite == hasGeo) {
            TraceLog(
                LOG_WARNING,
                "THINGDEFS: pickup '%s' requires exactly one of sprite or geo; ignored",
                def.id.c_str());
            return false;
        }
        if (def.onEnter.empty() && def.onUse.empty()) {
            TraceLog(
                LOG_WARNING,
                "THINGDEFS: pickup '%s' requires on-enter or on-use; ignored",
                def.id.c_str());
            return false;
        }
        if (!def.haveTriggerSize) {
            def.triggerSize = {1.0f, 1.5f, 1.0f};
            def.haveTriggerSize = true;
        }
    }
    defs_.push_back(std::move(def));
    return true;
}

const ThingDef* ThingDefRegistry::find(std::string_view id) const {
    for (const ThingDef& def : defs_) {
        if (def.id == id) {
            return &def;
        }
    }
    return nullptr;
}

std::vector<const ThingDef*> ThingDefRegistry::defsForRole(PackageRole role) const {
    std::vector<const ThingDef*> out;
    for (const ThingDef& def : defs_) {
        if (def.packageRole == role) {
            out.push_back(&def);
        }
    }
    return out;
}

const ThingFolderDef* ThingDefRegistry::findFolder(
    std::string_view path,
    PackageRole role,
    std::string_view packageId) const {
    for (const ThingFolderDef& folder : folders_) {
        if (folder.path != path || folder.packageRole != role) {
            continue;
        }
        if (!packageId.empty() && folder.packageId != packageId) {
            continue;
        }
        return &folder;
    }
    return nullptr;
}

void applyThingDef(const ThingDef& def, Thing& out) {
    out.kind = def.kind;
    out.type = def.id;
    out.sprite = def.sprite;
    out.geo = def.geo;
    out.frame = def.frame;
    out.haveAnim = def.haveAnim;
    out.animClip = def.animClip;
    out.animLoop = def.animLoop;
    out.haveMotor = def.haveMotor;
    out.motorRadius = def.motorRadius;
    out.motorHeight = def.motorHeight;
    out.motorSpeed = def.motorSpeed;
    out.motorGravity = def.motorGravity;
    out.motorStepHeight = def.motorStepHeight;
    out.motorHull = def.motorHull;
    out.motorMoveMode = def.motorMoveMode;
    out.tags = def.tags;
    out.onEnter = def.onEnter;
    out.onUse = def.onUse;
    out.haveTriggerSize = def.haveTriggerSize;
    out.triggerSize = def.triggerSize;
    out.haveSight = def.haveSight;
    out.sightEnabled = def.sightEnabled;
    out.sightRange = def.sightRange;
    out.sightFovDegrees = def.sightFovDegrees;
    out.sightEyeLift = def.sightEyeLift;
    out.sightSeeTags = def.sightSeeTags;
    out.sightIgnoreTags = def.sightIgnoreTags;
    out.sightFilterProc = def.sightFilterProc;
}

bool registerPackageThingsFromScheme(s7_scheme* scheme) {
    if (scheme == nullptr) {
        return false;
    }

    const std::string packageId{currentPackageLoadId()};
    const PackageRole packageRole = currentPackageRole();

    const s7_pointer folders = s7_name_to_value(scheme, "*package-thing-folders*");
    if (folders != s7_undefined(scheme) && !s7_is_null(scheme, folders)) {
        if (!s7_is_pair(folders)) {
            return false;
        }
        for (s7_pointer cursor = folders; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
            const s7_pointer entry = s7_car(cursor);
            if (!s7_is_pair(entry)) {
                continue;
            }
            ThingFolderDef folder{};
            if (!readStringValue(scheme, s7_car(entry), folder.path) || folder.path.empty()) {
                continue;
            }
            const s7_pointer value = s7_cdr(entry);
            if (s7_is_string(value) || s7_is_symbol(value)) {
                readStringValue(scheme, value, folder.icon);
            } else if (s7_is_pair(value)) {
                readAssocString(scheme, value, "icon", folder.icon);
                readAssocString(scheme, value, "label", folder.label);
                if (folder.icon.empty()) {
                    readStringValue(scheme, s7_car(value), folder.icon);
                }
            }
            folder.packageId = packageId;
            folder.packageRole = packageRole;
            thingDefRegistry().registerFolder(std::move(folder));
        }
    }

    const s7_pointer catalog = s7_name_to_value(scheme, "*package-things*");
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

        ThingDef def{};
        if (!readStringValue(scheme, s7_car(entry), def.id) || def.id.empty()) {
            continue;
        }

        const s7_pointer props = s7_cdr(entry);
        readAssocString(scheme, props, "label", def.label);
        readAssocString(scheme, props, "icon", def.icon);
        readAssocString(scheme, props, "path", def.path);

        std::string kindName;
        if (!readAssocString(scheme, props, "kind", kindName) ||
            !parseKindName(kindName, def.kind)) {
            TraceLog(
                LOG_WARNING,
                "THINGDEFS: '%s' missing or invalid kind; ignored",
                def.id.c_str());
            continue;
        }

        readAssocString(scheme, props, "sprite", def.sprite);
        readAssocString(scheme, props, "geo", def.geo);
        readAssocString(scheme, props, "frame", def.frame);

        s7_pointer animVal = nullptr;
        if (readAssoc(scheme, props, "anim", animVal)) {
            if (s7_is_pair(animVal)) {
                if (readStringValue(scheme, s7_car(animVal), def.animClip)) {
                    def.haveAnim = true;
                    def.animLoop = true;
                    if (s7_is_pair(s7_cdr(animVal))) {
                        s7_pointer loopVal = s7_cadr(animVal);
                        if (s7_is_boolean(loopVal)) {
                            def.animLoop = s7_boolean(scheme, loopVal);
                        } else if (s7_is_integer(loopVal)) {
                            def.animLoop = s7_integer(loopVal) != 0;
                        }
                    }
                }
            } else if (readStringValue(scheme, animVal, def.animClip)) {
                def.haveAnim = true;
                def.animLoop = true;
            }
        }
        bool animLoop = true;
        if (readAssocBool(scheme, props, "anim-loop", animLoop) && def.haveAnim) {
            def.animLoop = animLoop;
        }

        s7_pointer motorVal = nullptr;
        if (readAssoc(scheme, props, "motor", motorVal)) {
            if (!parseMotorClauses(scheme, motorVal, def)) {
                TraceLog(
                    LOG_WARNING,
                    "THINGDEFS: '%s' has invalid motor; ignored",
                    def.id.c_str());
                continue;
            }
        }

        s7_pointer tagsVal = nullptr;
        if (readAssoc(scheme, props, "tags", tagsVal)) {
            def.tags.clear();
            for (s7_pointer tagCursor = tagsVal; s7_is_pair(tagCursor);
                 tagCursor = s7_cdr(tagCursor)) {
                std::string tag;
                if (readStringValue(scheme, s7_car(tagCursor), tag) && !tag.empty()) {
                    def.tags.push_back(std::move(tag));
                }
            }
        }

        s7_pointer onEnterVal = nullptr;
        if (readAssoc(scheme, props, "on-enter", onEnterVal)) {
            if (!parseHandlerBinding(scheme, onEnterVal, def.onEnter)) {
                TraceLog(
                    LOG_WARNING,
                    "THINGDEFS: '%s' has invalid on-enter; ignored",
                    def.id.c_str());
                continue;
            }
        }
        s7_pointer onUseVal = nullptr;
        if (readAssoc(scheme, props, "on-use", onUseVal)) {
            if (!parseHandlerBinding(scheme, onUseVal, def.onUse)) {
                TraceLog(
                    LOG_WARNING,
                    "THINGDEFS: '%s' has invalid on-use; ignored",
                    def.id.c_str());
                continue;
            }
        }
        Vector3 triggerSize{};
        if (readAssocVec3(scheme, props, "trigger-size", triggerSize)) {
            def.triggerSize = triggerSize;
            def.haveTriggerSize = true;
        }

        int health = 0;
        if (readAssocInt(scheme, props, "health", health)) {
            def.health = health;
        }
        readAssocString(scheme, props, "idle-anim", def.idleAnim);
        readAssocString(scheme, props, "behavior", def.behavior);

        s7_pointer meleeVal = nullptr;
        if (readAssoc(scheme, props, "melee", meleeVal)) {
            if (!parseMeleeClauses(scheme, meleeVal, def)) {
                TraceLog(
                    LOG_WARNING,
                    "THINGDEFS: '%s' has invalid melee; ignored",
                    def.id.c_str());
                continue;
            }
        }

        s7_pointer rangedVal = nullptr;
        if (readAssoc(scheme, props, "ranged", rangedVal)) {
            if (!parseRangedClauses(scheme, rangedVal, def)) {
                TraceLog(
                    LOG_WARNING,
                    "THINGDEFS: '%s' has invalid ranged; ignored",
                    def.id.c_str());
                continue;
            }
        }

        s7_pointer sightVal = nullptr;
        if (readAssoc(scheme, props, "sight", sightVal)) {
            if (!parseSightClauses(scheme, sightVal, def)) {
                TraceLog(
                    LOG_WARNING,
                    "THINGDEFS: '%s' has invalid sight; ignored",
                    def.id.c_str());
                continue;
            }
        }

        def.packageId = packageId;
        def.packageRole = packageRole;
        thingDefRegistry().registerDef(std::move(def));
    }

    return true;
}

}
