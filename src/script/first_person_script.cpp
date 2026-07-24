#include "script/first_person_script.hpp"
#include "script/script_scope.hpp"

#include "assets/asset_services.hpp"
#include "assets/asset_store.hpp"
#include "assets/skeleton_loader.hpp"
#include "assets/sprite_anim_loader.hpp"
#include "assets/sprite_loader.hpp"
#include "camera/components.hpp"
#include "physics/components.hpp"
#include "physics/physics_module.hpp"
#include "render/components.hpp"
#include "render/dynamic_light.hpp"
#include "render/sprite_animator.hpp"
#include "render/transform.hpp"
#include "script/scheme_call.hpp"
#include "script/script_context.hpp"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <raylib.h>
#include <raymath.h>
#include <s7.h>

namespace slopengine {

namespace {

constexpr const char* kFpRootName = "PlayerFp";
constexpr const char* kWeaponSocketName = "weapon";
constexpr const char* kEmissionSocketName = "emission";

flecs::world* g_fpWorld = nullptr;

Matrix makeTrsMatrix(Vector3 position, Quaternion rotation, Vector3 scale) {
    const Matrix s = MatrixScale(scale.x, scale.y, scale.z);
    const Matrix r = QuaternionToMatrix(rotation);
    const Matrix t = MatrixTranslate(position.x, position.y, position.z);
    return MatrixMultiply(t, MatrixMultiply(r, s));
}

flecs::entity socketByName(flecs::world& world, const FirstPersonScene& scene, const char* name) {
    if (std::strcmp(name, kWeaponSocketName) == 0 && scene.weaponSocket != 0) {
        return world.entity(scene.weaponSocket);
    }
    if (std::strcmp(name, kEmissionSocketName) == 0 && scene.emissionSocket != 0) {
        return world.entity(scene.emissionSocket);
    }
    flecs::entity root = scene.root != 0 ? world.entity(scene.root) : world.lookup(kFpRootName);
    if (!root.is_valid()) {
        return {};
    }
    return root.lookup(name);
}

bool tryGetPlayerScene(flecs::world& world, flecs::entity& player, FirstPersonScene& scene) {
    player = world.lookup("Player");
    if (!player.is_valid() || !player.has<FirstPersonScene>()) {
        return false;
    }
    scene = player.get<FirstPersonScene>();
    return scene.root != 0;
}

void destroySocketChildren(flecs::entity socket) {
    if (!socket.is_valid()) {
        return;
    }
    std::vector<flecs::entity> children;
    socket.children([&](flecs::entity child) { children.push_back(child); });
    for (flecs::entity child : children) {
        child.destruct();
    }
}

flecs::entity findLightUnderSocket(flecs::entity socket) {
    if (!socket.is_valid()) {
        return {};
    }
    flecs::entity found{};
    socket.children([&](flecs::entity child) {
        if (!found.is_valid() && child.has<DynamicLight>()) {
            found = child;
        }
    });
    return found;
}

flecs::entity findSpriteUnderSocket(flecs::entity socket) {
    if (!socket.is_valid()) {
        return {};
    }
    flecs::entity found{};
    socket.children([&](flecs::entity child) {
        if (!found.is_valid() && child.has<SpriteInstance>() && child.has<ViewSprite>()) {
            found = child;
        }
    });
    return found;
}

s7_pointer g_fp_clear_socket(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    if (g_fpWorld == nullptr || !s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "fp-clear-socket", 1, args, "socket-name string");
    }
    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, s7_string(s7_car(args)));
    destroySocketChildren(socket);
    return s7_t(sc);
}

