#include "map/things_script.hpp"

#include "map/map_handler_registry.hpp"
#include "map/thing_def_registry.hpp"
#include "script/script_scope.hpp"

#include <raylib.h>
#include <s7.h>

#include <cstring>
#include <string>
#include <unordered_set>

namespace slopengine {

namespace {

struct ThingLoadContext {
    ThingDocument* doc = nullptr;
    AssetStore* assets = nullptr;
    std::unordered_set<std::string> usedIds;
    bool sawPlayerStart = false;
};

ThingLoadContext* g_context = nullptr;

s7_pointer makeTaggedList(s7_scheme* sc, const char* tag, s7_pointer rest) {
    return s7_cons(sc, s7_make_symbol(sc, tag), rest);
}

bool readString(s7_scheme* sc, s7_pointer value, std::string& out) {
    if (s7_is_string(value)) {
        out = s7_string(value);
        return true;
    }
    if (s7_is_symbol(value)) {
        out = s7_symbol_name(value);
        return true;
    }
    (void)sc;
    return false;
}

bool readVec3(s7_scheme* sc, s7_pointer x, s7_pointer y, s7_pointer z, Vector3& out) {
    if (!s7_is_number(x) || !s7_is_number(y) || !s7_is_number(z)) {
        return false;
    }
    out.x = static_cast<float>(s7_number_to_real(sc, x));
    out.y = static_cast<float>(s7_number_to_real(sc, y));
    out.z = static_cast<float>(s7_number_to_real(sc, z));
    return true;
}

bool readVec2(s7_scheme* sc, s7_pointer x, s7_pointer y, Vector2& out) {
    if (!s7_is_number(x) || !s7_is_number(y)) {
        return false;
    }
    out.x = static_cast<float>(s7_number_to_real(sc, x));
    out.y = static_cast<float>(s7_number_to_real(sc, y));
    return true;
}

bool claimId(const std::string& id) {
    if (g_context == nullptr || id.empty()) {
        return false;
    }
    if (!g_context->usedIds.insert(id).second) {
        TraceLog(LOG_WARNING, "THING: duplicate id '%s'", id.c_str());
        return false;
    }
    return true;
}

s7_pointer g_id(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "id", 1, args, "value");
    }
    return makeTaggedList(sc, "id", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_type(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "type", 1, args, "catalog-id");
    }
    return makeTaggedList(sc, "type", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_at(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args)) || !s7_is_pair(s7_cddr(args))) {
        return s7_wrong_type_arg_error(sc, "at", 0, args, "x y z");
    }
    return makeTaggedList(sc, "at", s7_list(sc, 3, s7_car(args), s7_cadr(args), s7_caddr(args)));
}

s7_pointer g_yaw(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "yaw", 1, args, "radians");
    }
    return makeTaggedList(sc, "yaw", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_pitch(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "pitch", 1, args, "radians");
    }
    return makeTaggedList(sc, "pitch", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_angles(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args)) || !s7_is_pair(s7_cddr(args))) {
        return s7_wrong_type_arg_error(sc, "angles", 0, args, "pitch yaw roll");
    }
    return makeTaggedList(
        sc,
        "angles",
        s7_list(sc, 3, s7_car(args), s7_cadr(args), s7_caddr(args)));
}

s7_pointer g_sprite(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "sprite", 1, args, "path");
    }
    return makeTaggedList(sc, "sprite", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_geo(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "geo", 1, args, "path");
    }
    return makeTaggedList(sc, "geo", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_brush(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "brush", 1, args, "brush-id");
    }
    return makeTaggedList(sc, "brush", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_frame(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "frame", 1, args, "frame-id");
    }
    return makeTaggedList(sc, "frame", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_anim(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "anim", 1, args, "clip [loop]");
    }
    if (s7_is_pair(s7_cdr(args))) {
        return makeTaggedList(sc, "anim", s7_list(sc, 2, s7_car(args), s7_cadr(args)));
    }
    return makeTaggedList(sc, "anim", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_prompt(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "prompt", 1, args, "text");
    }
    return makeTaggedList(sc, "prompt", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_on_use(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "on-use", 1, args, "handler");
    }
    return makeTaggedList(sc, "on-use", args);
}

s7_pointer g_pivot(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args)) || !s7_is_pair(s7_cddr(args))) {
        return s7_wrong_type_arg_error(sc, "pivot", 0, args, "x y z");
    }
    return makeTaggedList(
        sc,
        "pivot",
        s7_list(sc, 3, s7_car(args), s7_cadr(args), s7_caddr(args)));
}

s7_pointer g_open_offset(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args)) || !s7_is_pair(s7_cddr(args))) {
        return s7_wrong_type_arg_error(sc, "open-offset", 0, args, "dx dy dz");
    }
    return makeTaggedList(
        sc,
        "open-offset",
        s7_list(sc, 3, s7_car(args), s7_cadr(args), s7_caddr(args)));
}

s7_pointer g_open_pitch(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "open-pitch", 1, args, "radians");
    }
    return makeTaggedList(sc, "open-pitch", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_open_yaw(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "open-yaw", 1, args, "radians");
    }
    return makeTaggedList(sc, "open-yaw", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_open_roll(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "open-roll", 1, args, "radians");
    }
    return makeTaggedList(sc, "open-roll", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_duration(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "duration", 1, args, "seconds");
    }
    return makeTaggedList(sc, "duration", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_auto_close(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "auto-close", 1, args, "seconds");
    }
    return makeTaggedList(sc, "auto-close", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_collide_size(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args)) || !s7_is_pair(s7_cddr(args))) {
        return s7_wrong_type_arg_error(sc, "collide-size", 0, args, "w h d");
    }
    return makeTaggedList(
        sc,
        "collide-size",
        s7_list(sc, 3, s7_car(args), s7_cadr(args), s7_caddr(args)));
}

