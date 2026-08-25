#include "map/csg_script.hpp"

#include "script/script_scope.hpp"

#include "map/brush.hpp"
#include "map/map_handler_registry.hpp"
#include "map/thing_def_registry.hpp"
#include "map/bsp.hpp"
#include "map/bsp_analyze.hpp"
#include "map/bsp_io.hpp"
#include "map/csg_compile.hpp"
#include "map/lightmap.hpp"
#include "assets/texture_anim_loader.hpp"
#include "map/map_meta.hpp"
#include "map/mover_brushes.hpp"
#include "map/prefab.hpp"
#include "map/pvs_io.hpp"
#include "map/things_script.hpp"

#include <algorithm>
#include <raylib.h>
#include <s7.h>

#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace slopengine {

namespace {

struct CsgBuilder {
    AssetStore* assets = nullptr;
    std::vector<Brush> brushes;
    std::vector<PrefabInstance> instances;
    std::vector<std::string> nestStack;
    bool recordTopLevelInstances = false;
};

CsgBuilder* g_builder = nullptr;

void bindCsgApi(s7_scheme* sc);

MaterialUvInfo resolveMaterialUv(AssetStore& assets, std::string_view materialPath);

bool expandPrefabIntoBrushes(
    s7_scheme* sc,
    AssetStore& assets,
    const PrefabInstance& instance,
    const std::vector<std::string>& parentNest,
    std::vector<Brush>& outBrushes);

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

bool readUvAxes(s7_scheme* sc, s7_pointer rest, BrushFace& face) {
    s7_pointer cursor = rest;
    float values[6]{};
    for (int i = 0; i < 6; ++i) {
        if (!s7_is_pair(cursor) || !s7_is_number(s7_car(cursor))) {
            return false;
        }
        values[i] = static_cast<float>(s7_number_to_real(sc, s7_car(cursor)));
        cursor = s7_cdr(cursor);
    }
    face.uvUAxis = {values[0], values[1], values[2]};
    face.uvVAxis = {values[3], values[4], values[5]};
    return true;
}

std::optional<BrushBoxSide> parseSide(s7_pointer value) {
    if (!s7_is_symbol(value)) {
        return std::nullopt;
    }
    const char* name = s7_symbol_name(value);
    if (std::strcmp(name, "top") == 0) {
        return BrushBoxSide::Top;
    }
    if (std::strcmp(name, "bottom") == 0) {
        return BrushBoxSide::Bottom;
    }
    if (std::strcmp(name, "north") == 0) {
        return BrushBoxSide::North;
    }
    if (std::strcmp(name, "south") == 0) {
        return BrushBoxSide::South;
    }
    if (std::strcmp(name, "east") == 0) {
        return BrushBoxSide::East;
    }
    if (std::strcmp(name, "west") == 0) {
        return BrushBoxSide::West;
    }
    return std::nullopt;
}

bool parseFaceOverride(s7_scheme* sc, s7_pointer form, BrushBoxSide& side, BrushFace& face) {
    if (!s7_is_pair(form)) {
        return false;
    }

    const auto parsedSide = parseSide(s7_car(form));
    if (!parsedSide) {
        return false;
    }
    side = *parsedSide;

    for (s7_pointer cursor = s7_cdr(form); s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer clause = s7_car(cursor);
        if (!s7_is_pair(clause) || !s7_is_symbol(s7_car(clause))) {
            continue;
        }
        const char* tag = s7_symbol_name(s7_car(clause));
        s7_pointer rest = s7_cdr(clause);
        if (std::strcmp(tag, "id") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), face.id);
        } else if (std::strcmp(tag, "material") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), face.material);
        } else if (std::strcmp(tag, "uv-shift") == 0 &&
                   s7_is_pair(rest) &&
                   s7_is_pair(s7_cdr(rest)) &&
                   s7_is_number(s7_car(rest)) &&
                   s7_is_number(s7_cadr(rest))) {
            face.uvShiftPixels.x = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
            face.uvShiftPixels.y = static_cast<float>(s7_number_to_real(sc, s7_cadr(rest)));
        } else if (std::strcmp(tag, "uv-scale") == 0 &&
                   s7_is_pair(rest) &&
                   s7_is_pair(s7_cdr(rest)) &&
                   s7_is_number(s7_car(rest)) &&
                   s7_is_number(s7_cadr(rest))) {
            face.uvScale.x = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
            face.uvScale.y = static_cast<float>(s7_number_to_real(sc, s7_cadr(rest)));
        } else if (std::strcmp(tag, "nodraw") == 0) {
            face.nodraw = true;
        } else if (std::strcmp(tag, "uv-lock") == 0) {
            face.uvLock = true;
        } else if (std::strcmp(tag, "uv-axes") == 0) {
            readUvAxes(sc, rest, face);
        } else if (std::strcmp(tag, "on-use") == 0 && s7_is_pair(rest)) {
            if (parseHandlerBinding(sc, rest, face.onUse)) {
                mapHandlerRegistry().refineBinding(face.onUse, MapHandlerKind::Use);
            }
        } else if (std::strcmp(tag, "on-touch") == 0 && s7_is_pair(rest)) {
            if (parseHandlerBinding(sc, rest, face.onTouch)) {
                mapHandlerRegistry().refineBinding(face.onTouch, MapHandlerKind::Touch);
            }
        }
    }

    return true;
}

s7_pointer g_id(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "id", 1, args, "value");
    }
    return makeTaggedList(sc, "id", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_mins(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args)) || !s7_is_pair(s7_cddr(args))) {
        return s7_wrong_type_arg_error(sc, "mins", 0, args, "x y z");
    }
    return makeTaggedList(
        sc,
        "mins",
        s7_list(sc, 3, s7_car(args), s7_cadr(args), s7_caddr(args)));
}

s7_pointer g_maxs(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args)) || !s7_is_pair(s7_cddr(args))) {
        return s7_wrong_type_arg_error(sc, "maxs", 0, args, "x y z");
    }
    return makeTaggedList(
        sc,
        "maxs",
        s7_list(sc, 3, s7_car(args), s7_cadr(args), s7_caddr(args)));
}

s7_pointer g_material(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "material", 1, args, "string");
    }
    return makeTaggedList(sc, "material", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_role(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "role", 1, args, "hull|detail|door|hint|trigger|water|window");
    }
    return makeTaggedList(sc, "role", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_uv_shift(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args))) {
        return s7_wrong_type_arg_error(sc, "uv-shift", 0, args, "x y");
    }
    return makeTaggedList(sc, "uv-shift", s7_list(sc, 2, s7_car(args), s7_cadr(args)));
}

s7_pointer g_uv_scale(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args))) {
        return s7_wrong_type_arg_error(sc, "uv-scale", 0, args, "sx sy");
    }
    return makeTaggedList(sc, "uv-scale", s7_list(sc, 2, s7_car(args), s7_cadr(args)));
}

s7_pointer g_nodraw(s7_scheme* sc, s7_pointer args) {
    (void)args;
    return makeTaggedList(sc, "nodraw", s7_nil(sc));
}

s7_pointer g_on_use(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "on-use", 1, args, "handler");
    }
    return makeTaggedList(sc, "on-use", args);
}

s7_pointer g_on_touch(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "on-touch", 1, args, "handler");
    }
    return makeTaggedList(sc, "on-touch", args);
}

s7_pointer g_can_use(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "can-use", 1, args, "handler");
    }
    return makeTaggedList(sc, "can-use", args);
}