s7_pointer g_fp_attach_geo(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "fp-attach-geo", 1, args, "socket-name string");
    }
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest) || !s7_is_string(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "fp-attach-geo", 2, rest, "geo-path string");
    }

    const char* socketName = s7_string(s7_car(args));
    const char* geoPath = s7_string(s7_car(rest));
    rest = s7_cdr(rest);

    Vector3 position{0.2f, -0.15f, 0.35f};
    Vector3 scale{0.08f, 0.08f, 0.25f};
    float* floats[] = {
        &position.x,
        &position.y,
        &position.z,
        &scale.x,
        &scale.y,
        &scale.z,
    };
    for (float* slot : floats) {
        if (!s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
            break;
        }
        *slot = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
        rest = s7_cdr(rest);
    }

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, socketName);
    if (!socket.is_valid()) {
        return s7_f(sc);
    }

    if (!g_fpWorld->has<AssetServices>() || g_fpWorld->get<AssetServices>().store == nullptr) {
        return s7_f(sc);
    }
    AssetStore& assets = *g_fpWorld->get_mut<AssetServices>().store;
    if (!assets.hasGeo(geoPath)) {
        TraceLog(LOG_WARNING, "FP: missing geo '%s'", geoPath);
        return s7_f(sc);
    }

    destroySocketChildren(socket);

    const Model source = assets.getGeoModel(geoPath);
    Model model = cloneGeoModelInstance(source);
    if (model.meshCount <= 0) {
        TraceLog(LOG_WARNING, "FP: failed to load geo '%s'", geoPath);
        return s7_f(sc);
    }

    LocalTransformation local{};
    local.position = position;
    local.scale = scale;
    local.rotation = QuaternionIdentity();
    GlobalTransformation global{};
    global.matrix = makeTrsMatrix(local.position, local.rotation, local.scale);

    g_fpWorld->entity()
        .child_of(socket)
        .add<ViewSpace>()
        .set<LocalTransformation>(local)
        .set<GlobalTransformation>(global)
        .set<Model3D>({model, WHITE});

    flecs::entity root = g_fpWorld->entity(scene.root);
    if (root.is_valid() && root.has<LocalTransformation>() && root.has<GlobalTransformation>()) {
        updateTransform(
            root,
            root.get_mut<LocalTransformation>(),
            root.get_mut<GlobalTransformation>());
    }
    return s7_t(sc);
}

s7_pointer g_fp_attach_sprite(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "fp-attach-sprite", 1, args, "socket-name string");
    }
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest) || !s7_is_string(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "fp-attach-sprite", 2, rest, "sprite-path string");
    }

    const char* socketName = s7_string(s7_car(args));
    const char* spritePath = s7_string(s7_car(rest));
    rest = s7_cdr(rest);

    bool explicitCanvas = false;
    float explicitCanvasX = 0.0f;
    float explicitCanvasY = 0.0f;
    if (s7_is_pair(rest) && s7_is_number(s7_car(rest))) {
        explicitCanvas = true;
        explicitCanvasX = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
        rest = s7_cdr(rest);
        if (s7_is_pair(rest) && s7_is_number(s7_car(rest))) {
            explicitCanvasY = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
        } else {
            ViewCanvas canvas{};
            if (g_fpWorld->has<ViewCanvas>()) {
                canvas = g_fpWorld->get<ViewCanvas>();
            }
            explicitCanvasY = static_cast<float>(canvas.height);
        }
    }

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, socketName);
    if (!socket.is_valid()) {
        return s7_f(sc);
    }

    if (!g_fpWorld->has<AssetServices>() || g_fpWorld->get<AssetServices>().store == nullptr) {
        return s7_f(sc);
    }
    AssetStore& assets = *g_fpWorld->get_mut<AssetServices>().store;
    if (!assets.hasSprite(spritePath)) {
        TraceLog(LOG_WARNING, "FP: missing sprite '%s'", spritePath);
        return s7_f(sc);
    }

    destroySocketChildren(socket);

    ViewCanvas canvas{};
    if (g_fpWorld->has<ViewCanvas>()) {
        canvas = g_fpWorld->get<ViewCanvas>();
    }

    ViewSprite viewSprite{};
    viewSprite.canvasX = static_cast<float>(canvas.width) * 0.5f;
    viewSprite.canvasY = static_cast<float>(canvas.height);

    std::string frameId;
    if (const SpriteAsset* asset = assets.getSpriteAsset(spritePath);
        asset != nullptr) {
        if (asset->view.present) {
            viewSprite.canvasX = asset->view.canvasX;
            viewSprite.canvasY = asset->view.canvasY;
            viewSprite.scaleX = asset->view.scaleX;
            viewSprite.scaleY = asset->view.scaleY;
            viewSprite.rotationDeg = asset->view.rotationDeg;
            viewSprite.originX = asset->view.originX;
            viewSprite.originY = asset->view.originY;
        }
        if (!asset->frames.empty()) {
            frameId = asset->frames.front().id;
        }
    }
    if (explicitCanvas) {
        viewSprite.canvasX = explicitCanvasX;
        viewSprite.canvasY = explicitCanvasY;
    }

    LocalTransformation local{};
    local.scale = {1.0f, 1.0f, 1.0f};
    local.rotation = QuaternionIdentity();
    GlobalTransformation global{};
    global.matrix = MatrixIdentity();

    SpriteInstance sprite{
        .sprite = spritePath,
        .frame = frameId,
        .facingYaw = 0.0f,
    };
    flecs::entity entity = g_fpWorld->entity()
                               .child_of(socket)
                               .add<ViewSpace>()
                               .set<LocalTransformation>(local)
                               .set<GlobalTransformation>(global)
                               .set<ViewSprite>(viewSprite);

    if (assets.hasSpriteAnim(spritePath)) {
        SpriteAnimator animator{};
        animator.animPath = spritePath;
        const SpriteAnimBank* bank = assets.getSpriteAnimBank(spritePath);
        if (bank != nullptr && bank->clipIndexByName.find("idle") != bank->clipIndexByName.end()) {
            playSpriteAnim(animator, sprite, bank, "idle", true);
        }
        entity.set<SpriteInstance>(sprite);
        entity.set<SpriteAnimator>(animator);
    } else {
        entity.set<SpriteInstance>(sprite);
    }

    return s7_list(
        sc,
        2,
        s7_make_real(sc, static_cast<double>(viewSprite.canvasX)),
        s7_make_real(sc, static_cast<double>(viewSprite.canvasY)));
}