s7_pointer g_collide_center(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args)) || !s7_is_pair(s7_cddr(args))) {
        return s7_wrong_type_arg_error(sc, "collide-center", 0, args, "x y z");
    }
    return makeTaggedList(
        sc,
        "collide-center",
        s7_list(sc, 3, s7_car(args), s7_cadr(args), s7_caddr(args)));
}

s7_pointer g_block_mode(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "block-mode", 1, args, "shove|crush");
    }
    return makeTaggedList(sc, "block-mode", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_push(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "push", 1, args, "full|horizontal|off");
    }
    return makeTaggedList(sc, "push", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_carry(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "carry", 1, args, "bool");
    }
    return makeTaggedList(sc, "carry", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_on_crush(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "on-crush", 1, args, "handler");
    }
    return makeTaggedList(sc, "on-crush", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_group(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "group", 1, args, "id");
    }
    return makeTaggedList(sc, "group", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_on_enter(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "on-enter", 1, args, "handler");
    }
    return makeTaggedList(sc, "on-enter", args);
}

s7_pointer g_on_exit(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "on-exit", 1, args, "handler");
    }
    return makeTaggedList(sc, "on-exit", args);
}

s7_pointer g_trigger_size(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args)) || !s7_is_pair(s7_cddr(args))) {
        return s7_wrong_type_arg_error(sc, "trigger-size", 0, args, "w h d");
    }
    return makeTaggedList(
        sc,
        "trigger-size",
        s7_list(sc, 3, s7_car(args), s7_cadr(args), s7_caddr(args)));
}

s7_pointer g_collide_tags(s7_scheme* sc, s7_pointer args) {
    return makeTaggedList(sc, "collide-tags", args);
}

s7_pointer g_tags(s7_scheme* sc, s7_pointer args) {
    return makeTaggedList(sc, "tags", args);
}

s7_pointer g_motor(s7_scheme* sc, s7_pointer args) {
    return makeTaggedList(sc, "motor", args);
}

s7_pointer g_sight(s7_scheme* sc, s7_pointer args) {
    return makeTaggedList(sc, "sight", args);
}

s7_pointer g_sight_fov(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "fov", 1, args, "value");
    }
    return makeTaggedList(sc, "fov", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_sight_eye_lift(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "eye-lift", 1, args, "value");
    }
    return makeTaggedList(sc, "eye-lift", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_sight_see_tags(s7_scheme* sc, s7_pointer args) {
    return makeTaggedList(sc, "see-tags", args);
}

s7_pointer g_sight_ignore_tags(s7_scheme* sc, s7_pointer args) {
    return makeTaggedList(sc, "ignore-tags", args);
}

s7_pointer g_sight_filter(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "filter", 1, args, "proc-name");
    }
    return makeTaggedList(sc, "filter", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_sight_enabled(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "enabled", 1, args, "bool");
    }
    return makeTaggedList(sc, "enabled", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_motor_radius(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "radius", 1, args, "value");
    }
    return makeTaggedList(sc, "radius", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_motor_height(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "height", 1, args, "value");
    }
    return makeTaggedList(sc, "height", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_motor_speed(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "speed", 1, args, "value");
    }
    return makeTaggedList(sc, "speed", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_motor_gravity(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "gravity", 1, args, "value");
    }
    return makeTaggedList(sc, "gravity", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_motor_step_height(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "step-height", 1, args, "value");
    }
    return makeTaggedList(sc, "step-height", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_motor_hull(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "hull", 1, args, "capsule|box");
    }
    return makeTaggedList(sc, "hull", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_motor_move(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "move", 1, args, "slide|try-move");
    }
    return makeTaggedList(sc, "move", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_color(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args)) || !s7_is_pair(s7_cddr(args))) {
        return s7_wrong_type_arg_error(sc, "color", 0, args, "r g b");
    }
    return makeTaggedList(sc, "color", s7_list(sc, 3, s7_car(args), s7_cadr(args), s7_caddr(args)));
}

s7_pointer g_intensity(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "intensity", 1, args, "value");
    }
    return makeTaggedList(sc, "intensity", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_range(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "range", 1, args, "value");
    }
    return makeTaggedList(sc, "range", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_cone(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "cone", 1, args, "radians");
    }
    return makeTaggedList(sc, "cone", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_size(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args))) {
        return s7_wrong_type_arg_error(sc, "size", 0, args, "width height");
    }
    return makeTaggedList(sc, "size", s7_list(sc, 2, s7_car(args), s7_cadr(args)));
}