s7_pointer g_motion(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "motion", 1, args, "raise|slide|swing");
    }
    return makeTaggedList(sc, "motion", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_travel(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "travel", 1, args, "distance");
    }
    return makeTaggedList(sc, "travel", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_angle(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "angle", 1, args, "radians");
    }
    return makeTaggedList(sc, "angle", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_axis(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "axis", 1, args, "pitch|yaw|roll");
    }
    return makeTaggedList(sc, "axis", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_hinge(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "hinge", 1, args, "thing-id");
    }
    return makeTaggedList(sc, "hinge", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_group(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "group", 1, args, "id");
    }
    return makeTaggedList(sc, "group", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_prompt(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "prompt", 1, args, "string");
    }
    return makeTaggedList(sc, "prompt", s7_cons(sc, s7_car(args), s7_nil(sc)));
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

s7_pointer g_door(s7_scheme* sc, s7_pointer args) {
    return makeTaggedList(sc, "door", args);
}

s7_pointer g_open_sound(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "open-sound", 1, args, "path");
    }
    return makeTaggedList(sc, "open-sound", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_close_sound(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "close-sound", 1, args, "path");
    }
    return makeTaggedList(sc, "close-sound", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_sound_volume(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "sound-volume", 1, args, "volume");
    }
    return makeTaggedList(sc, "sound-volume", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

bool parseBrushDoor(s7_scheme* sc, s7_pointer rest, BrushDoor& out) {
    BrushDoor door{};
    for (s7_pointer cursor = rest; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer clause = s7_car(cursor);
        if (!s7_is_pair(clause) || !s7_is_symbol(s7_car(clause))) {
            continue;
        }
        const char* tag = s7_symbol_name(s7_car(clause));
        s7_pointer args = s7_cdr(clause);
        if (std::strcmp(tag, "motion") == 0 && s7_is_pair(args)) {
            std::string name;
            if (readString(sc, s7_car(args), name)) {
                DoorMotion motion = DoorMotion::Raise;
                if (parseDoorMotionName(name, motion)) {
                    door.motion = motion;
                }
            }
        } else if (std::strcmp(tag, "duration") == 0 && s7_is_pair(args) && s7_is_number(s7_car(args))) {
            door.duration = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
            door.haveDuration = true;
        } else if (std::strcmp(tag, "auto-close") == 0 && s7_is_pair(args) && s7_is_number(s7_car(args))) {
            door.autoClose = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
            door.haveAutoClose = true;
        } else if (std::strcmp(tag, "angle") == 0 && s7_is_pair(args) && s7_is_number(s7_car(args))) {
            door.angle = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
            door.haveAngle = true;
        } else if (std::strcmp(tag, "travel") == 0 && s7_is_pair(args) && s7_is_number(s7_car(args))) {
            door.travel = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
            door.haveTravel = true;
        } else if (std::strcmp(tag, "axis") == 0 && s7_is_pair(args)) {
            std::string name;
            if (readString(sc, s7_car(args), name)) {
                DoorAxis axis = DoorAxis::Yaw;
                if (parseDoorAxisName(name, axis)) {
                    door.axis = axis;
                    door.haveAxis = true;
                }
            }
        } else if (std::strcmp(tag, "hinge") == 0 && s7_is_pair(args)) {
            readString(sc, s7_car(args), door.hingeThingId);
        } else if (std::strcmp(tag, "group") == 0 && s7_is_pair(args)) {
            readString(sc, s7_car(args), door.group);
        } else if (std::strcmp(tag, "can-use") == 0 && s7_is_pair(args)) {
            if (parseHandlerBinding(sc, args, door.canUse)) {
                mapHandlerRegistry().refineBinding(door.canUse, MapHandlerKind::CanUse);
            }
        } else if (std::strcmp(tag, "open-sound") == 0 && s7_is_pair(args)) {
            readString(sc, s7_car(args), door.openSound);
        } else if (std::strcmp(tag, "close-sound") == 0 && s7_is_pair(args)) {
            readString(sc, s7_car(args), door.closeSound);
        } else if (std::strcmp(tag, "sound-volume") == 0 && s7_is_pair(args) && s7_is_number(s7_car(args))) {
            door.soundVolume = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
            door.haveSoundVolume = true;
        }
    }
    out = std::move(door);
    return true;
}

s7_pointer g_tint(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args)) || !s7_is_pair(s7_cddr(args))) {
        return s7_wrong_type_arg_error(sc, "tint", 0, args, "r g b");
    }
    return makeTaggedList(
        sc,
        "tint",
        s7_list(sc, 3, s7_car(args), s7_cadr(args), s7_caddr(args)));
}