s7_pointer g_fp_set_sprite_frame(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "fp-set-sprite-frame", 1, args, "socket-name string");
    }
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest) || !s7_is_string(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "fp-set-sprite-frame", 2, rest, "frame-id string");
    }

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, s7_string(s7_car(args)));
    flecs::entity entity = findSpriteUnderSocket(socket);
    if (!entity.is_valid() || !entity.has<SpriteInstance>()) {
        return s7_f(sc);
    }

    SpriteInstance sprite = entity.get<SpriteInstance>();
    sprite.frame = s7_string(s7_car(rest));
    entity.set<SpriteInstance>(sprite);
    return s7_t(sc);
}

s7_pointer g_fp_play_sprite_anim(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "fp-play-sprite-anim", 1, args, "socket-name string");
    }
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest) || !s7_is_string(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "fp-play-sprite-anim", 2, rest, "clip-name string");
    }

    const char* clipName = s7_string(s7_car(rest));
    rest = s7_cdr(rest);
    bool loop = false;
    if (s7_is_pair(rest)) {
        loop = s7_boolean(sc, s7_car(rest));
    }

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, s7_string(s7_car(args)));
    flecs::entity entity = findSpriteUnderSocket(socket);
    if (!entity.is_valid() || !entity.has<SpriteAnimator>() || !entity.has<SpriteInstance>()) {
        return s7_f(sc);
    }

    SpriteInstance sprite = entity.get<SpriteInstance>();
    SpriteAnimator animator = entity.get<SpriteAnimator>();
    const SpriteAnimBank* bank = nullptr;
    if (g_fpWorld->has<AssetServices>() && g_fpWorld->get<AssetServices>().store != nullptr) {
        bank = g_fpWorld->get<AssetServices>().store->getSpriteAnimBank(animator.animPath);
    }
    playSpriteAnim(animator, sprite, bank, clipName, loop);
    entity.set<SpriteInstance>(sprite);
    entity.set<SpriteAnimator>(animator);
    return s7_t(sc);
}

s7_pointer g_fp_sprite_anim_busy(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "fp-sprite-anim-busy?", 1, args, "socket-name string");
    }

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, s7_string(s7_car(args)));
    flecs::entity entity = findSpriteUnderSocket(socket);
    if (!entity.is_valid() || !entity.has<SpriteAnimator>()) {
        return s7_f(sc);
    }

    const SpriteAnimator& animator = entity.get<SpriteAnimator>();
    return (animator.playing && !animator.loop) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_fp_set_sprite_pos(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "fp-set-sprite-pos", 1, args, "socket-name string");
    }
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "fp-set-sprite-pos", 2, rest, "canvas-x number");
    }
    const float canvasX = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
    rest = s7_cdr(rest);
    if (!s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "fp-set-sprite-pos", 3, rest, "canvas-y number");
    }
    const float canvasY = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, s7_string(s7_car(args)));
    flecs::entity entity = findSpriteUnderSocket(socket);
    if (!entity.is_valid() || !entity.has<ViewSprite>()) {
        return s7_f(sc);
    }

    ViewSprite& viewSprite = entity.get_mut<ViewSprite>();
    viewSprite.canvasX = canvasX;
    viewSprite.canvasY = canvasY;
    return s7_t(sc);
}

s7_pointer g_fp_sprite_pos(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "fp-sprite-pos", 1, args, "socket-name string");
    }

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, s7_string(s7_car(args)));
    flecs::entity entity = findSpriteUnderSocket(socket);
    if (!entity.is_valid() || !entity.has<ViewSprite>()) {
        return s7_f(sc);
    }

    const ViewSprite& viewSprite = entity.get<ViewSprite>();
    return s7_list(
        sc,
        2,
        s7_make_real(sc, static_cast<double>(viewSprite.canvasX)),
        s7_make_real(sc, static_cast<double>(viewSprite.canvasY)));
}