s7_pointer g_audio(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "audio", 1, args, "path");
    }
    return makeTaggedList(sc, "audio", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_clip(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "clip", 1, args, "path");
    }
    return makeTaggedList(sc, "clip", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_volume(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "volume", 1, args, "value");
    }
    return makeTaggedList(sc, "volume", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_loop(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "loop", 1, args, "bool");
    }
    return makeTaggedList(sc, "loop", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_spatial(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "spatial", 1, args, "bool");
    }
    return makeTaggedList(sc, "spatial", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_min_distance(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "min-distance", 1, args, "value");
    }
    return makeTaggedList(sc, "min-distance", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_max_distance(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "max-distance", 1, args, "value");
    }
    return makeTaggedList(sc, "max-distance", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

bool readBool(s7_scheme* sc, s7_pointer value, bool& out) {
    if (s7_is_boolean(value)) {
        out = s7_boolean(sc, value);
        return true;
    }
    if (s7_is_integer(value)) {
        out = s7_integer(value) != 0;
        return true;
    }
    return false;
}

bool parseThingClauses(s7_scheme* sc, s7_pointer args, Thing& out) {
    for (s7_pointer cursor = args; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer clause = s7_car(cursor);
        if (!s7_is_pair(clause) || !s7_is_symbol(s7_car(clause))) {
            TraceLog(LOG_WARNING, "THING: expected clause list, got non-clause form");
            return false;
        }

        const char* tag = s7_symbol_name(s7_car(clause));
        s7_pointer rest = s7_cdr(clause);

        if (std::strcmp(tag, "id") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), out.id);
        } else if (std::strcmp(tag, "type") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), out.type);
        } else if (std::strcmp(tag, "at") == 0 &&
                   s7_is_pair(rest) &&
                   s7_is_pair(s7_cdr(rest)) &&
                   s7_is_pair(s7_cddr(rest))) {
            out.haveAt = readVec3(sc, s7_car(rest), s7_cadr(rest), s7_caddr(rest), out.at);
        } else if (std::strcmp(tag, "yaw") == 0 && s7_is_pair(rest) && s7_is_number(s7_car(rest))) {
            out.yaw = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
            if (!out.haveAngles) {
                out.angles.y = out.yaw;
            }
        } else if (std::strcmp(tag, "pitch") == 0 && s7_is_pair(rest) && s7_is_number(s7_car(rest))) {
            out.pitch = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
            out.havePitch = true;
            if (!out.haveAngles) {
                out.angles.x = out.pitch;
            }
        } else if (std::strcmp(tag, "angles") == 0 &&
                   s7_is_pair(rest) &&
                   s7_is_pair(s7_cdr(rest)) &&
                   s7_is_pair(s7_cddr(rest))) {
            out.haveAngles = readVec3(sc, s7_car(rest), s7_cadr(rest), s7_caddr(rest), out.angles);
            if (out.haveAngles) {
                out.yaw = out.angles.y;
                if (!out.havePitch) {
                    out.pitch = out.angles.x;
                    out.havePitch = true;
                }
            }
        } else if (std::strcmp(tag, "sprite") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), out.sprite);
        } else if (std::strcmp(tag, "geo") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), out.geo);
        } else if (std::strcmp(tag, "brush") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), out.brush);
        } else if (std::strcmp(tag, "frame") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), out.frame);
        } else if (std::strcmp(tag, "anim") == 0 && s7_is_pair(rest)) {
            if (readString(sc, s7_car(rest), out.animClip)) {
                out.haveAnim = true;
                out.animLoop = true;
                if (s7_is_pair(s7_cdr(rest))) {
                    s7_pointer loopVal = s7_cadr(rest);
                    if (s7_is_boolean(loopVal)) {
                        out.animLoop = s7_boolean(sc, loopVal);
                    } else if (s7_is_integer(loopVal)) {
                        out.animLoop = s7_integer(loopVal) != 0;
                    }
                }
            }
        } else if (std::strcmp(tag, "prompt") == 0 && s7_is_pair(rest)) {
            out.havePrompt = readString(sc, s7_car(rest), out.prompt);
        } else if (std::strcmp(tag, "on-use") == 0 && s7_is_pair(rest)) {
            if (parseHandlerBinding(sc, rest, out.onUse)) {
                mapHandlerRegistry().refineBinding(out.onUse, MapHandlerKind::Use);
            }
        } else if (std::strcmp(tag, "pivot") == 0 &&
                   s7_is_pair(rest) &&
                   s7_is_pair(s7_cdr(rest)) &&
                   s7_is_pair(s7_cddr(rest))) {
            out.haveMoverPivot =
                readVec3(sc, s7_car(rest), s7_cadr(rest), s7_caddr(rest), out.moverPivot);
        } else if (std::strcmp(tag, "open-offset") == 0 &&
                   s7_is_pair(rest) &&
                   s7_is_pair(s7_cdr(rest)) &&
                   s7_is_pair(s7_cddr(rest))) {
            out.haveMoverOpenOffset =
                readVec3(sc, s7_car(rest), s7_cadr(rest), s7_caddr(rest), out.moverOpenOffset);
        } else if (std::strcmp(tag, "open-pitch") == 0 && s7_is_pair(rest) &&
                   s7_is_number(s7_car(rest))) {
            out.haveMoverOpenAngle = true;
            out.moverRotAxis = 0;
            out.moverOpenAngle = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
        } else if (std::strcmp(tag, "open-yaw") == 0 && s7_is_pair(rest) &&
                   s7_is_number(s7_car(rest))) {
            out.haveMoverOpenAngle = true;
            out.moverRotAxis = 1;
            out.moverOpenAngle = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
        } else if (std::strcmp(tag, "open-roll") == 0 && s7_is_pair(rest) &&
                   s7_is_number(s7_car(rest))) {
            out.haveMoverOpenAngle = true;
            out.moverRotAxis = 2;
            out.moverOpenAngle = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
        } else if (std::strcmp(tag, "duration") == 0 && s7_is_pair(rest) &&
                   s7_is_number(s7_car(rest))) {
            out.haveMoverDuration = true;
            out.moverDuration = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
        } else if (std::strcmp(tag, "auto-close") == 0 && s7_is_pair(rest) &&
                   s7_is_number(s7_car(rest))) {
            out.haveMoverAutoClose = true;
            out.moverAutoClose = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
        } else if (std::strcmp(tag, "collide-size") == 0 &&
                   s7_is_pair(rest) &&
                   s7_is_pair(s7_cdr(rest)) &&
                   s7_is_pair(s7_cddr(rest))) {
            out.haveMoverCollideSize =
                readVec3(sc, s7_car(rest), s7_cadr(rest), s7_caddr(rest), out.moverCollideSize);
        } else if (std::strcmp(tag, "collide-center") == 0 &&
                   s7_is_pair(rest) &&
                   s7_is_pair(s7_cdr(rest)) &&
                   s7_is_pair(s7_cddr(rest))) {
            out.haveMoverCollideCenter =
                readVec3(sc, s7_car(rest), s7_cadr(rest), s7_caddr(rest), out.moverCollideCenter);
        } else if (std::strcmp(tag, "block-mode") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), out.moverBlockMode);
        } else if (std::strcmp(tag, "push") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), out.moverPush);
        } else if (std::strcmp(tag, "carry") == 0 && s7_is_pair(rest)) {
            out.haveMoverSlide = readBool(sc, s7_car(rest), out.moverSlide);
        } else if (std::strcmp(tag, "on-crush") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), out.onCrush);
        } else if (std::strcmp(tag, "group") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), out.moverGroup);
        } else if (std::strcmp(tag, "on-enter") == 0 && s7_is_pair(rest)) {
            if (parseHandlerBinding(sc, rest, out.onEnter)) {
                mapHandlerRegistry().refineBinding(out.onEnter, MapHandlerKind::Enter);
            }
        } else if (std::strcmp(tag, "on-exit") == 0 && s7_is_pair(rest)) {
            if (parseHandlerBinding(sc, rest, out.onExit)) {
                mapHandlerRegistry().refineBinding(out.onExit, MapHandlerKind::Exit);
            }
        } else if (std::strcmp(tag, "trigger-size") == 0 &&
                   s7_is_pair(rest) &&
                   s7_is_pair(s7_cdr(rest)) &&
                   s7_is_pair(s7_cddr(rest))) {
            out.haveTriggerSize =
                readVec3(sc, s7_car(rest), s7_cadr(rest), s7_caddr(rest), out.triggerSize);
        } else if (std::strcmp(tag, "collide-tags") == 0) {
            out.collideTags.clear();
            for (s7_pointer tagCursor = rest; s7_is_pair(tagCursor); tagCursor = s7_cdr(tagCursor)) {
                std::string tagValue;
                if (readString(sc, s7_car(tagCursor), tagValue) && !tagValue.empty()) {
                    out.collideTags.push_back(std::move(tagValue));
                }
            }
        } else if (std::strcmp(tag, "tags") == 0) {
            out.tags.clear();
            for (s7_pointer tagCursor = rest; s7_is_pair(tagCursor); tagCursor = s7_cdr(tagCursor)) {
                std::string tagValue;
                if (readString(sc, s7_car(tagCursor), tagValue) && !tagValue.empty()) {
                    out.tags.push_back(std::move(tagValue));
                }
            }
        } else if (std::strcmp(tag, "motor") == 0) {
            out.haveMotor = true;
            for (s7_pointer motorCursor = rest; s7_is_pair(motorCursor);
                 motorCursor = s7_cdr(motorCursor)) {
                s7_pointer motorClause = s7_car(motorCursor);
                if (!s7_is_pair(motorClause) || !s7_is_symbol(s7_car(motorClause))) {
                    TraceLog(LOG_WARNING, "THING: motor expected clause list");
                    return false;
                }
                const char* motorTag = s7_symbol_name(s7_car(motorClause));
                s7_pointer motorRest = s7_cdr(motorClause);
                if (!s7_is_pair(motorRest)) {
                    TraceLog(LOG_WARNING, "THING: malformed motor clause '%s'", motorTag);
                    return false;
                }
                if (std::strcmp(motorTag, "hull") == 0) {
                    std::string hull;
                    if (!readString(sc, s7_car(motorRest), hull) ||
                        (hull != "box" && hull != "capsule")) {
                        TraceLog(LOG_WARNING, "THING: motor hull must be box or capsule");
                        return false;
                    }
                    out.motorHull = (hull == "box") ? CharacterHull::Box : CharacterHull::Capsule;
                    continue;
                }
                if (std::strcmp(motorTag, "move") == 0) {
                    std::string move;
                    if (!readString(sc, s7_car(motorRest), move) ||
                        (move != "try-move" && move != "slide")) {
                        TraceLog(LOG_WARNING, "THING: motor move must be slide or try-move");
                        return false;
                    }
                    out.motorMoveMode = (move == "try-move") ? CharacterMoveMode::TryMove
                                                             : CharacterMoveMode::Slide;
                    continue;
                }
                if (!s7_is_number(s7_car(motorRest))) {
                    TraceLog(LOG_WARNING, "THING: unknown or malformed motor clause '%s'", motorTag);
                    return false;
                }
                const float value = static_cast<float>(s7_number_to_real(sc, s7_car(motorRest)));
                if (std::strcmp(motorTag, "radius") == 0) {
                    out.motorRadius = value;
                } else if (std::strcmp(motorTag, "height") == 0) {
                    out.motorHeight = value;
                } else if (std::strcmp(motorTag, "speed") == 0) {
                    out.motorSpeed = value;
                } else if (std::strcmp(motorTag, "gravity") == 0) {
                    out.motorGravity = value;
                } else if (std::strcmp(motorTag, "step-height") == 0) {
                    out.motorStepHeight = value;
                } else {
                    TraceLog(LOG_WARNING, "THING: unknown motor clause '%s'", motorTag);
                    return false;
                }
            }
        } else if (std::strcmp(tag, "sight") == 0) {
            out.haveSight = true;
            for (s7_pointer sightCursor = rest; s7_is_pair(sightCursor);
                 sightCursor = s7_cdr(sightCursor)) {
                s7_pointer sightClause = s7_car(sightCursor);
                if (!s7_is_pair(sightClause) || !s7_is_symbol(s7_car(sightClause))) {
                    TraceLog(LOG_WARNING, "THING: sight expected clause list");
                    return false;
                }
                const char* sightTag = s7_symbol_name(s7_car(sightClause));
                s7_pointer sightRest = s7_cdr(sightClause);
                if (std::strcmp(sightTag, "see-tags") == 0) {
                    out.sightSeeTags.clear();
                    for (s7_pointer tagCursor = sightRest; s7_is_pair(tagCursor);
                         tagCursor = s7_cdr(tagCursor)) {
                        std::string tagValue;
                        if (readString(sc, s7_car(tagCursor), tagValue) && !tagValue.empty()) {
                            out.sightSeeTags.push_back(std::move(tagValue));
                        }
                    }
                    continue;
                }
                if (std::strcmp(sightTag, "ignore-tags") == 0) {
                    out.sightIgnoreTags.clear();
                    for (s7_pointer tagCursor = sightRest; s7_is_pair(tagCursor);
                         tagCursor = s7_cdr(tagCursor)) {
                        std::string tagValue;
                        if (readString(sc, s7_car(tagCursor), tagValue) && !tagValue.empty()) {
                            out.sightIgnoreTags.push_back(std::move(tagValue));
                        }
                    }
                    continue;
                }
                if (!s7_is_pair(sightRest)) {
                    TraceLog(LOG_WARNING, "THING: malformed sight clause '%s'", sightTag);
                    return false;
                }
                if (std::strcmp(sightTag, "filter") == 0) {
                    if (!readString(sc, s7_car(sightRest), out.sightFilterProc)) {
                        TraceLog(LOG_WARNING, "THING: sight filter must be a string");
                        return false;
                    }
                    continue;
                }
                if (std::strcmp(sightTag, "enabled") == 0) {
                    if (!readBool(sc, s7_car(sightRest), out.sightEnabled)) {
                        TraceLog(LOG_WARNING, "THING: sight enabled must be bool");
                        return false;
                    }
                    continue;
                }
                if (!s7_is_number(s7_car(sightRest))) {
                    TraceLog(LOG_WARNING, "THING: unknown or malformed sight clause '%s'", sightTag);
                    return false;
                }
                const float value = static_cast<float>(s7_number_to_real(sc, s7_car(sightRest)));
                if (std::strcmp(sightTag, "range") == 0) {
                    out.sightRange = value;
                } else if (std::strcmp(sightTag, "fov") == 0) {
                    out.sightFovDegrees = value;
                } else if (std::strcmp(sightTag, "eye-lift") == 0) {
                    out.sightEyeLift = value;
                } else {
                    TraceLog(LOG_WARNING, "THING: unknown sight clause '%s'", sightTag);
                    return false;
                }
            }
        } else if (std::strcmp(tag, "color") == 0 &&
                   s7_is_pair(rest) &&
                   s7_is_pair(s7_cdr(rest)) &&
                   s7_is_pair(s7_cddr(rest))) {
            readVec3(sc, s7_car(rest), s7_cadr(rest), s7_caddr(rest), out.color);
        } else if (std::strcmp(tag, "intensity") == 0 && s7_is_pair(rest) &&
                   s7_is_number(s7_car(rest))) {
            out.intensity = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
        } else if (std::strcmp(tag, "range") == 0 && s7_is_pair(rest) &&
                   s7_is_number(s7_car(rest))) {
            out.range = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
        } else if (std::strcmp(tag, "cone") == 0 && s7_is_pair(rest) &&
                   s7_is_number(s7_car(rest))) {
            out.coneAngle = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
        } else if (std::strcmp(tag, "size") == 0 && s7_is_pair(rest) &&
                   s7_is_pair(s7_cdr(rest))) {
            readVec2(sc, s7_car(rest), s7_cadr(rest), out.size);
        } else if (std::strcmp(tag, "audio") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), out.audio);
        } else if (std::strcmp(tag, "clip") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), out.clip);
        } else if (std::strcmp(tag, "volume") == 0 && s7_is_pair(rest) &&
                   s7_is_number(s7_car(rest))) {
            out.volume = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
        } else if (std::strcmp(tag, "loop") == 0 && s7_is_pair(rest)) {
            readBool(sc, s7_car(rest), out.looping);
        } else if (std::strcmp(tag, "spatial") == 0 && s7_is_pair(rest)) {
            readBool(sc, s7_car(rest), out.spatial);
        } else if (std::strcmp(tag, "min-distance") == 0 && s7_is_pair(rest) &&
                   s7_is_number(s7_car(rest))) {
            out.minDistance = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
        } else if (std::strcmp(tag, "max-distance") == 0 && s7_is_pair(rest) &&
                   s7_is_number(s7_car(rest))) {
            out.maxDistance = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
        } else {
            TraceLog(LOG_WARNING, "THING: unknown or malformed clause '%s'", tag);
            return false;
        }
    }
    return true;
}