s7_pointer g_wobble(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "wobble", 1, args, "amount");
    }
    return makeTaggedList(sc, "wobble", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_vignette(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "vignette", 1, args, "amount");
    }
    return makeTaggedList(sc, "vignette", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_water(s7_scheme* sc, s7_pointer args) {
    return makeTaggedList(sc, "water", args);
}

bool parseBrushWater(s7_scheme* sc, s7_pointer rest, BrushWater& out) {
    BrushWater water{};
    for (s7_pointer cursor = rest; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer clause = s7_car(cursor);
        if (!s7_is_pair(clause) || !s7_is_symbol(s7_car(clause))) {
            continue;
        }
        const char* tag = s7_symbol_name(s7_car(clause));
        s7_pointer args = s7_cdr(clause);
        if (std::strcmp(tag, "tint") == 0 &&
            s7_is_pair(args) &&
            s7_is_pair(s7_cdr(args)) &&
            s7_is_pair(s7_cddr(args))) {
            water.haveTint = readVec3(sc, s7_car(args), s7_cadr(args), s7_caddr(args), water.tint);
        } else if (std::strcmp(tag, "wobble") == 0 && s7_is_pair(args) && s7_is_number(s7_car(args))) {
            water.wobble = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
            water.haveWobble = true;
        } else if (std::strcmp(tag, "vignette") == 0 && s7_is_pair(args) && s7_is_number(s7_car(args))) {
            water.vignette = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
            water.haveVignette = true;
        }
    }
    out = std::move(water);
    return true;
}

s7_pointer g_uv_lock(s7_scheme* sc, s7_pointer args) {
    (void)args;
    return makeTaggedList(sc, "uv-lock", s7_nil(sc));
}

s7_pointer g_uv_axes(s7_scheme* sc, s7_pointer args) {
    s7_pointer cursor = args;
    for (int i = 0; i < 6; ++i) {
        if (!s7_is_pair(cursor)) {
            return s7_wrong_type_arg_error(sc, "uv-axes", 0, args, "ux uy uz vx vy vz");
        }
        cursor = s7_cdr(cursor);
    }
    return makeTaggedList(sc, "uv-axes", args);
}

s7_pointer g_nocollide(s7_scheme* sc, s7_pointer args) {
    (void)args;
    return makeTaggedList(sc, "nocollide", s7_nil(sc));
}

s7_pointer g_no_block_los(s7_scheme* sc, s7_pointer args) {
    (void)args;
    return makeTaggedList(sc, "no-block-los", s7_nil(sc));
}

s7_pointer g_no_block_linescan(s7_scheme* sc, s7_pointer args) {
    (void)args;
    return makeTaggedList(sc, "no-block-linescan", s7_nil(sc));
}

s7_pointer g_no_block_projectile(s7_scheme* sc, s7_pointer args) {
    (void)args;
    return makeTaggedList(sc, "no-block-projectile", s7_nil(sc));
}

s7_pointer g_no_block_player(s7_scheme* sc, s7_pointer args) {
    (void)args;
    return makeTaggedList(sc, "no-block-player", s7_nil(sc));
}

s7_pointer g_no_block_actor(s7_scheme* sc, s7_pointer args) {
    (void)args;
    return makeTaggedList(sc, "no-block-actor", s7_nil(sc));
}

s7_pointer g_block_los(s7_scheme* sc, s7_pointer args) {
    (void)args;
    return makeTaggedList(sc, "block-los", s7_nil(sc));
}

s7_pointer g_block_linescan(s7_scheme* sc, s7_pointer args) {
    (void)args;
    return makeTaggedList(sc, "block-linescan", s7_nil(sc));
}

s7_pointer g_block_projectile(s7_scheme* sc, s7_pointer args) {
    (void)args;
    return makeTaggedList(sc, "block-projectile", s7_nil(sc));
}

s7_pointer g_block_player(s7_scheme* sc, s7_pointer args) {
    (void)args;
    return makeTaggedList(sc, "block-player", s7_nil(sc));
}

s7_pointer g_block_actor(s7_scheme* sc, s7_pointer args) {
    (void)args;
    return makeTaggedList(sc, "block-actor", s7_nil(sc));
}

enum class BlockTagTriState {
    Default,
    Clear,
    Set,
};

struct BrushBlockParseState {
    bool nocollide = false;
    BlockTagTriState los = BlockTagTriState::Default;
    BlockTagTriState linescan = BlockTagTriState::Default;
    BlockTagTriState projectile = BlockTagTriState::Default;
    BlockTagTriState player = BlockTagTriState::Default;
    BlockTagTriState actor = BlockTagTriState::Default;

    bool applyTag(const char* tag) {
        if (std::strcmp(tag, "nocollide") == 0) {
            nocollide = true;
            return true;
        }
        if (std::strcmp(tag, "no-block-los") == 0) {
            los = BlockTagTriState::Clear;
            return true;
        }
        if (std::strcmp(tag, "no-block-linescan") == 0) {
            linescan = BlockTagTriState::Clear;
            return true;
        }
        if (std::strcmp(tag, "no-block-projectile") == 0) {
            projectile = BlockTagTriState::Clear;
            return true;
        }
        if (std::strcmp(tag, "no-block-player") == 0) {
            player = BlockTagTriState::Clear;
            return true;
        }
        if (std::strcmp(tag, "no-block-actor") == 0) {
            actor = BlockTagTriState::Clear;
            return true;
        }
        if (std::strcmp(tag, "block-los") == 0) {
            los = BlockTagTriState::Set;
            return true;
        }
        if (std::strcmp(tag, "block-linescan") == 0) {
            linescan = BlockTagTriState::Set;
            return true;
        }
        if (std::strcmp(tag, "block-projectile") == 0) {
            projectile = BlockTagTriState::Set;
            return true;
        }
        if (std::strcmp(tag, "block-player") == 0) {
            player = BlockTagTriState::Set;
            return true;
        }
        if (std::strcmp(tag, "block-actor") == 0) {
            actor = BlockTagTriState::Set;
            return true;
        }
        return false;
    }

    std::uint8_t resolve(BrushRole role) const {
        std::uint8_t blocks = brushRoleDefaultBlocks(role);
        if (nocollide) {
            blocks = 0;
        }
        auto apply = [&](BlockTagTriState state, std::uint8_t bit) {
            if (state == BlockTagTriState::Clear) {
                blocks &= static_cast<std::uint8_t>(~bit);
            } else if (state == BlockTagTriState::Set) {
                blocks |= bit;
            }
        };
        apply(los, BrushBlock::Los);
        apply(linescan, BrushBlock::Linescan);
        apply(projectile, BrushBlock::Projectile);
        apply(player, BrushBlock::Player);
        apply(actor, BrushBlock::Actor);
        return blocks;
    }
};

s7_pointer g_faces(s7_scheme* sc, s7_pointer args) {
    return makeTaggedList(sc, "faces", args);
}

s7_pointer g_face(s7_scheme* sc, s7_pointer args) {
    return makeTaggedList(sc, "face", args);
}

s7_pointer g_verts(s7_scheme* sc, s7_pointer args) {
    return makeTaggedList(sc, "verts", args);
}

s7_pointer g_v(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args)) || !s7_is_pair(s7_cddr(args))) {
        return s7_wrong_type_arg_error(sc, "v", 0, args, "x y z");
    }
    return makeTaggedList(
        sc,
        "v",
        s7_list(sc, 3, s7_car(args), s7_cadr(args), s7_caddr(args)));
}

s7_pointer makeSideForm(s7_scheme* sc, const char* side, s7_pointer args) {
    return s7_cons(sc, s7_make_symbol(sc, side), args);
}

s7_pointer g_top(s7_scheme* sc, s7_pointer args) {
    return makeSideForm(sc, "top", args);
}

s7_pointer g_bottom(s7_scheme* sc, s7_pointer args) {
    return makeSideForm(sc, "bottom", args);
}

s7_pointer g_north(s7_scheme* sc, s7_pointer args) {
    return makeSideForm(sc, "north", args);
}

s7_pointer g_south(s7_scheme* sc, s7_pointer args) {
    return makeSideForm(sc, "south", args);
}

s7_pointer g_east(s7_scheme* sc, s7_pointer args) {
    return makeSideForm(sc, "east", args);
}

s7_pointer g_west(s7_scheme* sc, s7_pointer args) {
    return makeSideForm(sc, "west", args);
}

s7_pointer g_at(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args)) || !s7_is_pair(s7_cddr(args))) {
        return s7_wrong_type_arg_error(sc, "at", 0, args, "x y z");
    }
    return makeTaggedList(sc, "at", s7_list(sc, 3, s7_car(args), s7_cadr(args), s7_caddr(args)));
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

bool expandPrefabIntoBrushes(
    s7_scheme* sc,
    AssetStore& assets,
    const PrefabInstance& instance,
    const std::vector<std::string>& parentNest,
    std::vector<Brush>& outBrushes) {
    if (std::find(parentNest.begin(), parentNest.end(), instance.path) != parentNest.end()) {
        return false;
    }
    if (!assets.hasPrefabCsg(instance.path)) {
        return false;
    }

    CsgBuilder child;
    child.assets = &assets;
    child.recordTopLevelInstances = false;
    child.nestStack = parentNest;
    child.nestStack.push_back(instance.path);

    ScriptScopeGuard scopeGuard(ScriptScope::MapAuthor);
    const s7_pointer env = s7_sublet(sc, s7_rootlet(sc), s7_nil(sc));
    const s7_pointer previousEnv = s7_set_curlet(sc, env);

    CsgBuilder* previous = g_builder;
    g_builder = &child;
    bindCsgApi(sc);
    const bool loaded = assets.loadPrefabCsg(sc, instance.path, env);
    g_builder = previous;
    s7_set_curlet(sc, previousEnv);
    if (!loaded || child.brushes.empty()) {
        return false;
    }

    for (Brush& brush : child.brushes) {
        remapBrushIds(brush, instance.id);
        transformBrush(
            brush,
            instance.at,
            instance.angles,
            [&assets](std::string_view materialPath) {
                return resolveMaterialUv(assets, materialPath);
            });
        outBrushes.push_back(std::move(brush));
    }
    return true;
}