s7_pointer g_fp_set_sprite_offset(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "fp-set-sprite-offset", 1, args, "socket-name string");
    }
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "fp-set-sprite-offset", 2, rest, "offset-x number");
    }
    const float offsetX = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
    rest = s7_cdr(rest);
    if (!s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "fp-set-sprite-offset", 3, rest, "offset-y number");
    }
    const float offsetY = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, s7_string(s7_car(args)));
    flecs::entity entity = findSpriteUnderSocket(socket);
    if (!entity.is_valid() || !entity.has<ViewSprite>()) {
        return s7_f(sc);
    }

    ViewSprite& viewSprite = entity.get_mut<ViewSprite>();
    viewSprite.offsetX = offsetX;
    viewSprite.offsetY = offsetY;
    return s7_t(sc);
}

s7_pointer g_fp_sprite_offset(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "fp-sprite-offset", 1, args, "socket-name string");
    }

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, s7_string(s7_car(args)));
    flecs::entity entity = findSpriteUnderSocket(socket);
    if (!entity.is_valid() || !entity.has<ViewSprite>()) {
        return s7_f(sc);
    }

    const ViewSprite& viewSprite = entity.get<ViewSprite>();
    return s7_list(
        sc,
        2,
        s7_make_real(sc, static_cast<double>(viewSprite.offsetX)),
        s7_make_real(sc, static_cast<double>(viewSprite.offsetY)));
}

s7_pointer g_fp_set_sprite_scale(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "fp-set-sprite-scale", 1, args, "socket-name string");
    }
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "fp-set-sprite-scale", 2, rest, "scale-x number");
    }
    const float scaleX = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
    rest = s7_cdr(rest);
    if (!s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "fp-set-sprite-scale", 3, rest, "scale-y number");
    }
    const float scaleY = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, s7_string(s7_car(args)));
    flecs::entity entity = findSpriteUnderSocket(socket);
    if (!entity.is_valid() || !entity.has<ViewSprite>()) {
        return s7_f(sc);
    }

    ViewSprite& viewSprite = entity.get_mut<ViewSprite>();
    viewSprite.scaleX = scaleX;
    viewSprite.scaleY = scaleY;
    return s7_t(sc);
}

s7_pointer g_fp_set_sprite_rotation(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "fp-set-sprite-rotation", 1, args, "socket-name string");
    }
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "fp-set-sprite-rotation", 2, rest, "degrees number");
    }
    const float rotationDeg = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, s7_string(s7_car(args)));
    flecs::entity entity = findSpriteUnderSocket(socket);
    if (!entity.is_valid() || !entity.has<ViewSprite>()) {
        return s7_f(sc);
    }

    entity.get_mut<ViewSprite>().rotationDeg = rotationDeg;
    return s7_t(sc);
}

s7_pointer g_fp_set_sprite_origin(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "fp-set-sprite-origin", 1, args, "socket-name string");
    }
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "fp-set-sprite-origin", 2, rest, "origin-x number");
    }
    const float originX = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
    rest = s7_cdr(rest);
    if (!s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "fp-set-sprite-origin", 3, rest, "origin-y number");
    }
    const float originY = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, s7_string(s7_car(args)));
    flecs::entity entity = findSpriteUnderSocket(socket);
    if (!entity.is_valid() || !entity.has<ViewSprite>()) {
        return s7_f(sc);
    }

    ViewSprite& viewSprite = entity.get_mut<ViewSprite>();
    viewSprite.originX = originX;
    viewSprite.originY = originY;
    return s7_t(sc);
}

s7_pointer g_fp_spawn_light(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "fp-spawn-light", 1, args, "socket-name string");
    }
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest)) {
        return s7_wrong_type_arg_error(sc, "fp-spawn-light", 2, rest, "kind symbol/string");
    }

    const char* socketName = s7_string(s7_car(args));
    std::string kindName;
    if (s7_is_symbol(s7_car(rest))) {
        kindName = s7_symbol_name(s7_car(rest));
    } else if (s7_is_string(s7_car(rest))) {
        kindName = s7_string(s7_car(rest));
    } else {
        return s7_wrong_type_arg_error(sc, "fp-spawn-light", 2, rest, "kind symbol/string");
    }
    rest = s7_cdr(rest);

    float intensity = 1.35f;
    float range = 11.0f;
    float cone = 0.55f;
    Vector3 color{1.0f, 0.96f, 0.88f};
    Vector3 position{0.0f, 0.0f, 0.12f};
    float* floats[] = {
        &intensity,
        &range,
        &cone,
        &color.x,
        &color.y,
        &color.z,
        &position.x,
        &position.y,
        &position.z,
    };
    for (float* slot : floats) {
        if (!s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
            break;
        }
        *slot = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
        rest = s7_cdr(rest);
    }

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, socketName);
    if (!socket.is_valid()) {
        return s7_f(sc);
    }

    destroySocketChildren(socket);

    DynamicLight dyn{};
    dyn.kind = (kindName == "spot") ? DynamicLightKind::Spot : DynamicLightKind::Point;
    dyn.intensity = 0.0f;
    dyn.range = range;
    dyn.coneAngle = cone;
    dyn.castShadows = true;
    setDynamicLightRgb(dyn, color);

    LocalTransformation local{};
    local.position = position;
    local.scale = {1.0f, 1.0f, 1.0f};
    local.rotation = QuaternionIdentity();
    GlobalTransformation global{};
    global.matrix = makeTrsMatrix(local.position, local.rotation, local.scale);

    g_fpWorld->entity()
        .child_of(socket)
        .add<ViewSpace>()
        .set<LocalTransformation>(local)
        .set<GlobalTransformation>(global)
        .set<DynamicLight>(dyn)
        .set<FpLightControl>({intensity, false});

    flecs::entity root = g_fpWorld->entity(scene.root);
    if (root.is_valid() && root.has<LocalTransformation>() && root.has<GlobalTransformation>()) {
        updateTransform(
            root,
            root.get_mut<LocalTransformation>(),
            root.get_mut<GlobalTransformation>());
    }
    return s7_t(sc);
}