s7_pointer appendThing(s7_scheme* sc, Thing placement, const char* requireMsg, bool requireAt) {
    if (g_context == nullptr || g_context->doc == nullptr) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "thing called outside thing load")));
    }

    if (placement.id.empty() || (requireAt && !placement.haveAt)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, requireMsg)));
    }

    if (placement.kind == ThingKind::PlayerStart) {
        if (g_context->sawPlayerStart) {
            TraceLog(LOG_WARNING, "THING: ignoring extra player-start '%s'", placement.id.c_str());
            claimId(placement.id);
            return s7_t(sc);
        }
        g_context->sawPlayerStart = true;
        claimId(placement.id);
        g_context->doc->things.push_back(std::move(placement));
        return s7_t(sc);
    }

    if (!claimId(placement.id)) {
        return s7_f(sc);
    }

    g_context->doc->things.push_back(std::move(placement));
    return s7_t(sc);
}

s7_pointer g_player_start(s7_scheme* sc, s7_pointer args) {
    Thing placement{};
    placement.kind = ThingKind::PlayerStart;
    if (!parseThingClauses(sc, args, placement)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "player-start has invalid clauses")));
    }
    return appendThing(sc, std::move(placement), "player-start requires id and at", true);
}

s7_pointer g_prop(s7_scheme* sc, s7_pointer args) {
    Thing placement{};
    placement.kind = ThingKind::Prop;
    if (!parseThingClauses(sc, args, placement)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "prop has invalid clauses")));
    }
    return appendThing(sc, std::move(placement), "prop requires id and at", true);
}