s7_pointer g_prefab(s7_scheme* sc, s7_pointer args) {
    if (g_builder == nullptr || g_builder->assets == nullptr) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "csg-error"),
            s7_list(sc, 1, s7_make_string(sc, "prefab called outside map load")));
    }

    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "prefab", 1, args, "path");
    }

    std::string path;
    if (!readString(sc, s7_car(args), path) || path.empty()) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "csg-error"),
            s7_list(sc, 1, s7_make_string(sc, "prefab requires a path")));
    }

    std::string id;
    Vector3 at{};
    Vector3 angles{};

    for (s7_pointer cursor = s7_cdr(args); s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer clause = s7_car(cursor);
        if (!s7_is_pair(clause) || !s7_is_symbol(s7_car(clause))) {
            continue;
        }
        const char* tag = s7_symbol_name(s7_car(clause));
        s7_pointer rest = s7_cdr(clause);
        if (std::strcmp(tag, "id") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), id);
        } else if (std::strcmp(tag, "at") == 0 &&
                   s7_is_pair(rest) &&
                   s7_is_pair(s7_cdr(rest)) &&
                   s7_is_pair(s7_cddr(rest))) {
            readVec3(sc, s7_car(rest), s7_cadr(rest), s7_caddr(rest), at);
        } else if (std::strcmp(tag, "angles") == 0 &&
                   s7_is_pair(rest) &&
                   s7_is_pair(s7_cdr(rest)) &&
                   s7_is_pair(s7_cddr(rest))) {
            readVec3(sc, s7_car(rest), s7_cadr(rest), s7_caddr(rest), angles);
        }
    }

    if (id.empty()) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "csg-error"),
            s7_list(sc, 1, s7_make_string(sc, "prefab requires id")));
    }

    if (!g_builder->assets->hasPrefabCsg(path)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "csg-error"),
            s7_list(sc, 1, s7_make_string(sc, "prefab CSG not found")));
    }

    PrefabInstance instance;
    instance.path = path;
    instance.id = id;
    instance.at = at;
    instance.angles = angles;

    if (g_builder->recordTopLevelInstances && g_builder->nestStack.empty()) {
        g_builder->instances.push_back(std::move(instance));
        return s7_t(sc);
    }

    if (std::find(g_builder->nestStack.begin(), g_builder->nestStack.end(), path) !=
        g_builder->nestStack.end()) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "csg-error"),
            s7_list(sc, 1, s7_make_string(sc, "prefab cycle detected")));
    }

    if (!expandPrefabIntoBrushes(
            sc,
            *g_builder->assets,
            instance,
            g_builder->nestStack,
            g_builder->brushes)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "csg-error"),
            s7_list(sc, 1, s7_make_string(sc, "prefab produced no brushes")));
    }

    return s7_t(sc);
}

s7_pointer g_brush_box(s7_scheme* sc, s7_pointer args) {
    if (g_builder == nullptr) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "csg-error"),
            s7_list(sc, 1, s7_make_string(sc, "brush-box called outside map load")));
    }

    std::string id;
    std::string material = "default/cube";
    BrushRole role = BrushRole::Hull;
    Vector3 mins{};
    Vector3 maxs{};
    bool haveMins = false;
    bool haveMaxs = false;
    BrushBlockParseState blockState{};
    bool haveDoorClause = false;
    BrushDoor door{};
    bool haveWaterClause = false;
    BrushWater water{};
    std::vector<std::pair<BrushBoxSide, BrushFace>> overrides;

    for (s7_pointer cursor = args; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer clause = s7_car(cursor);
        if (!s7_is_pair(clause) || !s7_is_symbol(s7_car(clause))) {
            continue;
        }

        const char* tag = s7_symbol_name(s7_car(clause));
        s7_pointer rest = s7_cdr(clause);

        if (std::strcmp(tag, "id") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), id);
        } else if (std::strcmp(tag, "material") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), material);
        } else if (std::strcmp(tag, "role") == 0 && s7_is_pair(rest)) {
            std::string roleName;
            if (readString(sc, s7_car(rest), roleName)) {
                BrushRole parsed = BrushRole::Hull;
                if (parseBrushRoleName(roleName, parsed)) {
                    role = parsed;
                }
            }
        } else if (blockState.applyTag(tag)) {
        } else if (std::strcmp(tag, "door") == 0) {
            if (parseBrushDoor(sc, rest, door)) {
                haveDoorClause = true;
            }
        } else if (std::strcmp(tag, "water") == 0) {
            if (parseBrushWater(sc, rest, water)) {
                haveWaterClause = true;
            }
        } else if (std::strcmp(tag, "mins") == 0 &&
                   s7_is_pair(rest) &&
                   s7_is_pair(s7_cdr(rest)) &&
                   s7_is_pair(s7_cddr(rest))) {
            haveMins = readVec3(sc, s7_car(rest), s7_cadr(rest), s7_caddr(rest), mins);
        } else if (std::strcmp(tag, "maxs") == 0 &&
                   s7_is_pair(rest) &&
                   s7_is_pair(s7_cdr(rest)) &&
                   s7_is_pair(s7_cddr(rest))) {
            haveMaxs = readVec3(sc, s7_car(rest), s7_cadr(rest), s7_caddr(rest), maxs);
        } else if (std::strcmp(tag, "faces") == 0) {
            for (s7_pointer faceCursor = rest; s7_is_pair(faceCursor); faceCursor = s7_cdr(faceCursor)) {
                BrushBoxSide side{};
                BrushFace face{};
                if (parseFaceOverride(sc, s7_car(faceCursor), side, face)) {
                    overrides.emplace_back(side, std::move(face));
                }
            }
        }
    }

    if (id.empty() || !haveMins || !haveMaxs) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "csg-error"),
            s7_list(sc, 1, s7_make_string(sc, "brush-box requires id, mins, and maxs")));
    }

    if (haveDoorClause) {
        role = BrushRole::Door;
    }
    if (haveWaterClause) {
        role = BrushRole::Water;
    }
    Brush brush = makeBrushBox(std::move(id), mins, maxs, material, overrides, role);
    setBrushBlocks(brush, blockState.resolve(role));
    if (role == BrushRole::Door) {
        brush.door = haveDoorClause ? std::move(door) : BrushDoor{};
    }
    if (role == BrushRole::Water) {
        brush.water = haveWaterClause ? std::move(water) : BrushWater{};
    }
    g_builder->brushes.push_back(std::move(brush));
    return s7_t(sc);
}

bool parseConvexFace(s7_scheme* sc, s7_pointer form, BrushFace& face) {
    if (!s7_is_pair(form) || !s7_is_symbol(s7_car(form))) {
        return false;
    }
    if (std::strcmp(s7_symbol_name(s7_car(form)), "face") != 0) {
        return false;
    }

    for (s7_pointer cursor = s7_cdr(form); s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer clause = s7_car(cursor);
        if (!s7_is_pair(clause) || !s7_is_symbol(s7_car(clause))) {
            continue;
        }
        const char* tag = s7_symbol_name(s7_car(clause));
        s7_pointer rest = s7_cdr(clause);
        if (std::strcmp(tag, "id") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), face.id);
        } else if (std::strcmp(tag, "material") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), face.material);
        } else if (std::strcmp(tag, "uv-shift") == 0 &&
                   s7_is_pair(rest) &&
                   s7_is_pair(s7_cdr(rest)) &&
                   s7_is_number(s7_car(rest)) &&
                   s7_is_number(s7_cadr(rest))) {
            face.uvShiftPixels.x = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
            face.uvShiftPixels.y = static_cast<float>(s7_number_to_real(sc, s7_cadr(rest)));
        } else if (std::strcmp(tag, "uv-scale") == 0 &&
                   s7_is_pair(rest) &&
                   s7_is_pair(s7_cdr(rest)) &&
                   s7_is_number(s7_car(rest)) &&
                   s7_is_number(s7_cadr(rest))) {
            face.uvScale.x = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
            face.uvScale.y = static_cast<float>(s7_number_to_real(sc, s7_cadr(rest)));
        } else if (std::strcmp(tag, "nodraw") == 0) {
            face.nodraw = true;
        } else if (std::strcmp(tag, "uv-lock") == 0) {
            face.uvLock = true;
        } else if (std::strcmp(tag, "uv-axes") == 0) {
            readUvAxes(sc, rest, face);
        } else if (std::strcmp(tag, "on-use") == 0 && s7_is_pair(rest)) {
            if (parseHandlerBinding(sc, rest, face.onUse)) {
                mapHandlerRegistry().refineBinding(face.onUse, MapHandlerKind::Use);
            }
        } else if (std::strcmp(tag, "on-touch") == 0 && s7_is_pair(rest)) {
            if (parseHandlerBinding(sc, rest, face.onTouch)) {
                mapHandlerRegistry().refineBinding(face.onTouch, MapHandlerKind::Touch);
            }
        } else if (std::strcmp(tag, "verts") == 0) {
            for (s7_pointer vertCursor = rest; s7_is_pair(vertCursor); vertCursor = s7_cdr(vertCursor)) {
                s7_pointer vert = s7_car(vertCursor);
                if (!s7_is_pair(vert) || !s7_is_symbol(s7_car(vert))) {
                    continue;
                }
                if (std::strcmp(s7_symbol_name(s7_car(vert)), "v") != 0) {
                    continue;
                }
                s7_pointer coords = s7_cdr(vert);
                if (!s7_is_pair(coords) || !s7_is_pair(s7_cdr(coords)) || !s7_is_pair(s7_cddr(coords))) {
                    continue;
                }
                Vector3 point{};
                if (readVec3(sc, s7_car(coords), s7_cadr(coords), s7_caddr(coords), point)) {
                    face.vertices.push_back(point);
                }
            }
        }
    }
    return face.vertices.size() >= 3;
}