s7_pointer g_fp_set_light_enabled(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "fp-set-light-enabled", 1, args, "socket-name string");
    }
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest)) {
        return s7_wrong_type_arg_error(sc, "fp-set-light-enabled", 2, rest, "enabled");
    }

    const bool enabled = s7_boolean(sc, s7_car(rest));
    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, s7_string(s7_car(args)));
    flecs::entity light = findLightUnderSocket(socket);
    if (!light.is_valid() || !light.has<DynamicLight>()) {
        return s7_f(sc);
    }

    DynamicLight& dyn = light.get_mut<DynamicLight>();
    if (!light.has<FpLightControl>()) {
        light.set<FpLightControl>({dyn.intensity > 0.0f ? dyn.intensity : 1.0f, enabled});
    }
    FpLightControl& control = light.get_mut<FpLightControl>();
    control.enabled = enabled;
    dyn.intensity = enabled ? control.onIntensity : 0.0f;
    return s7_t(sc);
}

s7_pointer g_fp_set_rad_tint(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    if (g_fpWorld == nullptr || !s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "fp-set-rad-tint", 1, args, "enabled");
    }
    flecs::entity player = g_fpWorld->lookup("Player");
    if (!player.is_valid() || !player.has<FirstPersonScene>()) {
        return s7_f(sc);
    }
    player.get_mut<FirstPersonScene>().useRadTint = s7_boolean(sc, s7_car(args));
    return s7_t(sc);
}

s7_pointer g_fp_set_shading(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    if (g_fpWorld == nullptr || !s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "fp-set-shading", 1, args, "enabled");
    }
    flecs::entity player = g_fpWorld->lookup("Player");
    if (!player.is_valid() || !player.has<FirstPersonScene>()) {
        return s7_f(sc);
    }
    player.get_mut<FirstPersonScene>().useShading = s7_boolean(sc, s7_car(args));
    return s7_t(sc);
}

s7_pointer g_fp_set_eye_offset(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::FpPresent)) {
        return s7_f(sc);
    }
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_number(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "fp-set-eye-offset", 1, args, "x number");
    }
    const float x = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "fp-set-eye-offset", 2, rest, "y number");
    }
    const float y = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
    rest = s7_cdr(rest);
    if (!s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "fp-set-eye-offset", 3, rest, "z number");
    }
    const float z = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));

    flecs::entity player = g_fpWorld->lookup("Player");
    if (!player.is_valid()) {
        return s7_f(sc);
    }
    if (!player.has<ViewEyeOffset>()) {
        player.set<ViewEyeOffset>({});
    }
    ViewEyeOffset& offset = player.get_mut<ViewEyeOffset>();
    offset.x = x;
    offset.y = y;
    offset.z = z;
    return s7_t(sc);
}

s7_pointer g_player_speed(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (g_fpWorld == nullptr || !g_fpWorld->has<PhysicsContext>()) {
        return s7_make_real(sc, 0.0);
    }
    PhysicsWorld* physics = g_fpWorld->get<PhysicsContext>().world;
    if (physics == nullptr || !physics->hasPlayer()) {
        return s7_make_real(sc, 0.0);
    }
    const JPH::Vec3 vel = physics->playerVelocity();
    const float speed = std::sqrt(vel.GetX() * vel.GetX() + vel.GetZ() * vel.GetZ());
    return s7_make_real(sc, static_cast<double>(speed));
}