s7_pointer g_usable(s7_scheme* sc, s7_pointer args) {
    Thing placement{};
    placement.kind = ThingKind::Usable;
    if (!parseThingClauses(sc, args, placement)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "usable has invalid clauses")));
    }
    return appendThing(sc, std::move(placement), "usable requires id and at", true);
}

s7_pointer g_pickup(s7_scheme* sc, s7_pointer args) {
    Thing placement{};
    placement.kind = ThingKind::Pickup;
    if (!parseThingClauses(sc, args, placement)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "pickup has invalid clauses")));
    }
    const bool hasSprite = !placement.sprite.empty();
    const bool hasGeo = !placement.geo.empty();
    if (hasSprite == hasGeo) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "pickup requires exactly one of sprite or geo")));
    }
    if (placement.onEnter.empty() && placement.onUse.empty()) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "pickup requires on-enter or on-use")));
    }
    return appendThing(sc, std::move(placement), "pickup requires id and at", true);
}

s7_pointer g_actor(s7_scheme* sc, s7_pointer args) {
    Thing placement{};
    placement.kind = ThingKind::Actor;
    if (!parseThingClauses(sc, args, placement)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "actor has invalid clauses")));
    }
    return appendThing(sc, std::move(placement), "actor requires id and at", true);
}

s7_pointer g_thing(s7_scheme* sc, s7_pointer args) {
    Thing probe{};
    if (!parseThingClauses(sc, args, probe)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "thing has invalid clauses")));
    }
    if (probe.type.empty()) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "thing requires type")));
    }
    const ThingDef* def = thingDefRegistry().find(probe.type);
    if (def == nullptr) {
        const std::string msg = "thing unknown type '" + probe.type + "'";
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, msg.c_str())));
    }
    Thing placement{};
    applyThingDef(*def, placement);
    if (!parseThingClauses(sc, args, placement)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "thing has invalid clauses")));
    }
    placement.kind = def->kind;
    if (placement.kind == ThingKind::Pickup) {
        const bool hasSprite = !placement.sprite.empty();
        const bool hasGeo = !placement.geo.empty();
        if (hasSprite == hasGeo) {
            return s7_error(
                sc,
                s7_make_symbol(sc, "thing-error"),
                s7_list(
                    sc,
                    1,
                    s7_make_string(sc, "pickup thing requires exactly one of sprite or geo")));
        }
        if (placement.onEnter.empty() && placement.onUse.empty()) {
            return s7_error(
                sc,
                s7_make_symbol(sc, "thing-error"),
                s7_list(sc, 1, s7_make_string(sc, "pickup thing requires on-enter or on-use")));
        }
    }
    return appendThing(sc, std::move(placement), "thing requires id and at", true);
}