s7_pointer g_brush_convex(s7_scheme* sc, s7_pointer args) {
    if (g_builder == nullptr) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "csg-error"),
            s7_list(sc, 1, s7_make_string(sc, "brush-convex called outside map load")));
    }

    std::string id;
    std::string defaultMaterial = "default/cube";
    BrushRole role = BrushRole::Hull;
    BrushBlockParseState blockState{};
    bool haveDoorClause = false;
    BrushDoor door{};
    bool haveWaterClause = false;
    BrushWater water{};
    std::vector<BrushFace> faces;

    for (s7_pointer cursor = args; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer clause = s7_car(cursor);
        if (!s7_is_pair(clause) || !s7_is_symbol(s7_car(clause))) {
            continue;
        }
        const char* tag = s7_symbol_name(s7_car(clause));
        s7_pointer rest = s7_cdr(clause);
        if (std::strcmp(tag, "id") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), id);
        } else if (std::strcmp(tag, "material") == 0 && s7_is_pair(rest)) {
            readString(sc, s7_car(rest), defaultMaterial);
        } else if (std::strcmp(tag, "role") == 0 && s7_is_pair(rest)) {
            std::string roleName;
            if (readString(sc, s7_car(rest), roleName)) {
                BrushRole parsed = BrushRole::Hull;
                if (parseBrushRoleName(roleName, parsed)) {
                    role = parsed;
                }
            }
        } else if (blockState.applyTag(tag)) {
        } else if (std::strcmp(tag, "door") == 0) {
            if (parseBrushDoor(sc, rest, door)) {
                haveDoorClause = true;
            }
        } else if (std::strcmp(tag, "water") == 0) {
            if (parseBrushWater(sc, rest, water)) {
                haveWaterClause = true;
            }
        } else if (std::strcmp(tag, "faces") == 0) {
            for (s7_pointer faceCursor = rest; s7_is_pair(faceCursor); faceCursor = s7_cdr(faceCursor)) {
                BrushFace face{};
                if (parseConvexFace(sc, s7_car(faceCursor), face)) {
                    if (face.material.empty()) {
                        face.material = defaultMaterial;
                    }
                    faces.push_back(std::move(face));
                }
            }
        }
    }

    if (id.empty() || faces.empty()) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "csg-error"),
            s7_list(sc, 1, s7_make_string(sc, "brush-convex requires id and faces")));
    }

    if (haveDoorClause) {
        role = BrushRole::Door;
    }
    if (haveWaterClause) {
        role = BrushRole::Water;
    }
    Brush brush = finalizeBrushFaces(std::move(id), std::move(faces), role);
    setBrushBlocks(brush, blockState.resolve(role));
    if (role == BrushRole::Door) {
        brush.door = haveDoorClause ? std::move(door) : BrushDoor{};
    }
    if (role == BrushRole::Water) {
        brush.water = haveWaterClause ? std::move(water) : BrushWater{};
    }
    g_builder->brushes.push_back(std::move(brush));
    return s7_t(sc);
}

void bindCsgApi(s7_scheme* sc) {
    s7_define_function(sc, "id", g_id, 1, 0, false, "(id value)");
    s7_define_function(sc, "mins", g_mins, 3, 0, false, "(mins x y z)");
    s7_define_function(sc, "maxs", g_maxs, 3, 0, false, "(maxs x y z)");
    s7_define_function(sc, "material", g_material, 1, 0, false, "(material name)");
    s7_define_function(sc, "role", g_role, 1, 0, false, "(role hull|detail|door|hint|trigger|water|window)");
    s7_define_function(sc, "uv-shift", g_uv_shift, 2, 0, false, "(uv-shift x y)");
    s7_define_function(sc, "uv-scale", g_uv_scale, 2, 0, false, "(uv-scale sx sy)");
    s7_define_function(sc, "nodraw", g_nodraw, 0, 0, false, "(nodraw)");
    s7_define_function(sc, "on-use", g_on_use, 1, 0, true, "(on-use handler arg-clause...)");
    s7_define_function(sc, "on-touch", g_on_touch, 1, 0, true, "(on-touch handler arg-clause...)");
    s7_define_function(sc, "can-use", g_can_use, 1, 0, true, "(can-use handler arg-clause...)");
    s7_define_function(sc, "motion", g_motion, 1, 0, false, "(motion raise|slide|swing)");
    s7_define_function(sc, "travel", g_travel, 1, 0, false, "(travel distance)");
    s7_define_function(sc, "angle", g_angle, 1, 0, false, "(angle radians)");
    s7_define_function(sc, "axis", g_axis, 1, 0, false, "(axis pitch|yaw|roll)");
    s7_define_function(sc, "hinge", g_hinge, 1, 0, false, "(hinge thing-id)");
    s7_define_function(sc, "group", g_group, 1, 0, false, "(group id)");
    s7_define_function(sc, "prompt", g_prompt, 1, 0, false, "(prompt string)");
    s7_define_function(sc, "duration", g_duration, 1, 0, false, "(duration seconds)");
    s7_define_function(sc, "auto-close", g_auto_close, 1, 0, false, "(auto-close seconds)");
    s7_define_function(sc, "open-sound", g_open_sound, 1, 0, false, "(open-sound path)");
    s7_define_function(sc, "close-sound", g_close_sound, 1, 0, false, "(close-sound path)");
    s7_define_function(sc, "sound-volume", g_sound_volume, 1, 0, false, "(sound-volume volume)");
    s7_define_function(sc, "door", g_door, 0, 0, true, "(door clauses...)");
    s7_define_function(sc, "tint", g_tint, 3, 0, false, "(tint r g b)");
    s7_define_function(sc, "wobble", g_wobble, 1, 0, false, "(wobble amount)");
    s7_define_function(sc, "vignette", g_vignette, 1, 0, false, "(vignette amount)");
    s7_define_function(sc, "water", g_water, 0, 0, true, "(water clauses...)");
    s7_define_function(sc, "uv-lock", g_uv_lock, 0, 0, false, "(uv-lock)");
    s7_define_function(sc, "uv-axes", g_uv_axes, 6, 0, false, "(uv-axes ux uy uz vx vy vz)");
    s7_define_function(sc, "nocollide", g_nocollide, 0, 0, false, "(nocollide)");
    s7_define_function(sc, "no-block-los", g_no_block_los, 0, 0, false, "(no-block-los)");
    s7_define_function(sc, "no-block-linescan", g_no_block_linescan, 0, 0, false, "(no-block-linescan)");
    s7_define_function(sc, "no-block-projectile", g_no_block_projectile, 0, 0, false, "(no-block-projectile)");
    s7_define_function(sc, "no-block-player", g_no_block_player, 0, 0, false, "(no-block-player)");
    s7_define_function(sc, "no-block-actor", g_no_block_actor, 0, 0, false, "(no-block-actor)");
    s7_define_function(sc, "block-los", g_block_los, 0, 0, false, "(block-los)");
    s7_define_function(sc, "block-linescan", g_block_linescan, 0, 0, false, "(block-linescan)");
    s7_define_function(sc, "block-projectile", g_block_projectile, 0, 0, false, "(block-projectile)");
    s7_define_function(sc, "block-player", g_block_player, 0, 0, false, "(block-player)");
    s7_define_function(sc, "block-actor", g_block_actor, 0, 0, false, "(block-actor)");
    s7_define_function(sc, "at", g_at, 3, 0, false, "(at x y z)");
    s7_define_function(sc, "angles", g_angles, 3, 0, false, "(angles pitch yaw roll)");
    s7_define_function(sc, "faces", g_faces, 0, 0, true, "(faces face...)");
    s7_define_function(sc, "face", g_face, 0, 0, true, "(face props...)");
    s7_define_function(sc, "verts", g_verts, 0, 0, true, "(verts (v x y z)...)");
    s7_define_function(sc, "v", g_v, 3, 0, false, "(v x y z)");
    s7_define_function(sc, "top", g_top, 0, 0, true, "(top props...)");
    s7_define_function(sc, "bottom", g_bottom, 0, 0, true, "(bottom props...)");
    s7_define_function(sc, "north", g_north, 0, 0, true, "(north props...)");
    s7_define_function(sc, "south", g_south, 0, 0, true, "(south props...)");
    s7_define_function(sc, "east", g_east, 0, 0, true, "(east props...)");
    s7_define_function(sc, "west", g_west, 0, 0, true, "(west props...)");
    s7_define_function(sc, "brush-box", g_brush_box, 0, 0, true, "(brush-box clauses...)");
    s7_define_function(sc, "brush-convex", g_brush_convex, 0, 0, true, "(brush-convex clauses...)");
    s7_define_function(sc, "prefab", g_prefab, 1, 0, true, "(prefab path clauses...)");
    bindMapHandlerArgClauses(sc);
}