s7_pointer g_player_grounded(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (g_fpWorld == nullptr || !g_fpWorld->has<PhysicsContext>()) {
        return s7_f(sc);
    }
    PhysicsWorld* physics = g_fpWorld->get<PhysicsContext>().world;
    if (physics == nullptr || !physics->hasPlayer()) {
        return s7_f(sc);
    }
    return physics->playerSupported() ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_player_wish_speed(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (g_fpWorld == nullptr) {
        return s7_make_real(sc, 0.0);
    }
    flecs::entity player = g_fpWorld->lookup("Player");
    if (!player.is_valid() || !player.has<CharacterMotor>()) {
        return s7_make_real(sc, 0.0);
    }
    const CharacterMotor& motor = player.get<CharacterMotor>();
    const float speed = std::sqrt(motor.wishX * motor.wishX + motor.wishZ * motor.wishZ);
    return s7_make_real(sc, static_cast<double>(speed));
}

s7_pointer g_player_eye(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    flecs::entity player = g_fpWorld->lookup("Player");
    if (!player.is_valid() || !player.has<Lens>()) {
        return s7_f(sc);
    }
    const Vector3 eye = player.get<Lens>().camera.position;
    return s7_list(
        sc,
        3,
        s7_make_real(sc, static_cast<double>(eye.x)),
        s7_make_real(sc, static_cast<double>(eye.y)),
        s7_make_real(sc, static_cast<double>(eye.z)));
}

s7_pointer g_player_look_dir(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    flecs::entity player = g_fpWorld->lookup("Player");
    if (!player.is_valid() || !player.has<Lens>()) {
        return s7_f(sc);
    }
    const Camera3D& camera = player.get<Lens>().camera;
    const Vector3 dir = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    return s7_list(
        sc,
        3,
        s7_make_real(sc, static_cast<double>(dir.x)),
        s7_make_real(sc, static_cast<double>(dir.y)),
        s7_make_real(sc, static_cast<double>(dir.z)));
}

} // namespace

void bindFirstPersonApi(flecs::world& world, s7_scheme* scheme) {
    g_fpWorld = &world;
    if (scheme == nullptr) {
        return;
    }

    s7_define_function(scheme, "fp-clear-socket", g_fp_clear_socket, 1, 0, false, "(fp-clear-socket socket)");
    s7_define_function(scheme, "fp-attach-geo", g_fp_attach_geo, 2, 6, false,
                       "(fp-attach-geo socket geo [x y z sx sy sz])");
    s7_define_function(scheme, "fp-attach-sprite", g_fp_attach_sprite, 2, 2, false,
                       "(fp-attach-sprite socket sprite [canvas-x canvas-y]) -> (x y)");
    s7_define_function(scheme, "fp-set-sprite-frame", g_fp_set_sprite_frame, 2, 0, false,
                       "(fp-set-sprite-frame socket frame-id)");
    s7_define_function(scheme, "fp-play-sprite-anim", g_fp_play_sprite_anim, 2, 1, false,
                       "(fp-play-sprite-anim socket clip [loop])");
    s7_define_function(scheme, "fp-sprite-anim-busy?", g_fp_sprite_anim_busy, 1, 0, false,
                       "(fp-sprite-anim-busy? socket)");
    s7_define_function(scheme, "fp-set-sprite-pos", g_fp_set_sprite_pos, 3, 0, false,
                       "(fp-set-sprite-pos socket x y)");
    s7_define_function(scheme, "fp-sprite-pos", g_fp_sprite_pos, 1, 0, false,
                       "(fp-sprite-pos socket)");
    s7_define_function(scheme, "fp-set-sprite-offset", g_fp_set_sprite_offset, 3, 0, false,
                       "(fp-set-sprite-offset socket x y)");
    s7_define_function(scheme, "fp-sprite-offset", g_fp_sprite_offset, 1, 0, false,
                       "(fp-sprite-offset socket)");
    s7_define_function(scheme, "fp-set-sprite-scale", g_fp_set_sprite_scale, 3, 0, false,
                       "(fp-set-sprite-scale socket sx sy)");
    s7_define_function(scheme, "fp-set-sprite-rotation", g_fp_set_sprite_rotation, 2, 0, false,
                       "(fp-set-sprite-rotation socket degrees)");
    s7_define_function(scheme, "fp-set-sprite-origin", g_fp_set_sprite_origin, 3, 0, false,
                       "(fp-set-sprite-origin socket ox oy)");
    s7_define_function(scheme, "fp-spawn-light", g_fp_spawn_light, 2, 9, false,
                       "(fp-spawn-light socket kind [intensity range cone r g b x y z])");
    s7_define_function(scheme, "fp-set-light-enabled", g_fp_set_light_enabled, 2, 0, false,
                       "(fp-set-light-enabled socket enabled)");
    s7_define_function(scheme, "fp-set-rad-tint", g_fp_set_rad_tint, 1, 0, false,
                       "(fp-set-rad-tint enabled)");
    s7_define_function(scheme, "fp-set-shading", g_fp_set_shading, 1, 0, false,
                       "(fp-set-shading enabled)");
    s7_define_function(scheme, "fp-set-eye-offset", g_fp_set_eye_offset, 3, 0, false,
                       "(fp-set-eye-offset x y z)");
    s7_define_function(scheme, "player-speed", g_player_speed, 0, 0, false, "(player-speed)");
    s7_define_function(scheme, "player-grounded?", g_player_grounded, 0, 0, false,
                       "(player-grounded?)");
    s7_define_function(scheme, "player-wish-speed", g_player_wish_speed, 0, 0, false,
                       "(player-wish-speed)");
    s7_define_function(scheme, "player-eye", g_player_eye, 0, 0, false, "(player-eye)");
    s7_define_function(scheme, "player-look-dir", g_player_look_dir, 0, 0, false,
                       "(player-look-dir)");
}