s7_pointer g_mover(s7_scheme* sc, s7_pointer args) {
    Thing placement{};
    placement.kind = ThingKind::Mover;
    if (!parseThingClauses(sc, args, placement)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "mover has invalid clauses")));
    }
    const bool hasBrush = !placement.brush.empty();
    if (hasBrush && (!placement.geo.empty() || !placement.sprite.empty())) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "mover brush cannot combine with geo or sprite")));
    }
    if (!hasBrush && !placement.haveMoverCollideSize) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "mover requires collide-size or brush")));
    }
    if (!placement.haveMoverOpenOffset && !placement.haveMoverOpenAngle) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "mover requires open-offset or open-pitch/yaw/roll")));
    }
    return appendThing(
        sc,
        std::move(placement),
        "mover requires id and at (or brush)",
        !hasBrush);
}

s7_pointer g_trigger(s7_scheme* sc, s7_pointer args) {
    Thing placement{};
    placement.kind = ThingKind::Trigger;
    if (!parseThingClauses(sc, args, placement)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "trigger has invalid clauses")));
    }
    if (placement.onEnter.empty() && placement.onExit.empty()) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "trigger requires on-enter or on-exit")));
    }
    return appendThing(sc, std::move(placement), "trigger requires id and at", true);
}

s7_pointer g_point_light(s7_scheme* sc, s7_pointer args) {
    Thing placement = makeDefaultLightThing(ThingKind::PointLight);
    if (!parseThingClauses(sc, args, placement)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "point-light has invalid clauses")));
    }
    return appendThing(sc, std::move(placement), "point-light requires id and at", true);
}

s7_pointer g_spot_light(s7_scheme* sc, s7_pointer args) {
    Thing placement = makeDefaultLightThing(ThingKind::SpotLight);
    if (!parseThingClauses(sc, args, placement)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "spot-light has invalid clauses")));
    }
    return appendThing(sc, std::move(placement), "spot-light requires id and at", true);
}

s7_pointer g_area_light(s7_scheme* sc, s7_pointer args) {
    Thing placement = makeDefaultLightThing(ThingKind::AreaLight);
    if (!parseThingClauses(sc, args, placement)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "area-light has invalid clauses")));
    }
    return appendThing(sc, std::move(placement), "area-light requires id and at", true);
}

s7_pointer g_sun(s7_scheme* sc, s7_pointer args) {
    Thing placement = makeDefaultLightThing(ThingKind::Sun);
    if (!parseThingClauses(sc, args, placement)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "sun has invalid clauses")));
    }
    return appendThing(sc, std::move(placement), "sun requires id", false);
}

s7_pointer g_ambient_light(s7_scheme* sc, s7_pointer args) {
    Thing placement = makeDefaultLightThing(ThingKind::AmbientLight);
    placement.color = {0.08f, 0.08f, 0.09f};
    if (!parseThingClauses(sc, args, placement)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "ambient-light has invalid clauses")));
    }
    return appendThing(sc, std::move(placement), "ambient-light requires id", false);
}

s7_pointer g_sound_source(s7_scheme* sc, s7_pointer args) {
    Thing placement = makeDefaultSoundSourceThing();
    if (!parseThingClauses(sc, args, placement)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "sound-source has invalid clauses")));
    }
    if (placement.audio.empty() && placement.clip.empty()) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "sound-source requires audio or clip")));
    }
    return appendThing(sc, std::move(placement), "sound-source requires id and at", true);
}

