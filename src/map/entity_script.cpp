#include "map/entity_script.hpp"

#include "assets/skeleton_loader.hpp"
#include "interact/components.hpp"
#include "render/components.hpp"
#include "render/sprite_animator.hpp"

#include <raylib.h>
#include <raymath.h>
#include <s7.h>

#include <cstring>
#include <string>
#include <unordered_set>
#include <utility>

namespace slopengine {

namespace {

struct EntityLoadContext {
    flecs::world* world = nullptr;
    AssetStore* assets = nullptr;
    PlayerStart playerStart{};
    std::unordered_set<std::string> usedIds;
};

EntityLoadContext* g_context = nullptr;

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

bool claimId(const std::string& id) {
    if (g_context == nullptr || id.empty()) {
        return false;
    }
    if (!g_context->usedIds.insert(id).second) {
        TraceLog(LOG_WARNING, "ENTITY: duplicate id '%s'", id.c_str());
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

struct SpawnCommon {
    std::string id;
    Vector3 position{0.0f, 0.0f, 0.0f};
    float yaw = 0.0f;
    bool haveAt = false;
    std::string sprite;
    std::string geo;
    std::string frame = "A";
    std::string animClip;
    bool animLoop = true;
    bool haveAnim = false;
    std::string prompt = "Interact";
    std::string onUse;
};

bool parseSpawnClauses(s7_scheme* sc, s7_pointer args, SpawnCommon& out) {
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
            out.haveAt = readVec3(sc, s7_car(rest), s7_cadr(rest), s7_caddr(rest), out.position);
        } else if (std::strcmp(tag, "yaw") == 0 && s7_is_pair(rest) && s7_is_number(s7_car(rest))) {
            out.yaw = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
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
        }
    }
    return true;
}

bool applyPresentation(flecs::entity entity, const SpawnCommon& common) {
    const bool hasSprite = !common.sprite.empty();
    const bool hasGeo = !common.geo.empty();
    if (hasSprite == hasGeo) {
        TraceLog(
            LOG_WARNING,
            "ENTITY: '%s' requires exactly one of sprite or geo",
            common.id.c_str());
        return false;
    }

    LocalTransformation local{};
    local.position = common.position;
    local.scale = {1.0f, 1.0f, 1.0f};
    local.rotation = QuaternionFromAxisAngle({0.0f, 1.0f, 0.0f}, common.yaw);

    entity.add<WorldSpace>().set<LocalTransformation>(local);

    if (hasSprite) {
        if (g_context->assets == nullptr || !g_context->assets->hasSprite(common.sprite)) {
            TraceLog(LOG_WARNING, "ENTITY: missing sprite '%s' for '%s'", common.sprite.c_str(), common.id.c_str());
            return false;
        }

        entity.set<SpriteInstance>({
            .sprite = common.sprite,
            .frame = common.frame.empty() ? "A" : common.frame,
            .facingYaw = common.yaw,
        });

        if (common.haveAnim) {
            SpriteAnimator animator{};
            animator.animPath = common.sprite;
            animator.play(common.animClip, common.animLoop);
            entity.set<SpriteAnimator>(animator);
        }
        return true;
    }

    if (g_context->assets == nullptr || !g_context->assets->hasGeo(common.geo)) {
        TraceLog(LOG_WARNING, "ENTITY: missing geo '%s' for '%s'", common.geo.c_str(), common.id.c_str());
        return false;
    }

    const Model source = g_context->assets->getGeoModel(common.geo);
    Model model = cloneGeoModelInstance(source);
    if (model.meshCount <= 0) {
        TraceLog(LOG_WARNING, "ENTITY: failed to load geo '%s' for '%s'", common.geo.c_str(), common.id.c_str());
        return false;
    }

    entity.set<Model3D>({model, WHITE});
    return true;
}

s7_pointer g_player_start(s7_scheme* sc, s7_pointer args) {
    if (g_context == nullptr) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "entity-error"),
            s7_list(sc, 1, s7_make_string(sc, "player-start called outside entity load")));
    }

    SpawnCommon common{};
    parseSpawnClauses(sc, args, common);

    if (common.id.empty() || !common.haveAt) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "entity-error"),
            s7_list(sc, 1, s7_make_string(sc, "player-start requires id and at")));
    }

    claimId(common.id);

    if (g_context->playerStart.found) {
        TraceLog(LOG_WARNING, "ENTITY: ignoring extra player-start '%s'", common.id.c_str());
        return s7_t(sc);
    }

    g_context->playerStart.position = common.position;
    g_context->playerStart.yaw = common.yaw;
    g_context->playerStart.found = true;
    return s7_t(sc);
}