MaterialUvInfo resolveMaterialUv(AssetStore& assets, std::string_view materialPath) {
    MaterialUvInfo info{};
    const MaterialAsset* asset = assets.getMaterialAsset(materialPath);
    if (asset != nullptr) {
        info.pixelsPerMeter = asset->pixelsPerMeter;
        if (!asset->albedoTexture.empty()) {
            const Texture2D texture = assets.getTexture(asset->albedoTexture);
            if (texture.id != 0 && texture.width > 0 && texture.height > 0) {
                info.textureWidth = static_cast<float>(texture.width);
                info.textureHeight = static_cast<float>(texture.height);
            }
        } else if (!asset->textureAnimPath.empty()) {
            const Texture2D texture = assets.resolveTextureAnimFrame(asset->textureAnimPath, "default", 0);
            if (texture.id != 0 && texture.width > 0 && texture.height > 0) {
                info.textureWidth = static_cast<float>(texture.width);
                info.textureHeight = static_cast<float>(texture.height);
            }
        }
    }
    return info;
}

} // namespace

void loadPackageMapHandlers(s7_scheme* scheme, AssetStore& assets) {
    if (scheme == nullptr) {
        return;
    }
    mapHandlerRegistry().clear();
    const std::string baseId{assets.basePackageId()};
    if (!baseId.empty() && assets.loadDataFromPackage(scheme, baseId, "map-handlers")) {
        registerPackageMapHandlersFromScheme(scheme);
    }
    for (const Package& package : assets.packages()) {
        if (package.role() != PackageRole::Mod) {
            continue;
        }
        if (assets.loadDataFromPackage(scheme, package.meta().id, "map-handlers")) {
            registerPackageMapHandlersFromScheme(scheme);
        }
    }
}

void loadPackageThings(s7_scheme* scheme, AssetStore& assets) {
    if (scheme == nullptr) {
        return;
    }
    thingDefRegistry().clear();
    for (const Package& package : assets.packages()) {
        if (package.role() != PackageRole::Engine) {
            continue;
        }
        if (assets.loadDataFromPackage(scheme, package.meta().id, "things")) {
            registerPackageThingsFromScheme(scheme);
        }
    }
    const std::string baseId{assets.basePackageId()};
    if (!baseId.empty() && assets.loadDataFromPackage(scheme, baseId, "things")) {
        registerPackageThingsFromScheme(scheme);
    }
    for (const Package& package : assets.packages()) {
        if (package.role() != PackageRole::Mod) {
            continue;
        }
        if (assets.loadDataFromPackage(scheme, package.meta().id, "things")) {
            registerPackageThingsFromScheme(scheme);
        }
    }
}

std::optional<MapMeta> loadMapMeta(AssetStore& assets, std::string_view mapName) {
    const std::string metaPath = std::string(mapName) + "/map";
    auto owned = assets.resolveOwned(AssetKind::MapMeta, metaPath);
    if (!owned || owned->package == nullptr) {
        return std::nullopt;
    }
    MapMeta mapMeta;
    if (!parseMapMeta(assets.getMapMetaSource(metaPath), mapMeta)) {
        return std::nullopt;
    }
    mapMeta.package = owned->package->meta().id;
    if (mapMeta.package.empty() || !assets.hasPackageId(mapMeta.package)) {
        return std::nullopt;
    }
    for (const std::string& depend : mapMeta.depends) {
        if (depend == mapMeta.package) {
            continue;
        }
        if (!assets.hasPackageId(depend)) {
            return std::nullopt;
        }
    }
    return mapMeta;
}

std::optional<MapCsgDocument> loadMapCsgDocument(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName) {
    if (scheme == nullptr) {
        return std::nullopt;
    }

    const std::string virtualPath = std::string(mapName) + "/static";
    if (!assets.hasMapCsg(virtualPath)) {
        return std::nullopt;
    }
    if (!loadMapMeta(assets, mapName)) {
        return std::nullopt;
    }

    ScriptScopeGuard scopeGuard(ScriptScope::MapAuthor);
    const s7_pointer env = s7_sublet(scheme, s7_rootlet(scheme), s7_nil(scheme));
    const s7_pointer previousEnv = s7_set_curlet(scheme, env);

    CsgBuilder builder;
    builder.assets = &assets;
    builder.recordTopLevelInstances = true;
    g_builder = &builder;
    bindCsgApi(scheme);
    const bool loaded = assets.loadMapCsg(scheme, virtualPath, env);
    g_builder = nullptr;
    s7_set_curlet(scheme, previousEnv);
    if (!loaded || (builder.brushes.empty() && builder.instances.empty())) {
        return std::nullopt;
    }

    MapCsgDocument doc;
    doc.brushes = std::move(builder.brushes);
    doc.instances = std::move(builder.instances);
    return doc;
}

std::optional<std::vector<Brush>> loadMapBrushes(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName) {
    auto doc = loadMapCsgDocument(scheme, assets, mapName);
    if (!doc) {
        return std::nullopt;
    }

    std::vector<Brush> brushes = std::move(doc->brushes);
    for (const PrefabInstance& instance : doc->instances) {
        if (!expandPrefabIntoBrushes(scheme, assets, instance, {}, brushes)) {
            TraceLog(
                LOG_WARNING,
                "MAP: failed to expand prefab '%s' id='%s'",
                instance.path.c_str(),
                instance.id.c_str());
            return std::nullopt;
        }
    }

    int hullCount = 0;
    int detailCount = 0;
    int otherCount = 0;
    int boxCount = 0;
    int nocollideCount = 0;
    for (const Brush& brush : brushes) {
        if (brush.box) {
            ++boxCount;
        }
        if (brush.nocollide) {
            ++nocollideCount;
        }
        if (brush.role == BrushRole::Detail) {
            ++detailCount;
        } else if (brush.role == BrushRole::Hull) {
            ++hullCount;
        } else {
            ++otherCount;
        }
    }
    const std::string virtualPath = std::string(mapName) + "/static";
    TraceLog(
        LOG_INFO,
        "MAP: loaded brushes '%s' total=%d hull=%d detail=%d other=%d box=%d nocollide=%d instances=%d",
        virtualPath.c_str(),
        static_cast<int>(brushes.size()),
        hullCount,
        detailCount,
        otherCount,
        boxCount,
        nocollideCount,
        static_cast<int>(doc->instances.size()));

    return brushes;
}