s7_pointer g_marker(s7_scheme* sc, s7_pointer args) {
    Thing placement = makeDefaultMarkerThing();
    if (!parseThingClauses(sc, args, placement)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "marker has invalid clauses")));
    }
    return appendThing(sc, std::move(placement), "marker requires id and at", true);
}

s7_pointer g_prefab(s7_scheme* sc, s7_pointer args) {
    if (g_context == nullptr || g_context->doc == nullptr) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "prefab called outside thing load")));
    }

    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "prefab", 1, args, "path");
    }

    Thing placement{};
    placement.kind = ThingKind::Prefab;
    if (!readString(sc, s7_car(args), placement.prefabPath) || placement.prefabPath.empty()) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "prefab requires a path")));
    }

    if (!parseThingClauses(sc, s7_cdr(args), placement)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "prefab has invalid clauses")));
    }
    if (placement.id.empty()) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "prefab requires id")));
    }

    if (!claimId(placement.id)) {
        return s7_f(sc);
    }

    g_context->doc->things.push_back(std::move(placement));
    return s7_t(sc);
}

void bindThingApi(s7_scheme* sc) {
    s7_define_function(sc, "id", g_id, 1, 0, false, "(id value)");
    s7_define_function(sc, "type", g_type, 1, 0, false, "(type catalog-id)");
    s7_define_function(sc, "at", g_at, 3, 0, false, "(at x y z)");
    s7_define_function(sc, "yaw", g_yaw, 1, 0, false, "(yaw radians)");
    s7_define_function(sc, "pitch", g_pitch, 1, 0, false, "(pitch radians)");
    s7_define_function(sc, "angles", g_angles, 3, 0, false, "(angles pitch yaw roll)");
    s7_define_function(sc, "sprite", g_sprite, 1, 0, false, "(sprite path)");
    s7_define_function(sc, "geo", g_geo, 1, 0, false, "(geo path)");
    s7_define_function(sc, "brush", g_brush, 1, 0, false, "(brush brush-id)");
    s7_define_function(sc, "frame", g_frame, 1, 0, false, "(frame id)");
    s7_define_function(sc, "anim", g_anim, 1, 1, false, "(anim clip [loop])");
    s7_define_function(sc, "prompt", g_prompt, 1, 0, false, "(prompt text)");
    s7_define_function(sc, "on-use", g_on_use, 1, 0, true, "(on-use handler arg-clause...)");
    s7_define_function(sc, "pivot", g_pivot, 3, 0, false, "(pivot x y z)");
    s7_define_function(sc, "open-offset", g_open_offset, 3, 0, false, "(open-offset dx dy dz)");
    s7_define_function(sc, "open-pitch", g_open_pitch, 1, 0, false, "(open-pitch radians)");
    s7_define_function(sc, "open-yaw", g_open_yaw, 1, 0, false, "(open-yaw radians)");
    s7_define_function(sc, "open-roll", g_open_roll, 1, 0, false, "(open-roll radians)");
    s7_define_function(sc, "duration", g_duration, 1, 0, false, "(duration seconds)");
    s7_define_function(sc, "auto-close", g_auto_close, 1, 0, false, "(auto-close seconds)");
    s7_define_function(sc, "collide-size", g_collide_size, 3, 0, false, "(collide-size w h d)");
    s7_define_function(sc, "collide-center", g_collide_center, 3, 0, false, "(collide-center x y z)");
    s7_define_function(sc, "block-mode", g_block_mode, 1, 0, false, "(block-mode shove|crush)");
    s7_define_function(sc, "push", g_push, 1, 0, false, "(push full|horizontal|off)");
    s7_define_function(sc, "carry", g_carry, 1, 0, false, "(carry bool)");
    s7_define_function(sc, "on-crush", g_on_crush, 1, 0, false, "(on-crush handler)");
    s7_define_function(sc, "group", g_group, 1, 0, false, "(group id)");
    s7_define_function(sc, "on-enter", g_on_enter, 1, 0, true, "(on-enter handler arg-clause...)");
    s7_define_function(sc, "on-exit", g_on_exit, 1, 0, true, "(on-exit handler arg-clause...)");
    s7_define_function(sc, "trigger-size", g_trigger_size, 3, 0, false, "(trigger-size w h d)");
    s7_define_function(sc, "collide-tags", g_collide_tags, 0, 0, true, "(collide-tags values...)");
    s7_define_function(sc, "tags", g_tags, 0, 0, true, "(tags values...)");
    s7_define_function(sc, "motor", g_motor, 0, 0, true, "(motor clauses...)");
    s7_define_function(sc, "radius", g_motor_radius, 1, 0, false, "(radius value)");
    s7_define_function(sc, "height", g_motor_height, 1, 0, false, "(height value)");
    s7_define_function(sc, "speed", g_motor_speed, 1, 0, false, "(speed value)");
    s7_define_function(sc, "gravity", g_motor_gravity, 1, 0, false, "(gravity value)");
    s7_define_function(sc, "step-height", g_motor_step_height, 1, 0, false, "(step-height value)");
    s7_define_function(sc, "hull", g_motor_hull, 1, 0, false, "(hull capsule|box)");
    s7_define_function(sc, "move", g_motor_move, 1, 0, false, "(move slide|try-move)");
    s7_define_variable(sc, "capsule", s7_make_symbol(sc, "capsule"));
    s7_define_variable(sc, "box", s7_make_symbol(sc, "box"));
    s7_define_variable(sc, "slide", s7_make_symbol(sc, "slide"));
    s7_define_variable(sc, "try-move", s7_make_symbol(sc, "try-move"));
    s7_define_function(sc, "sight", g_sight, 0, 0, true, "(sight clauses...)");
    s7_define_function(sc, "fov", g_sight_fov, 1, 0, false, "(fov degrees)");
    s7_define_function(sc, "eye-lift", g_sight_eye_lift, 1, 0, false, "(eye-lift fraction)");
    s7_define_function(sc, "see-tags", g_sight_see_tags, 0, 0, true, "(see-tags values...)");
    s7_define_function(
        sc, "ignore-tags", g_sight_ignore_tags, 0, 0, true, "(ignore-tags values...)");
    s7_define_function(sc, "filter", g_sight_filter, 1, 0, false, "(filter proc-name)");
    s7_define_function(sc, "enabled", g_sight_enabled, 1, 0, false, "(enabled bool)");
    s7_define_function(sc, "color", g_color, 3, 0, false, "(color r g b)");
    s7_define_function(sc, "intensity", g_intensity, 1, 0, false, "(intensity value)");
    s7_define_function(sc, "range", g_range, 1, 0, false, "(range value)");
    s7_define_function(sc, "cone", g_cone, 1, 0, false, "(cone radians)");
    s7_define_function(sc, "size", g_size, 2, 0, false, "(size width height)");
    s7_define_function(sc, "audio", g_audio, 1, 0, false, "(audio path)");
    s7_define_function(sc, "clip", g_clip, 1, 0, false, "(clip path)");
    s7_define_function(sc, "volume", g_volume, 1, 0, false, "(volume value)");
    s7_define_function(sc, "loop", g_loop, 1, 0, false, "(loop bool)");
    s7_define_function(sc, "spatial", g_spatial, 1, 0, false, "(spatial bool)");
    s7_define_function(sc, "min-distance", g_min_distance, 1, 0, false, "(min-distance value)");
    s7_define_function(sc, "max-distance", g_max_distance, 1, 0, false, "(max-distance value)");
    s7_define_function(sc, "player-start", g_player_start, 0, 0, true, "(player-start clauses...)");
    s7_define_function(sc, "prop", g_prop, 0, 0, true, "(prop clauses...)");
    s7_define_function(sc, "usable", g_usable, 0, 0, true, "(usable clauses...)");
    s7_define_function(sc, "pickup", g_pickup, 0, 0, true, "(pickup clauses...)");
    s7_define_function(sc, "actor", g_actor, 0, 0, true, "(actor clauses...)");
    s7_define_function(sc, "thing", g_thing, 0, 0, true, "(thing clauses...)");
    s7_define_function(sc, "mover", g_mover, 0, 0, true, "(mover clauses...)");
    s7_define_function(sc, "trigger", g_trigger, 0, 0, true, "(trigger clauses...)");
    s7_define_function(sc, "point-light", g_point_light, 0, 0, true, "(point-light clauses...)");
    s7_define_function(sc, "spot-light", g_spot_light, 0, 0, true, "(spot-light clauses...)");
    s7_define_function(sc, "area-light", g_area_light, 0, 0, true, "(area-light clauses...)");
    s7_define_function(sc, "sun", g_sun, 0, 0, true, "(sun clauses...)");
    s7_define_function(sc, "ambient-light", g_ambient_light, 0, 0, true, "(ambient-light clauses...)");
    s7_define_function(sc, "sound-source", g_sound_source, 0, 0, true, "(sound-source clauses...)");
    s7_define_function(sc, "marker", g_marker, 0, 0, true, "(marker clauses...)");
    s7_define_function(sc, "prefab", g_prefab, 1, 0, true, "(prefab path clauses...)");
    bindMapHandlerArgClauses(sc);
}

