#include "map/things_script.hpp"

#include "map/map_handler_registry.hpp"
#include "map/thing_def_registry.hpp"
#include "script/script_scope.hpp"
#include "script/s7_read.hpp"

#include <raylib.h>
#include <s7.h>

#include <cstring>
#include <string>
#include <unordered_set>
#include <algorithm>

namespace slopengine {

namespace {

struct ThingLoadContext {
    ThingDocument* doc = nullptr;
    AssetStore* assets = nullptr;
    std::unordered_set<std::string> usedIds;
    bool sawPlayerStart = false;
};

ThingLoadContext* g_context = nullptr;

bool readVec2(s7_scheme* sc, s7_pointer list, Vector2& out) {
    s7_pointer x = s7_car(list);
    s7_pointer y = s7_cadr(list);
    if (!s7_is_number(x) || !s7_is_number(y)) {
        return false;
    }
    out.x = static_cast<float>(s7_number_to_real(sc, x));
    out.y = static_cast<float>(s7_number_to_real(sc, y));
    return true;
}

bool hasArgs(s7_pointer rest, int count) {
    for (int i = 0; i < count; ++i) {
        if (!s7_is_pair(rest)) {
            return false;
        }
        rest = s7_cdr(rest);
    }
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

constexpr ClauseSpec kThingClauses[] = {
    {"id", 1, 0, false, "(id value)"},
    {"type", 1, 0, false, "(type catalog-id)"},
    {"at", 3, 0, false, "(at x y z)"},
    {"yaw", 1, 0, false, "(yaw radians)"},
    {"pitch", 1, 0, false, "(pitch radians)"},
    {"angles", 3, 0, false, "(angles pitch yaw roll)"},
    {"sprite", 1, 0, false, "(sprite path)"},
    {"geo", 1, 0, false, "(geo path)"},
    {"brush", 1, 0, false, "(brush brush-id)"},
    {"frame", 1, 0, false, "(frame id)"},
    {"anim", 1, 1, false, "(anim clip [loop])"},
    {"anims", 1, 0, false, "(anims path)"},
    {"prompt", 1, 0, false, "(prompt text)"},
    {"on-use", 1, 0, true, "(on-use handler arg-clause...)"},
    {"pivot", 3, 0, false, "(pivot x y z)"},
    {"open-offset", 3, 0, false, "(open-offset dx dy dz)"},
    {"open-pitch", 1, 0, false, "(open-pitch radians)"},
    {"open-yaw", 1, 0, false, "(open-yaw radians)"},
    {"open-roll", 1, 0, false, "(open-roll radians)"},
    {"duration", 1, 0, false, "(duration seconds)"},
    {"auto-close", 1, 0, false, "(auto-close seconds)"},
    {"collide-size", 3, 0, false, "(collide-size w h d)"},
    {"collide-center", 3, 0, false, "(collide-center x y z)"},
    {"block-mode", 1, 0, false, "(block-mode shove|crush)"},
    {"push", 1, 0, false, "(push full|horizontal|off)"},
    {"carry", 1, 0, false, "(carry bool)"},
    {"on-crush", 1, 0, false, "(on-crush handler)"},
    {"group", 1, 0, false, "(group id)"},
    {"on-enter", 1, 0, true, "(on-enter handler arg-clause...)"},
    {"on-exit", 1, 0, true, "(on-exit handler arg-clause...)"},
    {"trigger-size", 3, 0, false, "(trigger-size w h d)"},
    {"once", 1, 0, false, "(once bool)"},
    {"collide-tags", 0, 0, true, "(collide-tags values...)"},
    {"tags", 0, 0, true, "(tags values...)"},
    {"motor", 0, 0, true, "(motor clauses...)"},
    {"radius", 1, 0, false, "(radius value)"},
    {"height", 1, 0, false, "(height value)"},
    {"speed", 1, 0, false, "(speed value)"},
    {"gravity", 1, 0, false, "(gravity value)"},
    {"step-height", 1, 0, false, "(step-height value)"},
    {"vertical-speed", 1, 0, false, "(vertical-speed value)"},
    {"hover-height", 1, 0, false, "(hover-height value)"},
    {"eye-height", 1, 0, false, "(eye-height value)"},
    {"hull", 1, 0, false, "(hull capsule|box|sphere)"},
    {"move", 1, 0, false, "(move slide|try-move)"},
    {"sight", 0, 0, true, "(sight clauses...)"},
    {"fov", 1, 0, false, "(fov degrees)"},
    {"eye-lift", 1, 0, false, "(eye-lift fraction)"},
    {"see-tags", 0, 0, true, "(see-tags values...)"},
    {"ignore-tags", 0, 0, true, "(ignore-tags values...)"},
    {"filter", 1, 0, false, "(filter proc-name)"},
    {"enabled", 1, 0, false, "(enabled bool)"},
    {"color", 3, 0, false, "(color r g b)"},
    {"intensity", 1, 0, false, "(intensity value)"},
    {"range", 1, 0, false, "(range value)"},
    {"cone", 1, 0, false, "(cone radians)"},
    {"size", 2, 0, false, "(size width height)"},
    {"audio", 1, 0, false, "(audio path)"},
    {"system", 1, 0, false, "(system path)"},
    {"play", 1, 0, false, "(play bool)"},
    {"volume", 1, 0, false, "(volume value)"},
    {"loop", 1, 0, false, "(loop bool)"},
    {"spatial", 1, 0, false, "(spatial bool)"},
    {"cast-shadows", 1, 0, false, "(cast-shadows bool)"},
    {"min-distance", 1, 0, false, "(min-distance value)"},
    {"max-distance", 1, 0, false, "(max-distance value)"},
    {"material", 1, 0, false, "(material path)"},
    {"gradient", 0, 0, true, "(gradient (stop ...)...)"},
    {"cube", 0, 0, true, "(cube (px ...)... (nz ...))"},
    {"cylinder", 1, 0, false, "(cylinder path)"},
    {"cylinder-offset", 1, 0, false, "(cylinder-offset value)"},
    {"cylinder-scale", 1, 0, false, "(cylinder-scale value)"},
    {"cylinder-repeat", 1, 0, false, "(cylinder-repeat value)"},
    {"stop", 4, 0, false, "(stop position r g b)"},
    {"px", 1, 0, false, "(px path)"},
    {"nx", 1, 0, false, "(nx path)"},
    {"py", 1, 0, false, "(py path)"},
    {"ny", 1, 0, false, "(ny path)"},
    {"pz", 1, 0, false, "(pz path)"},
    {"nz", 1, 0, false, "(nz path)"},
};

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
            readName(s7_car(rest), out.id);
        } else if (std::strcmp(tag, "type") == 0 && s7_is_pair(rest)) {
            readName(s7_car(rest), out.type);
        } else if (std::strcmp(tag, "at") == 0 && hasArgs(rest, 3)) {
            out.haveAt = readVec3List(sc, rest, out.at);
        } else if (std::strcmp(tag, "yaw") == 0 && s7_is_pair(rest) &&
                   readReal(sc, s7_car(rest), out.yaw)) {
            if (!out.haveAngles) {
                out.angles.y = out.yaw;
            }
        } else if (std::strcmp(tag, "pitch") == 0 && s7_is_pair(rest) &&
                   readReal(sc, s7_car(rest), out.pitch)) {
            out.havePitch = true;
            if (!out.haveAngles) {
                out.angles.x = out.pitch;
            }
        } else if (std::strcmp(tag, "angles") == 0 && hasArgs(rest, 3)) {
            out.haveAngles = readVec3List(sc, rest, out.angles);
            if (out.haveAngles) {
                out.yaw = out.angles.y;
                if (!out.havePitch) {
                    out.pitch = out.angles.x;
                    out.havePitch = true;
                }
            }
        } else if (std::strcmp(tag, "sprite") == 0 && s7_is_pair(rest)) {
            readName(s7_car(rest), out.sprite);
        } else if (std::strcmp(tag, "geo") == 0 && s7_is_pair(rest)) {
            readName(s7_car(rest), out.geo);
        } else if (std::strcmp(tag, "brush") == 0 && s7_is_pair(rest)) {
            readName(s7_car(rest), out.brush);
        } else if (std::strcmp(tag, "frame") == 0 && s7_is_pair(rest)) {
            readName(s7_car(rest), out.frame);
        } else if (std::strcmp(tag, "anim") == 0 && s7_is_pair(rest)) {
            ArgReader in(sc, "anim", rest);
            if (in.nextIsName()) {
                out.animClip = in.name();
                out.haveAnim = true;
                out.animLoop = in.optFlag(true);
            }
        } else if (std::strcmp(tag, "anims") == 0 && s7_is_pair(rest)) {
            readName(s7_car(rest), out.animBank);
        } else if (std::strcmp(tag, "prompt") == 0 && s7_is_pair(rest)) {
            out.havePrompt = readName(s7_car(rest), out.prompt);
        } else if (std::strcmp(tag, "on-use") == 0 && s7_is_pair(rest)) {
            if (parseHandlerBinding(sc, rest, out.onUse)) {
                mapHandlerRegistry().refineBinding(out.onUse, MapHandlerKind::Use);
            }
        } else if (std::strcmp(tag, "pivot") == 0 && hasArgs(rest, 3)) {
            out.haveMoverPivot = readVec3List(sc, rest, out.moverPivot);
        } else if (std::strcmp(tag, "open-offset") == 0 && hasArgs(rest, 3)) {
            out.haveMoverOpenOffset = readVec3List(sc, rest, out.moverOpenOffset);
        } else if (std::strcmp(tag, "open-pitch") == 0 && s7_is_pair(rest) &&
                   readReal(sc, s7_car(rest), out.moverOpenAngle)) {
            out.haveMoverOpenAngle = true;
            out.moverRotAxis = 0;
        } else if (std::strcmp(tag, "open-yaw") == 0 && s7_is_pair(rest) &&
                   readReal(sc, s7_car(rest), out.moverOpenAngle)) {
            out.haveMoverOpenAngle = true;
            out.moverRotAxis = 1;
        } else if (std::strcmp(tag, "open-roll") == 0 && s7_is_pair(rest) &&
                   readReal(sc, s7_car(rest), out.moverOpenAngle)) {
            out.haveMoverOpenAngle = true;
            out.moverRotAxis = 2;
        } else if (std::strcmp(tag, "duration") == 0 && s7_is_pair(rest) &&
                   readReal(sc, s7_car(rest), out.moverDuration)) {
            out.haveMoverDuration = true;
        } else if (std::strcmp(tag, "auto-close") == 0 && s7_is_pair(rest) &&
                   readReal(sc, s7_car(rest), out.moverAutoClose)) {
            out.haveMoverAutoClose = true;
        } else if (std::strcmp(tag, "collide-size") == 0 && hasArgs(rest, 3)) {
            out.haveMoverCollideSize = readVec3List(sc, rest, out.moverCollideSize);
        } else if (std::strcmp(tag, "collide-center") == 0 && hasArgs(rest, 3)) {
            out.haveMoverCollideCenter = readVec3List(sc, rest, out.moverCollideCenter);
        } else if (std::strcmp(tag, "block-mode") == 0 && s7_is_pair(rest)) {
            readName(s7_car(rest), out.moverBlockMode);
        } else if (std::strcmp(tag, "push") == 0 && s7_is_pair(rest)) {
            readName(s7_car(rest), out.moverPush);
        } else if (std::strcmp(tag, "carry") == 0 && s7_is_pair(rest)) {
            out.haveMoverSlide = readFlag(sc, s7_car(rest), out.moverSlide);
        } else if (std::strcmp(tag, "on-crush") == 0 && s7_is_pair(rest)) {
            readName(s7_car(rest), out.onCrush);
        } else if (std::strcmp(tag, "group") == 0 && s7_is_pair(rest)) {
            readName(s7_car(rest), out.moverGroup);
        } else if (std::strcmp(tag, "open-sound") == 0 && s7_is_pair(rest)) {
            readName(s7_car(rest), out.moverOpenSound);
        } else if (std::strcmp(tag, "close-sound") == 0 && s7_is_pair(rest)) {
            readName(s7_car(rest), out.moverCloseSound);
        } else if (std::strcmp(tag, "sound-volume") == 0 && s7_is_pair(rest) &&
                   readReal(sc, s7_car(rest), out.moverSoundVolume)) {
            out.haveMoverSoundVolume = true;
        } else if (std::strcmp(tag, "on-enter") == 0 && s7_is_pair(rest)) {
            if (parseHandlerBinding(sc, rest, out.onEnter)) {
                mapHandlerRegistry().refineBinding(out.onEnter, MapHandlerKind::Enter);
            }
        } else if (std::strcmp(tag, "on-exit") == 0 && s7_is_pair(rest)) {
            if (parseHandlerBinding(sc, rest, out.onExit)) {
                mapHandlerRegistry().refineBinding(out.onExit, MapHandlerKind::Exit);
            }
        } else if (std::strcmp(tag, "trigger-size") == 0 && hasArgs(rest, 3)) {
            out.haveTriggerSize = readVec3List(sc, rest, out.triggerSize);
        } else if (std::strcmp(tag, "once") == 0 && s7_is_pair(rest)) {
            readFlag(sc, s7_car(rest), out.triggerOnce);
        } else if (std::strcmp(tag, "collide-tags") == 0) {
            out.collideTags.clear();
            for (s7_pointer tagCursor = rest; s7_is_pair(tagCursor); tagCursor = s7_cdr(tagCursor)) {
                std::string tagValue;
                if (readName(s7_car(tagCursor), tagValue) && !tagValue.empty()) {
                    out.collideTags.push_back(std::move(tagValue));
                }
            }
        } else if (std::strcmp(tag, "tags") == 0) {
            out.tags.clear();
            for (s7_pointer tagCursor = rest; s7_is_pair(tagCursor); tagCursor = s7_cdr(tagCursor)) {
                std::string tagValue;
                if (readName(s7_car(tagCursor), tagValue) && !tagValue.empty()) {
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
                    if (!readName(s7_car(motorRest), hull) ||
                        (hull != "box" && hull != "capsule" && hull != "sphere")) {
                        TraceLog(LOG_WARNING, "THING: motor hull must be box, capsule, or sphere");
                        return false;
                    }
                    if (hull == "box") {
                        out.motorHull = CharacterHull::Box;
                    } else if (hull == "sphere") {
                        out.motorHull = CharacterHull::Sphere;
                    } else {
                        out.motorHull = CharacterHull::Capsule;
                    }
                    continue;
                }
                if (std::strcmp(motorTag, "move") == 0) {
                    std::string move;
                    if (!readName(s7_car(motorRest), move) ||
                        (move != "try-move" && move != "slide" && move != "fly")) {
                        TraceLog(LOG_WARNING, "THING: motor move must be slide, try-move, or fly");
                        return false;
                    }
                    if (move == "try-move") {
                        out.motorMoveMode = CharacterMoveMode::TryMove;
                    } else if (move == "fly") {
                        out.motorMoveMode = CharacterMoveMode::Fly;
                    } else {
                        out.motorMoveMode = CharacterMoveMode::Slide;
                    }
                    continue;
                }
                if (std::strcmp(motorTag, "nav-profile") == 0) {
                    std::string profile;
                    if (!readName(s7_car(motorRest), profile) || profile.empty()) {
                        TraceLog(LOG_WARNING, "THING: motor nav-profile must be a non-empty string");
                        return false;
                    }
                    out.motorNavProfile = std::move(profile);
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
                } else if (std::strcmp(motorTag, "vertical-speed") == 0) {
                    out.motorVerticalSpeed = value;
                } else if (std::strcmp(motorTag, "hover-height") == 0) {
                    out.motorHoverHeight = value;
                } else if (std::strcmp(motorTag, "eye-height") == 0) {
                    out.motorEyeHeight = value;
                } else if (std::strcmp(motorTag, "max-fall") == 0) {
                    out.motorMaxFall = value;
                } else if (std::strcmp(motorTag, "water-aversion") == 0) {
                    out.motorWaterAversion = value;
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
                        if (readName(s7_car(tagCursor), tagValue) && !tagValue.empty()) {
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
                        if (readName(s7_car(tagCursor), tagValue) && !tagValue.empty()) {
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
                    if (!readName(s7_car(sightRest), out.sightFilterProc)) {
                        TraceLog(LOG_WARNING, "THING: sight filter must be a string");
                        return false;
                    }
                    continue;
                }
                if (std::strcmp(sightTag, "enabled") == 0) {
                    if (!readFlag(sc, s7_car(sightRest), out.sightEnabled)) {
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
        } else if (std::strcmp(tag, "color") == 0 && hasArgs(rest, 3)) {
            readVec3List(sc, rest, out.color);
        } else if (std::strcmp(tag, "intensity") == 0 && s7_is_pair(rest) &&
                   readReal(sc, s7_car(rest), out.intensity)) {
        } else if (std::strcmp(tag, "range") == 0 && s7_is_pair(rest) &&
                   readReal(sc, s7_car(rest), out.range)) {
        } else if (std::strcmp(tag, "cone") == 0 && s7_is_pair(rest) &&
                   readReal(sc, s7_car(rest), out.coneAngle)) {
        } else if (std::strcmp(tag, "size") == 0 && hasArgs(rest, 2)) {
            readVec2(sc, rest, out.size);
        } else if (std::strcmp(tag, "audio") == 0 && s7_is_pair(rest)) {
            readName(s7_car(rest), out.audio);
        } else if (std::strcmp(tag, "volume") == 0 && s7_is_pair(rest) &&
                   s7_is_number(s7_car(rest))) {
        } else if (std::strcmp(tag, "loop") == 0 && s7_is_pair(rest)) {
        } else if (std::strcmp(tag, "spatial") == 0 && s7_is_pair(rest)) {
        } else if (std::strcmp(tag, "cast-shadows") == 0 && s7_is_pair(rest)) {
            readFlag(sc, s7_car(rest), out.dynamicCastShadows);
        } else if (std::strcmp(tag, "min-distance") == 0 && s7_is_pair(rest) &&
                   s7_is_number(s7_car(rest))) {
        } else if (std::strcmp(tag, "max-distance") == 0 && s7_is_pair(rest) &&
                   s7_is_number(s7_car(rest))) {
        } else if (std::strcmp(tag, "system") == 0 && s7_is_pair(rest)) {
            readName(s7_car(rest), out.particleSystem);
        } else if (std::strcmp(tag, "play") == 0 && s7_is_pair(rest)) {
            out.haveParticlePlay = readFlag(sc, s7_car(rest), out.particlePlay);
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

s7_pointer g_dynamic_point_light(s7_scheme* sc, s7_pointer args) {
    Thing placement = makeDefaultLightThing(ThingKind::DynamicPointLight);
    if (!parseThingClauses(sc, args, placement)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "dynamic-point-light has invalid clauses")));
    }
    return appendThing(sc, std::move(placement), "dynamic-point-light requires id and at", true);
}

s7_pointer g_dynamic_spot_light(s7_scheme* sc, s7_pointer args) {
    Thing placement = makeDefaultLightThing(ThingKind::DynamicSpotLight);
    if (!parseThingClauses(sc, args, placement)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "dynamic-spot-light has invalid clauses")));
    }
    return appendThing(sc, std::move(placement), "dynamic-spot-light requires id and at", true);
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

bool parseSkyboxClauses(s7_scheme* sc, s7_pointer args, Thing& out) {
    int modeCount = 0;
    for (s7_pointer cursor = args; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer clause = s7_car(cursor);
        if (!s7_is_pair(clause) || !s7_is_symbol(s7_car(clause))) {
            TraceLog(LOG_WARNING, "THING: skybox expected clause list");
            return false;
        }
        const char* tag = s7_symbol_name(s7_car(clause));
        s7_pointer rest = s7_cdr(clause);
        if (std::strcmp(tag, "id") == 0 && s7_is_pair(rest)) {
            if (!readName(s7_car(rest), out.id)) {
                TraceLog(LOG_WARNING, "THING: skybox id must be a string");
                return false;
            }
        } else if (std::strcmp(tag, "material") == 0 && s7_is_pair(rest)) {
            if (!readName(s7_car(rest), out.skyMaterial)) {
                TraceLog(LOG_WARNING, "THING: skybox material must be a string");
                return false;
            }
        } else if (std::strcmp(tag, "color") == 0 && hasArgs(rest, 3)) {
            readVec3List(sc, rest, out.color);
            out.skyboxMode = SkyboxMode::Solid;
            out.haveSkyboxMode = true;
            ++modeCount;
        } else if (std::strcmp(tag, "cube") == 0) {
            out.skyboxMode = SkyboxMode::Cube;
            out.haveSkyboxMode = true;
            ++modeCount;
            for (s7_pointer faceCursor = rest; s7_is_pair(faceCursor); faceCursor = s7_cdr(faceCursor)) {
                s7_pointer faceClause = s7_car(faceCursor);
                if (!s7_is_pair(faceClause) || !s7_is_symbol(s7_car(faceClause))) {
                    TraceLog(LOG_WARNING, "THING: skybox cube expected face clause");
                    return false;
                }
                const char* faceTag = s7_symbol_name(s7_car(faceClause));
                s7_pointer faceRest = s7_cdr(faceClause);
                if (!s7_is_pair(faceRest)) {
                    TraceLog(LOG_WARNING, "THING: skybox cube face missing path");
                    return false;
                }
                std::string path;
                if (!readName(s7_car(faceRest), path)) {
                    TraceLog(LOG_WARNING, "THING: skybox cube face path must be a string");
                    return false;
                }
                if (std::strcmp(faceTag, "px") == 0) {
                    out.skyCubePx = std::move(path);
                } else if (std::strcmp(faceTag, "nx") == 0) {
                    out.skyCubeNx = std::move(path);
                } else if (std::strcmp(faceTag, "py") == 0) {
                    out.skyCubePy = std::move(path);
                } else if (std::strcmp(faceTag, "ny") == 0) {
                    out.skyCubeNy = std::move(path);
                } else if (std::strcmp(faceTag, "pz") == 0) {
                    out.skyCubePz = std::move(path);
                } else if (std::strcmp(faceTag, "nz") == 0) {
                    out.skyCubeNz = std::move(path);
                } else {
                    TraceLog(LOG_WARNING, "THING: unknown skybox cube face '%s'", faceTag);
                    return false;
                }
            }
        } else if (std::strcmp(tag, "cylinder") == 0 && s7_is_pair(rest)) {
            if (!readName(s7_car(rest), out.skyCylinderTexture)) {
                TraceLog(LOG_WARNING, "THING: skybox cylinder texture must be a string");
                return false;
            }
            out.skyboxMode = SkyboxMode::Cylinder;
            out.haveSkyboxMode = true;
            ++modeCount;
        } else if (std::strcmp(tag, "cylinder-offset") == 0 && s7_is_pair(rest) &&
                   readReal(sc, s7_car(rest), out.skyCylinderOffset)) {
        } else if (std::strcmp(tag, "cylinder-scale") == 0 && s7_is_pair(rest) &&
                   readReal(sc, s7_car(rest), out.skyCylinderScale)) {
        } else if (std::strcmp(tag, "cylinder-repeat") == 0 && s7_is_pair(rest) &&
                   s7_is_number(s7_car(rest))) {
            out.skyCylinderRepeat = std::max(1, static_cast<int>(s7_number_to_real(sc, s7_car(rest))));
        } else if (std::strcmp(tag, "gradient") == 0) {
            out.skyboxMode = SkyboxMode::Gradient;
            out.haveSkyboxMode = true;
            ++modeCount;
            out.skyGradientStopCount = 0;
            for (s7_pointer stopCursor = rest; s7_is_pair(stopCursor); stopCursor = s7_cdr(stopCursor)) {
                s7_pointer stopClause = s7_car(stopCursor);
                if (!s7_is_pair(stopClause) || !s7_is_symbol(s7_car(stopClause)) ||
                    std::strcmp(s7_symbol_name(s7_car(stopClause)), "stop") != 0) {
                    TraceLog(LOG_WARNING, "THING: skybox gradient expected (stop position r g b)");
                    return false;
                }
                s7_pointer stopRest = s7_cdr(stopClause);
                if (!hasArgs(stopRest, 4)) {
                    TraceLog(LOG_WARNING, "THING: skybox stop requires position r g b");
                    return false;
                }
                if (out.skyGradientStopCount >= 4) {
                    TraceLog(LOG_WARNING, "THING: skybox gradient accepts exactly 4 stops");
                    return false;
                }
                SkyGradientStop stop{};
                readReal(sc, s7_car(stopRest), stop.position);
                readVec3List(sc, s7_cdr(stopRest), stop.color);
                out.skyGradientStops[static_cast<std::size_t>(out.skyGradientStopCount)] = stop;
                ++out.skyGradientStopCount;
            }
            if (out.skyGradientStopCount != 4) {
                TraceLog(LOG_WARNING, "THING: skybox gradient requires exactly 4 stops");
                return false;
            }
            std::sort(
                out.skyGradientStops.begin(),
                out.skyGradientStops.begin() + out.skyGradientStopCount,
                [](const SkyGradientStop& a, const SkyGradientStop& b) {
                    return a.position < b.position;
                });
        } else {
            TraceLog(LOG_WARNING, "THING: unknown or malformed skybox clause '%s'", tag);
            return false;
        }
    }
    if (!out.haveSkyboxMode && out.skyMaterial.empty()) {
        TraceLog(LOG_WARNING, "THING: skybox requires material, color, cube, gradient, or cylinder");
        return false;
    }
    if (out.haveSkyboxMode && modeCount != 1) {
        TraceLog(LOG_WARNING, "THING: skybox requires exactly one of color, cube, gradient, or cylinder");
        return false;
    }
    if (out.skyboxMode == SkyboxMode::Cube) {
        if (out.skyCubePx.empty() || out.skyCubeNx.empty() || out.skyCubePy.empty() ||
            out.skyCubeNy.empty() || out.skyCubePz.empty() || out.skyCubeNz.empty()) {
            TraceLog(LOG_WARNING, "THING: skybox cube requires px nx py ny pz nz faces");
            return false;
        }
    }
    if (out.skyboxMode == SkyboxMode::Cylinder && out.skyCylinderTexture.empty()) {
        TraceLog(LOG_WARNING, "THING: skybox cylinder requires a texture path");
        return false;
    }
    return true;
}

s7_pointer g_skybox(s7_scheme* sc, s7_pointer args) {
    Thing placement = makeDefaultSkyboxThing();
    if (!parseSkyboxClauses(sc, args, placement)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "skybox has invalid clauses")));
    }
    return appendThing(sc, std::move(placement), "skybox requires id", false);
}

