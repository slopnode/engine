#include "map/things_script.hpp"

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
    return makeTaggedList(sc, "on-use", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_on_enter(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "on-enter", 1, args, "handler");
    }
    return makeTaggedList(sc, "on-enter", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_on_exit(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "on-exit", 1, args, "handler");
    }
    return makeTaggedList(sc, "on-exit", s7_cons(sc, s7_car(args), s7_nil(sc)));
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

void parseThingClauses(s7_scheme* sc, s7_pointer args, Thing& out) {
    for (s7_pointer cursor = args; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer clause = s7_car(cursor);
        if (!s7_is_pair(clause) || !s7_is_symbol(s7_car(clause))) {
            continue;
        }

        const char* tag = s7_symbol_name(s7_car(clause));
        s7_pointer rest = s7_cdr(clause);

        if (std::strcmp(tag, "id") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), out.id);
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
        } else if (std::strcmp(tag, "angles") == 0 &&
                   s7_is_pair(rest) &&
                   s7_is_pair(s7_cdr(rest)) &&
                   s7_is_pair(s7_cddr(rest))) {
            out.haveAngles = readVec3(sc, s7_car(rest), s7_cadr(rest), s7_caddr(rest), out.angles);
            if (out.haveAngles) {
                out.yaw = out.angles.y;
            }
        } else if (std::strcmp(tag, "sprite") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), out.sprite);
        } else if (std::strcmp(tag, "geo") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), out.geo);
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
            readString(sc, s7_car(rest), out.prompt);
        } else if (std::strcmp(tag, "on-use") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), out.onUse);
        } else if (std::strcmp(tag, "on-enter") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), out.onEnter);
        } else if (std::strcmp(tag, "on-exit") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), out.onExit);
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
        }
    }
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
    parseThingClauses(sc, args, placement);
    return appendThing(sc, std::move(placement), "player-start requires id and at", true);
}

s7_pointer g_prop(s7_scheme* sc, s7_pointer args) {
    Thing placement{};
    placement.kind = ThingKind::Prop;
    parseThingClauses(sc, args, placement);
    return appendThing(sc, std::move(placement), "prop requires id and at", true);
}

s7_pointer g_usable(s7_scheme* sc, s7_pointer args) {
    Thing placement{};
    placement.kind = ThingKind::Usable;
    parseThingClauses(sc, args, placement);
    return appendThing(sc, std::move(placement), "usable requires id and at", true);
}

s7_pointer g_trigger(s7_scheme* sc, s7_pointer args) {
    Thing placement{};
    placement.kind = ThingKind::Trigger;
    parseThingClauses(sc, args, placement);
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
    parseThingClauses(sc, args, placement);
    return appendThing(sc, std::move(placement), "point-light requires id and at", true);
}

s7_pointer g_spot_light(s7_scheme* sc, s7_pointer args) {
    Thing placement = makeDefaultLightThing(ThingKind::SpotLight);
    parseThingClauses(sc, args, placement);
    return appendThing(sc, std::move(placement), "spot-light requires id and at", true);
}

s7_pointer g_area_light(s7_scheme* sc, s7_pointer args) {
    Thing placement = makeDefaultLightThing(ThingKind::AreaLight);
    parseThingClauses(sc, args, placement);
    return appendThing(sc, std::move(placement), "area-light requires id and at", true);
}

s7_pointer g_sun(s7_scheme* sc, s7_pointer args) {
    Thing placement = makeDefaultLightThing(ThingKind::Sun);
    parseThingClauses(sc, args, placement);
    return appendThing(sc, std::move(placement), "sun requires id", false);
}

s7_pointer g_sound_source(s7_scheme* sc, s7_pointer args) {
    Thing placement = makeDefaultSoundSourceThing();
    parseThingClauses(sc, args, placement);
    if (placement.audio.empty() && placement.clip.empty()) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "sound-source requires audio or clip")));
    }
    return appendThing(sc, std::move(placement), "sound-source requires id and at", true);
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

    parseThingClauses(sc, s7_cdr(args), placement);
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
    s7_define_function(sc, "at", g_at, 3, 0, false, "(at x y z)");
    s7_define_function(sc, "yaw", g_yaw, 1, 0, false, "(yaw radians)");
    s7_define_function(sc, "angles", g_angles, 3, 0, false, "(angles pitch yaw roll)");
    s7_define_function(sc, "sprite", g_sprite, 1, 0, false, "(sprite path)");
    s7_define_function(sc, "geo", g_geo, 1, 0, false, "(geo path)");
    s7_define_function(sc, "frame", g_frame, 1, 0, false, "(frame id)");
    s7_define_function(sc, "anim", g_anim, 1, 1, false, "(anim clip [loop])");
    s7_define_function(sc, "prompt", g_prompt, 1, 0, false, "(prompt text)");
    s7_define_function(sc, "on-use", g_on_use, 1, 0, false, "(on-use handler)");
    s7_define_function(sc, "on-enter", g_on_enter, 1, 0, false, "(on-enter handler)");
    s7_define_function(sc, "on-exit", g_on_exit, 1, 0, false, "(on-exit handler)");
    s7_define_function(sc, "trigger-size", g_trigger_size, 3, 0, false, "(trigger-size w h d)");
    s7_define_function(sc, "collide-tags", g_collide_tags, 0, 0, true, "(collide-tags values...)");
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
    s7_define_function(sc, "trigger", g_trigger, 0, 0, true, "(trigger clauses...)");
    s7_define_function(sc, "point-light", g_point_light, 0, 0, true, "(point-light clauses...)");
    s7_define_function(sc, "spot-light", g_spot_light, 0, 0, true, "(spot-light clauses...)");
    s7_define_function(sc, "area-light", g_area_light, 0, 0, true, "(area-light clauses...)");
    s7_define_function(sc, "sun", g_sun, 0, 0, true, "(sun clauses...)");
    s7_define_function(sc, "sound-source", g_sound_source, 0, 0, true, "(sound-source clauses...)");
    s7_define_function(sc, "prefab", g_prefab, 1, 0, true, "(prefab path clauses...)");
}

std::optional<ThingDocument> evaluateThings(
    s7_scheme* scheme,
    AssetStore& assets,
    bool (*loader)(AssetStore&, s7_scheme*, std::string_view),
    std::string_view virtualPath) {
    if (scheme == nullptr) {
        return std::nullopt;
    }

    ThingDocument doc{};
    ThingLoadContext context{};
    context.doc = &doc;
    context.assets = &assets;
    g_context = &context;
    bindThingApi(scheme);

    const bool loaded = loader(assets, scheme, virtualPath);
    g_context = nullptr;

    if (!loaded) {
        return std::nullopt;
    }
    return doc;
}

bool loadMapThingsThunk(AssetStore& assets, s7_scheme* scheme, std::string_view path) {
    return assets.loadMapThings(scheme, path);
}

bool loadPrefabThingsThunk(AssetStore& assets, s7_scheme* scheme, std::string_view path) {
    return assets.loadPrefabThings(scheme, path);
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