std::optional<ThingDocument> evaluateThings(
    s7_scheme* scheme,
    AssetStore& assets,
    bool (*loader)(AssetStore&, s7_scheme*, std::string_view, s7_pointer),
    std::string_view virtualPath) {
    if (scheme == nullptr) {
        return std::nullopt;
    }

    ScriptScopeGuard scopeGuard(ScriptScope::MapAuthor);
    const s7_pointer env = s7_sublet(scheme, s7_rootlet(scheme), s7_nil(scheme));
    const s7_pointer previousEnv = s7_set_curlet(scheme, env);

    ThingDocument doc{};
    ThingLoadContext context{};
    context.doc = &doc;
    context.assets = &assets;
    g_context = &context;
    bindThingApi(scheme);

    const bool loaded = loader(assets, scheme, virtualPath, env);
    g_context = nullptr;
    s7_set_curlet(scheme, previousEnv);

    if (!loaded) {
        return std::nullopt;
    }
    return doc;
}

bool loadMapThingsThunk(
    AssetStore& assets,
    s7_scheme* scheme,
    std::string_view path,
    s7_pointer env) {
    return assets.loadMapThings(scheme, path, env);
}

bool loadPrefabThingsThunk(
    AssetStore& assets,
    s7_scheme* scheme,
    std::string_view path,
    s7_pointer env) {
    return assets.loadPrefabThings(scheme, path, env);
}

} // namespace

std::optional<ThingDocument> loadMapThings(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName) {
    const std::string virtualPath = std::string(mapName) + "/things";
    if (!assets.hasMapThings(virtualPath)) {
        return ThingDocument{};
    }

    auto doc = evaluateThings(scheme, assets, loadMapThingsThunk, virtualPath);
    if (!doc) {
        TraceLog(
            LOG_WARNING,
            "THING: failed to evaluate things.s7 for map '%.*s'",
            static_cast<int>(mapName.size()),
            mapName.data());
        return std::nullopt;
    }
    return doc;
}

std::optional<ThingDocument> loadPrefabThings(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view prefabPath) {
    if (!assets.hasPrefabThings(prefabPath)) {
        return ThingDocument{};
    }

    auto doc = evaluateThings(scheme, assets, loadPrefabThingsThunk, prefabPath);
    if (!doc) {
        TraceLog(
            LOG_WARNING,
            "THING: failed to evaluate prefab things for '%.*s'",
            static_cast<int>(prefabPath.size()),
            prefabPath.data());
        return std::nullopt;
    }
    return doc;
}

}