std::optional<std::vector<Brush>> loadPrefabBrushes(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view prefabPath) {
    if (scheme == nullptr || prefabPath.empty() || !assets.hasPrefabCsg(prefabPath)) {
        return std::nullopt;
    }

    ScriptScopeGuard scopeGuard(ScriptScope::MapAuthor);
    const s7_pointer env = s7_sublet(scheme, s7_rootlet(scheme), s7_nil(scheme));
    const s7_pointer previousEnv = s7_set_curlet(scheme, env);

    CsgBuilder builder;
    builder.assets = &assets;
    builder.recordTopLevelInstances = false;
    g_builder = &builder;
    bindCsgApi(scheme);
    const bool loaded = assets.loadPrefabCsg(scheme, prefabPath, env);
    g_builder = nullptr;
    s7_set_curlet(scheme, previousEnv);
    if (!loaded || builder.brushes.empty()) {
        return std::nullopt;
    }
    return std::move(builder.brushes);
}

std::optional<std::vector<Brush>> expandPrefabInstance(
    s7_scheme* scheme,
    AssetStore& assets,
    const PrefabInstance& instance) {
    if (scheme == nullptr) {
        return std::nullopt;
    }
    std::vector<Brush> brushes;
    if (!expandPrefabIntoBrushes(scheme, assets, instance, {}, brushes)) {
        return std::nullopt;
    }
    return brushes;
}

std::vector<Brush> expandPrefabInstances(
    s7_scheme* scheme,
    AssetStore& assets,
    const std::vector<PrefabInstance>& instances) {
    std::vector<Brush> brushes;
    if (scheme == nullptr) {
        return brushes;
    }
    for (const PrefabInstance& instance : instances) {
        expandPrefabIntoBrushes(scheme, assets, instance, {}, brushes);
    }
    return brushes;
}