s7_pointer g_sound_source(s7_scheme* sc, s7_pointer args) {
    Thing placement = makeDefaultSoundSourceThing();
    if (!parseThingClauses(sc, args, placement)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "sound-source has invalid clauses")));
    }
    if (placement.audio.empty()) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "sound-source requires audio")));
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

s7_pointer g_particle(s7_scheme* sc, s7_pointer args) {
    Thing placement = makeDefaultParticleThing();
    if (!parseThingClauses(sc, args, placement)) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "particle has invalid clauses")));
    }
    if (placement.particleSystem.empty()) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "thing-error"),
            s7_list(sc, 1, s7_make_string(sc, "particle requires system")));
    }
    return appendThing(sc, std::move(placement), "particle requires id and at", true);
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
    if (!readName(s7_car(args), placement.prefabPath) || placement.prefabPath.empty()) {
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

constexpr SchemeBinding kThingBindings[] = {
    {"player-start", g_player_start, 0, 0, true, "(player-start clauses...)"},
    {"prop", g_prop, 0, 0, true, "(prop clauses...)"},
    {"usable", g_usable, 0, 0, true, "(usable clauses...)"},
    {"pickup", g_pickup, 0, 0, true, "(pickup clauses...)"},
    {"actor", g_actor, 0, 0, true, "(actor clauses...)"},
    {"thing", g_thing, 0, 0, true, "(thing clauses...)"},
    {"mover", g_mover, 0, 0, true, "(mover clauses...)"},
    {"trigger", g_trigger, 0, 0, true, "(trigger clauses...)"},
    {"point-light", g_point_light, 0, 0, true, "(point-light clauses...)"},
    {"spot-light", g_spot_light, 0, 0, true, "(spot-light clauses...)"},
    {"area-light", g_area_light, 0, 0, true, "(area-light clauses...)"},
    {"sun", g_sun, 0, 0, true, "(sun clauses...)"},
    {"ambient-light", g_ambient_light, 0, 0, true, "(ambient-light clauses...)"},
    {"dynamic-point-light", g_dynamic_point_light, 0, 0, true, "(dynamic-point-light clauses...)"},
    {"dynamic-spot-light", g_dynamic_spot_light, 0, 0, true, "(dynamic-spot-light clauses...)"},
    {"skybox", g_skybox, 0, 0, true, "(skybox clauses...)"},
    {"sound-source", g_sound_source, 0, 0, true, "(sound-source clauses...)"},
    {"marker", g_marker, 0, 0, true, "(marker clauses...)"},
    {"particle", g_particle, 0, 0, true, "(particle clauses...)"},
    {"prefab", g_prefab, 1, 0, true, "(prefab path clauses...)"},
};

void bindThingApi(s7_scheme* sc) {
    registerClauseBuilders(sc, kThingClauses);
    registerBindings(sc, kThingBindings);
    s7_define_variable(sc, "capsule", s7_make_symbol(sc, "capsule"));
    s7_define_variable(sc, "box", s7_make_symbol(sc, "box"));
    s7_define_variable(sc, "slide", s7_make_symbol(sc, "slide"));
    s7_define_variable(sc, "try-move", s7_make_symbol(sc, "try-move"));
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