s7_pointer g_prop(s7_scheme* sc, s7_pointer args) {
    if (g_context == nullptr || g_context->world == nullptr) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "entity-error"),
            s7_list(sc, 1, s7_make_string(sc, "prop called outside entity load")));
    }

    SpawnCommon common{};
    parseSpawnClauses(sc, args, common);

    if (common.id.empty() || !common.haveAt) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "entity-error"),
            s7_list(sc, 1, s7_make_string(sc, "prop requires id and at")));
    }

    if (!claimId(common.id)) {
        return s7_f(sc);
    }

    flecs::entity entity = g_context->world->entity(common.id.c_str());
    if (!applyPresentation(entity, common)) {
        entity.destruct();
        return s7_f(sc);
    }
    return s7_t(sc);
}

s7_pointer g_usable(s7_scheme* sc, s7_pointer args) {
    if (g_context == nullptr || g_context->world == nullptr) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "entity-error"),
            s7_list(sc, 1, s7_make_string(sc, "usable called outside entity load")));
    }

    SpawnCommon common{};
    parseSpawnClauses(sc, args, common);

    if (common.id.empty() || !common.haveAt) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "entity-error"),
            s7_list(sc, 1, s7_make_string(sc, "usable requires id and at")));
    }

    if (!claimId(common.id)) {
        return s7_f(sc);
    }

    flecs::entity entity = g_context->world->entity(common.id.c_str());
    if (!applyPresentation(entity, common)) {
        entity.destruct();
        return s7_f(sc);
    }

    entity.set<Interactable>({
        .prompt = common.prompt,
        .eventName = common.onUse,
        .maxDistance = 5.0f,
    });
    return s7_t(sc);
}

void bindEntityApi(s7_scheme* sc) {
    s7_define_function(sc, "id", g_id, 1, 0, false, "(id value)");
    s7_define_function(sc, "at", g_at, 3, 0, false, "(at x y z)");
    s7_define_function(sc, "yaw", g_yaw, 1, 0, false, "(yaw radians)");
    s7_define_function(sc, "sprite", g_sprite, 1, 0, false, "(sprite path)");
    s7_define_function(sc, "geo", g_geo, 1, 0, false, "(geo path)");
    s7_define_function(sc, "frame", g_frame, 1, 0, false, "(frame id)");
    s7_define_function(sc, "anim", g_anim, 1, 1, false, "(anim clip [loop])");
    s7_define_function(sc, "prompt", g_prompt, 1, 0, false, "(prompt text)");
    s7_define_function(sc, "on-use", g_on_use, 1, 0, false, "(on-use handler)");
    s7_define_function(sc, "player-start", g_player_start, 0, 0, true, "(player-start clauses...)");
    s7_define_function(sc, "prop", g_prop, 0, 0, true, "(prop clauses...)");
    s7_define_function(sc, "usable", g_usable, 0, 0, true, "(usable clauses...)");
}

} // namespace

PlayerStart loadMapEntities(
    s7_scheme* scheme,
    flecs::world& world,
    AssetStore& assets,
    std::string_view mapName) {
    PlayerStart defaults{};
    if (scheme == nullptr) {
        return defaults;
    }

    const std::string virtualPath = std::string(mapName) + "/entities";
    if (!assets.hasMapEntities(virtualPath)) {
        TraceLog(LOG_INFO, "ENTITY: no entities.s7 for map '%.*s'", static_cast<int>(mapName.size()), mapName.data());
        return defaults;
    }

    EntityLoadContext context{};
    context.world = &world;
    context.assets = &assets;
    g_context = &context;
    bindEntityApi(scheme);

    const bool loaded = assets.loadMapEntities(scheme, virtualPath);
    g_context = nullptr;

    if (!loaded) {
        TraceLog(
            LOG_WARNING,
            "ENTITY: failed to evaluate entities.s7 for map '%.*s'",
            static_cast<int>(mapName.size()),
            mapName.data());
        return defaults;
    }

    if (!context.playerStart.found) {
        TraceLog(LOG_WARNING, "ENTITY: no player-start in entities.s7; using default spawn");
        return defaults;
    }

    return context.playerStart;
}

}