std::optional<LoadedMap> loadAndCompileMap(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName) {
    if (scheme == nullptr) {
        TraceLog(LOG_WARNING, "MAP: no scheme runtime");
        return std::nullopt;
    }

    const std::string virtualPath = std::string(mapName) + "/static";
    const std::string radVirtualPath = std::string(mapName) + "/rad/static";
    const std::string metaPath = std::string(mapName) + "/map";
    if (!assets.hasMapCsg(virtualPath)) {
        TraceLog(LOG_WARNING, "MAP: missing maps/%s.csg", virtualPath.c_str());
        return std::nullopt;
    }
    if (!assets.hasMapMeta(metaPath)) {
        TraceLog(LOG_WARNING, "MAP: missing maps/%s.meta", metaPath.c_str());
        return std::nullopt;
    }
    if (!assets.hasMapBsp(virtualPath)) {
        TraceLog(LOG_WARNING, "MAP: missing maps/%s.bsp (run slopbsp)", virtualPath.c_str());
        return std::nullopt;
    }

    auto mapMeta = loadMapMeta(assets, mapName);
    if (!mapMeta) {
        TraceLog(LOG_WARNING, "MAP: invalid maps/%s.meta", metaPath.c_str());
        return std::nullopt;
    }

    TraceLog(
        LOG_INFO,
        "MAP: meta id='%s' name='%s' author='%s' package='%s' depends=%d ambient=(%.3f %.3f %.3f) sun=%s",
        mapMeta->id.c_str(),
        mapMeta->name.c_str(),
        mapMeta->author.c_str(),
        mapMeta->package.c_str(),
        static_cast<int>(mapMeta->depends.size()),
        mapMeta->ambient.x,
        mapMeta->ambient.y,
        mapMeta->ambient.z,
        mapMeta->sun.enabled ? "yes" : "no");

    auto brushes = loadMapBrushes(scheme, assets, mapName);
    if (!brushes) {
        TraceLog(LOG_WARNING, "MAP: failed to load maps/%s.csg", virtualPath.c_str());
        return std::nullopt;
    }

    std::unordered_set<std::string> moverBrushIds;
    if (auto things = loadMapThings(scheme, assets, mapName)) {
        moverBrushIds = collectClaimedBrushIds(&*things, *brushes);
    } else {
        TraceLog(LOG_WARNING, "MAP: failed to load things.s7; mover brush claims not applied");
        moverBrushIds = collectClaimedBrushIds(nullptr, *brushes);
    }

    const auto bspPath = assets.resolvePath(AssetKind::MapBsp, virtualPath);
    if (!bspPath) {
        TraceLog(LOG_WARNING, "MAP: failed to resolve bsp path");
        return std::nullopt;
    }

    auto bsp = readBspFile(*bspPath);
    if (!bsp) {
        TraceLog(LOG_WARNING, "MAP: failed to read bsp for '%s'", virtualPath.c_str());
        return std::nullopt;
    }

    const MapHullAnalysis analysis = analyzeMapHull(*bsp, *brushes);
    for (const std::string& warning : analysis.detailOutsideWarnings) {
        TraceLog(LOG_WARNING, "MAP: %s", warning.c_str());
    }

    PvsFile pvs{};
    if (assets.hasMapVis(virtualPath)) {
        if (const auto pvsPath = assets.resolvePath(AssetKind::MapVis, virtualPath)) {
            if (auto loadedPvs = readPvsFile(*pvsPath)) {
                pvs = std::move(*loadedPvs);
                TraceLog(
                    LOG_INFO,
                    "MAP: loaded pvs leaves=%d",
                    pvs.leafCount);
            } else {
                TraceLog(LOG_ERROR, "MAP: failed to read maps/%s.vis", virtualPath.c_str());
                return std::nullopt;
            }
        }
    } else {
        TraceLog(
            LOG_ERROR,
            "MAP: missing maps/%s.vis (run slopvis)",
            virtualPath.c_str());
        return std::nullopt;
    }
    if (pvs.leafCount != static_cast<int>(bsp->leaves.size())) {
        TraceLog(
            LOG_ERROR,
            "MAP: pvs leafCount=%d does not match bsp leaves=%d (re-run slopvis)",
            pvs.leafCount,
            static_cast<int>(bsp->leaves.size()));
        return std::nullopt;
    }

    RadFile rad{};
    const bool hasRad = assets.hasMapRad(radVirtualPath);
    if (hasRad) {
        const auto radPath = assets.resolvePath(AssetKind::MapRad, radVirtualPath);
        if (radPath) {
            if (auto loadedRad = readRadFile(*radPath)) {
                rad = std::move(*loadedRad);
            } else {
                TraceLog(LOG_WARNING, "MAP: failed to read rad; rendering unlit");
            }
        }
    } else {
        TraceLog(LOG_INFO, "MAP: no rad bake present; rendering unlit");
    }

    int hullCount = 0;
    int detailCount = 0;
    int boxCount = 0;
    int nocollideCount = 0;
    for (const Brush& brush : *brushes) {
        if (brush.box) {
            ++boxCount;
        }
        if (brush.nocollide) {
            ++nocollideCount;
        }
        if (brush.role == BrushRole::Detail) {
            ++detailCount;
        } else {
            ++hullCount;
        }
    }

    int emptyLeaves = 0;
    int solidLeaves = 0;
    for (const BspLeaf& leaf : bsp->leaves) {
        if (leafBlocksFlood(leaf.contents)) {
            ++solidLeaves;
        } else {
            ++emptyLeaves;
        }
    }

    TraceLog(
        LOG_INFO,
        "MAP: BSP hull=%d detail=%d box=%d nocollide=%d nodes=%d emptyLeaves=%d solidLeaves=%d portals=%d surfaceFaces=%d charts=%d",
        hullCount,
        detailCount,
        boxCount,
        nocollideCount,
        static_cast<int>(bsp->nodes.size()),
        emptyLeaves,
        solidLeaves,
        static_cast<int>(bsp->portals.size()),
        static_cast<int>(bsp->surfaceFaces.size()),
        static_cast<int>(rad.charts.size()));

    LoadedMap result;
    result.hasLightmaps = !rad.charts.empty() && !rad.atlases.empty();
    if (result.hasLightmaps) {
        result.lightmapShader = loadLightmapShader(assets, result.useLightmapLoc);
        if (result.lightmapShader.id == 0) {
            result.hasLightmaps = false;
        } else {
            applyLightmapEncoding(result.lightmapShader, primaryLightmapEncoding(rad));
        }
    }

    result.skyShader = loadSkyFaceShader(assets);
    if (result.skyShader.id == 0) {
        TraceLog(LOG_WARNING, "MAP: failed to load sky face shader for '%s'", virtualPath.c_str());
    }

    if (result.hasLightmaps) {
        result.lightmapAtlases.reserve(rad.atlases.size());
        result.lightmapAtlasImages.reserve(rad.atlases.size());
        for (const LightmapAtlasInfo& atlas : rad.atlases) {
            const std::string atlasPath = std::string(mapName) + "/rad/" + atlas.texturePath;
            const auto resolved = assets.resolvePath(AssetKind::MapLightmap, atlasPath);
            Texture2D texture{};
            Image image{};
            if (resolved) {
                texture = LoadTexture(resolved->string().c_str());
                if (texture.id != 0) {
                    const int filter = atlas.encoding == LightmapEncoding::Rgbe
                        ? TEXTURE_FILTER_POINT
                        : TEXTURE_FILTER_BILINEAR;
                    SetTextureFilter(texture, filter);
                    SetTextureWrap(texture, TEXTURE_WRAP_CLAMP);
                }
                image = LoadImage(resolved->string().c_str());
            }
            result.lightmapAtlases.push_back(texture);
            result.lightmapAtlasImages.push_back(image);
        }
    }

    const RadFile* lightmaps = result.hasLightmaps ? &rad : nullptr;
    const auto resolveUv =
        [&assets](std::string_view materialPath) { return resolveMaterialUv(assets, materialPath); };
    std::vector<Brush> staticBrushes;
    if (!moverBrushIds.empty()) {
        staticBrushes.reserve(brushes->size());
        for (const Brush& brush : *brushes) {
            if (moverBrushIds.count(brush.id) == 0) {
                staticBrushes.push_back(brush);
            }
        }
    }

    std::unordered_map<std::string, std::int32_t> faceAtlasById;
    for (const LightmapChart& chart : rad.charts) {
        faceAtlasById[chart.faceId] = chart.atlasIndex;
    }
    std::unordered_set<std::string> detailBrushIds;
    for (const Brush& brush : *brushes) {
        if (brush.role == BrushRole::Detail && !brush.id.empty()) {
            detailBrushIds.insert(brush.id);
        }
    }

    CsgCompileResult compiled = compileBrushesToGeo(
        staticBrushes.empty() && moverBrushIds.empty() ? *brushes : staticBrushes,
        resolveUv,
        lightmaps);

    // Batch same-material static geometry into fewer, larger draw calls.
    // Transparent faces stay unmerged (per-mesh back-to-front sort in the
    // transparent pass); atlas index and detail status are part of the key
    // since a merged mesh can only carry one lightmap page / offset mode.
    mergeGeoPrimitivesByKey(compiled.asset, compiled.buffer, [&](const GeoPrimitive& primitive) {
        if (primitive.transparent) {
            return "t:" + primitive.name;
        }
        std::int32_t atlasIndex = -1;
        if (const auto it = faceAtlasById.find(primitive.name); it != faceAtlasById.end()) {
            atlasIndex = it->second;
        }
        const bool detail = faceIdBelongsToAnyMoverBrush(primitive.name, detailBrushIds);
        return primitive.material + '|' + std::to_string(atlasIndex) + '|' + (detail ? "1" : "0") +
               '|' + (primitive.twoSided ? "1" : "0");
    });

    result.pvs = std::move(pvs);

    Model model = buildModelFromGeo(
        compiled.asset,
        compiled.buffer,
        [&](std::string_view path) {
            Material material = assets.resolveMaterial(path);
            if (result.hasLightmaps) {
                material.shader = result.lightmapShader;
                if (!result.lightmapAtlases.empty() && result.lightmapAtlases[0].id != 0) {
                    SetMaterialTexture(&material, MATERIAL_MAP_METALNESS, result.lightmapAtlases[0]);
                }
            }
            return material;
        });

    if (model.meshCount > 0 && result.hasLightmaps) {
        for (int meshIndex = 0; meshIndex < model.meshCount; ++meshIndex) {
            const std::string& faceId = compiled.asset.primitives[static_cast<std::size_t>(meshIndex)].name;
            std::int32_t atlasIndex = 0;
            const auto atlasIt = faceAtlasById.find(faceId);
            if (atlasIt != faceAtlasById.end()) {
                atlasIndex = atlasIt->second;
            }
            if (atlasIndex >= 0 && atlasIndex < static_cast<std::int32_t>(result.lightmapAtlases.size())) {
                const Texture2D lightmap = result.lightmapAtlases[static_cast<std::size_t>(atlasIndex)];
                if (lightmap.id != 0) {
                    SetMaterialTexture(&model.materials[meshIndex], MATERIAL_MAP_METALNESS, lightmap);
                }
            }
            model.materials[meshIndex].shader = result.lightmapShader;
        }
    }

    if (model.meshCount <= 0) {
        TraceLog(LOG_WARNING, "MAP: compile produced empty model for '%s'", virtualPath.c_str());
        return std::nullopt;
    }

    result.transparentMeshIndices.clear();
    result.transparentMeshIndices.reserve(compiled.asset.primitives.size());
    result.skyMeshIndices.clear();
    result.skyMeshIndices.reserve(compiled.asset.primitives.size());
    result.detailMeshIndices.clear();
    result.detailMeshIndices.reserve(compiled.asset.primitives.size());
    result.twoSidedMeshIndices.clear();
    result.twoSidedMeshIndices.reserve(compiled.asset.primitives.size());
    for (int meshIndex = 0; meshIndex < model.meshCount; ++meshIndex) {
        const GeoPrimitive& primitive =
            compiled.asset.primitives[static_cast<std::size_t>(meshIndex)];
        if (primitive.transparent) {
            result.transparentMeshIndices.push_back(meshIndex);
        }
        if (faceIdBelongsToAnyMoverBrush(primitive.name, detailBrushIds)) {
            result.detailMeshIndices.push_back(meshIndex);
        }
        if (primitive.twoSided) {
            result.twoSidedMeshIndices.push_back(meshIndex);
        }
        const MaterialAsset* materialAsset = assets.getMaterialAsset(primitive.material);
        if (materialAsset != nullptr && materialAsset->sky) {
            result.skyMeshIndices.push_back(meshIndex);
            if (result.skyShader.id != 0) {
                model.materials[meshIndex].shader = result.skyShader;
            }
        }
    }

    TraceLog(
        LOG_INFO,
        "MAP: loaded '%s' (%d brushes, %d meshes, lightmaps=%s)",
        virtualPath.c_str(),
        static_cast<int>(brushes->size()),
        model.meshCount,
        result.hasLightmaps ? "yes" : "no");

    collectMaterialAnimTargets(compiled.asset, assets, result.materialAnimTargets);

    result.model = model;
    result.brushes = std::move(*brushes);
    result.moverBrushIds = std::move(moverBrushIds);
    result.bsp = std::move(*bsp);
    result.rad = std::move(rad);
    result.meta = std::move(*mapMeta);
    return result;
}

}