void ensureFirstPersonScene(flecs::world& world, flecs::entity player) {
    if (!player.is_valid()) {
        return;
    }

    flecs::entity root = world.lookup(kFpRootName);
    if (!root.is_valid()) {
        LocalTransformation local{};
        local.scale = {1.0f, 1.0f, 1.0f};
        local.rotation = QuaternionIdentity();
        GlobalTransformation global{};
        global.matrix = MatrixIdentity();
        root = world.entity(kFpRootName)
                   .add<ViewSpace>()
                   .set<LocalTransformation>(local)
                   .set<GlobalTransformation>(global);
    }

    flecs::entity weapon = root.lookup(kWeaponSocketName);
    if (!weapon.is_valid()) {
        LocalTransformation local{};
        local.scale = {1.0f, 1.0f, 1.0f};
        local.rotation = QuaternionIdentity();
        GlobalTransformation global{};
        global.matrix = MatrixIdentity();
        weapon = world.entity(kWeaponSocketName)
                     .child_of(root)
                     .add<ViewSpace>()
                     .set<LocalTransformation>(local)
                     .set<GlobalTransformation>(global);
    }

    flecs::entity emission = root.lookup(kEmissionSocketName);
    if (!emission.is_valid()) {
        LocalTransformation local{};
        local.scale = {1.0f, 1.0f, 1.0f};
        local.rotation = QuaternionIdentity();
        GlobalTransformation global{};
        global.matrix = MatrixIdentity();
        emission = world.entity(kEmissionSocketName)
                       .child_of(root)
                       .add<ViewSpace>()
                       .set<LocalTransformation>(local)
                       .set<GlobalTransformation>(global);
    }

    bool useRadTint = false;
    bool useShading = false;
    bool radTintInitialized = false;
    Vector3 radTintSmoothed{1.0f, 1.0f, 1.0f};
    if (player.has<FirstPersonScene>()) {
        const FirstPersonScene& existing = player.get<FirstPersonScene>();
        useRadTint = existing.useRadTint;
        useShading = existing.useShading;
        radTintInitialized = existing.radTintInitialized;
        radTintSmoothed = existing.radTintSmoothed;
    }
    player.set<FirstPersonScene>({
        root.id(),
        weapon.id(),
        emission.id(),
        useRadTint,
        useShading,
        radTintInitialized,
        radTintSmoothed,
    });
}

void callPrepareFirstPerson(flecs::world& world) {
    if (!world.has<ScriptContext>() || world.get<ScriptContext>().scheme == nullptr) {
        return;
    }
    tryCallSchemeProc1String(
        world.get<ScriptContext>().scheme,
        "prepare-first-person",
        "Player",
        ScriptScope::World);
}

FirstPersonViewShader createFirstPersonViewShader(AssetStore& assets) {
    FirstPersonViewShader state{};
    const std::string vert = assets.getShaderSource("default/viewmodel_vert");
    const std::string frag = assets.getShaderSource("default/viewmodel_frag");
    if (vert.empty() || frag.empty()) {
        TraceLog(LOG_WARNING, "FP: missing viewmodel shaders");
        return state;
    }
    state.shader = LoadShaderFromMemory(vert.c_str(), frag.c_str());
    if (state.shader.id == 0) {
        TraceLog(LOG_WARNING, "FP: failed to compile viewmodel shaders");
        return state;
    }
    state.shader.locs[SHADER_LOC_MAP_ALBEDO] = GetShaderLocation(state.shader, "texture0");
    state.shader.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(state.shader, "colDiffuse");
    state.shader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(state.shader, "mvp");
    state.matModelLoc = GetShaderLocation(state.shader, "matModel");
    state.shader.locs[SHADER_LOC_MATRIX_MODEL] = state.matModelLoc;
    state.probeRgbLoc = GetShaderLocation(state.shader, "probeRgb");
    state.ambientLoc = GetShaderLocation(state.shader, "ambient");
    state.keyDirLoc = GetShaderLocation(state.shader, "keyDir");
    state.keyStrengthLoc = GetShaderLocation(state.shader, "keyStrength");
    state.rimStrengthLoc = GetShaderLocation(state.shader, "rimStrength");
    return state;
}

Matrix viewToWorldMatrix(const Lens& lens) {
    Vector3 forward = Vector3Subtract(lens.camera.target, lens.camera.position);
    if (Vector3LengthSqr(forward) < 1e-8f) {
        forward = {0.0f, 0.0f, 1.0f};
    } else {
        forward = Vector3Normalize(forward);
    }

    Vector3 upHint = lens.camera.up;
    if (Vector3LengthSqr(upHint) < 1e-8f) {
        upHint = {0.0f, 1.0f, 0.0f};
    } else {
        upHint = Vector3Normalize(upHint);
    }

    Vector3 right = Vector3CrossProduct(forward, upHint);
    if (Vector3LengthSqr(right) < 1e-6f) {
        const Vector3 alt =
            std::fabs(forward.y) < 0.99f ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{0.0f, 0.0f, 1.0f};
        right = Vector3CrossProduct(forward, alt);
    }
    right = Vector3Normalize(right);
    const Vector3 up = Vector3Normalize(Vector3CrossProduct(right, forward));

    Matrix basis = MatrixIdentity();
    basis.m0 = right.x;
    basis.m1 = right.y;
    basis.m2 = right.z;
    basis.m4 = up.x;
    basis.m5 = up.y;
    basis.m6 = up.z;
    basis.m8 = forward.x;
    basis.m9 = forward.y;
    basis.m10 = forward.z;
    basis.m12 = lens.camera.position.x;
    basis.m13 = lens.camera.position.y;
    basis.m14 = lens.camera.position.z;
    return basis;
}

Vector3 viewToWorldPoint(const Lens& lens, Vector3 viewPoint) {
    return Vector3Transform(viewPoint, viewToWorldMatrix(lens));
}

Vector3 viewToWorldDirection(const Lens& lens, Vector3 viewDirection) {
    const Matrix m = viewToWorldMatrix(lens);
    const Vector3 world{
        m.m0 * viewDirection.x + m.m4 * viewDirection.y + m.m8 * viewDirection.z,
        m.m1 * viewDirection.x + m.m5 * viewDirection.y + m.m9 * viewDirection.z,
        m.m2 * viewDirection.x + m.m6 * viewDirection.y + m.m10 * viewDirection.z,
    };
    if (Vector3LengthSqr(world) < 1e-8f) {
        return {0.0f, 0.0f, 1.0f};
    }
    return Vector3Normalize(world);
}

Camera3D presentationCamera(
    const Lens& lens,
    const FirstPersonController& controller,
    const ViewEyeOffset& offset) {
    const float cosPitch = std::cos(controller.pitch);
    const Vector3 forward = Vector3Normalize({
        std::sin(controller.yaw) * cosPitch,
        std::sin(controller.pitch),
        std::cos(controller.yaw) * cosPitch,
    });
    Vector3 right = Vector3CrossProduct(forward, {0.0f, 1.0f, 0.0f});
    if (Vector3LengthSqr(right) < 1e-6f) {
        right = Vector3CrossProduct(forward, {0.0f, 0.0f, 1.0f});
    }
    right = Vector3Normalize(right);
    const Vector3 up = Vector3Normalize(Vector3CrossProduct(right, forward));
    const Vector3 worldOffset = Vector3Add(
        Vector3Add(Vector3Scale(right, offset.x), Vector3Scale(up, offset.y)),
        Vector3Scale(forward, offset.z));

    Camera3D camera = lens.camera;
    camera.position = Vector3Add(lens.camera.position, worldOffset);
    camera.target = Vector3Add(camera.position, forward);
    camera.up = {0.0f, 1.0f, 0.0f};
    return camera;
}

void updateFirstPersonSceneTransforms(flecs::world world) {
    flecs::entity player = world.lookup("Player");
    if (!player.is_valid() || !player.has<FirstPersonScene>()) {
        return;
    }

    const FirstPersonScene& scene = player.get<FirstPersonScene>();
    flecs::entity root = scene.root != 0 ? world.entity(scene.root) : world.lookup(kFpRootName);
    if (!root.is_valid() || !root.has<LocalTransformation>() || !root.has<GlobalTransformation>()) {
        return;
    }

    LocalTransformation& local = root.get_mut<LocalTransformation>();
    local.position = {0.0f, 0.0f, 0.0f};
    local.rotation = QuaternionIdentity();
    local.scale = {1.0f, 1.0f, 1.0f};
    updateTransform(root, local, root.get_mut<GlobalTransformation>());
}

}
