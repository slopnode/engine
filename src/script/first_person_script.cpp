#include "script/first_person_script.hpp"
#include "map/player_bodies.hpp"
#include "script/player_target.hpp"
#include "script/s7_read.hpp"
#include "script/script_scope.hpp"

#include "assets/asset_services.hpp"
#include "assets/asset_store.hpp"
#include "assets/geo_loader.hpp"
#include "assets/skeleton_loader.hpp"
#include "assets/sprite_anim_loader.hpp"
#include "map/things_spawn.hpp"
#include "render/animation_player.hpp"
#include "render/attachment_path.hpp"
#include "render/bone_attachment.hpp"
#include "assets/sprite_loader.hpp"
#include "camera/components.hpp"
#include "camera/view_recoil.hpp"
#include "core/sim_clock.hpp"
#include "game/sim_interpolation.hpp"
#include "physics/components.hpp"
#include "physics/physics_module.hpp"
#include "render/components.hpp"
#include "render/dynamic_light.hpp"
#include "render/model_lighting.hpp"
#include "render/sprite_animator.hpp"
#include "render/transform.hpp"
#include "script/hook_registry.hpp"
#include "script/scheme_call.hpp"
#include "script/script_context.hpp"

#include <algorithm>
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
constexpr const char* kLightSocketName = "light";
constexpr const char* kFpLightRootName = "PlayerFpLight";

flecs::world* g_fpWorld = nullptr;

Matrix makeTrsMatrix(Vector3 position, Quaternion rotation, Vector3 scale) {
    const Matrix s = MatrixScale(scale.x, scale.y, scale.z);
    const Matrix r = QuaternionToMatrix(rotation);
    const Matrix t = MatrixTranslate(position.x, position.y, position.z);
    return MatrixMultiply(s, MatrixMultiply(r, t));
}

flecs::entity socketByName(flecs::world& world, const FirstPersonScene& scene, const char* name) {
    if (std::strcmp(name, kWeaponSocketName) == 0 && scene.weaponSocket != 0) {
        return world.entity(scene.weaponSocket);
    }
    if (std::strcmp(name, kEmissionSocketName) == 0 && scene.emissionSocket != 0) {
        return world.entity(scene.emissionSocket);
    }
    if (std::strcmp(name, kLightSocketName) == 0) {
        return world.lookup(kFpLightRootName);
    }
    flecs::entity root = scene.root != 0 ? world.entity(scene.root) : world.lookup(kFpRootName);
    if (!root.is_valid()) {
        return {};
    }
    return root.lookup(name);
}

bool tryGetPlayerScene(flecs::world& world, flecs::entity& player, FirstPersonScene& scene) {
    if (!scriptPlayerIsLocal()) {
        return false;
    }
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
        destroyAttachmentTree(child);
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
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-clear-socket", args);
    const char* socketName = in.string("socket-name string");
    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, socketName);
    destroySocketChildren(socket);
    return s7_t(sc);
}

s7_pointer g_fp_attach_geo(s7_scheme* sc, s7_pointer args) {
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-attach-geo", args);
    const char* socketName = in.string("socket-name string");
    const char* geoPath = in.string("geo-path string");

    if (!g_fpWorld->has<AssetServices>() || g_fpWorld->get<AssetServices>().store == nullptr) {
        return s7_f(sc);
    }
    AssetStore& assets = *g_fpWorld->get_mut<AssetServices>().store;
    if (!assets.hasGeo(geoPath)) {
        TraceLog(LOG_WARNING, "FP: missing geo '%s'", geoPath);
        return s7_f(sc);
    }

    Vector3 position{0.2f, -0.15f, 0.35f};
    Vector3 scale{0.08f, 0.08f, 0.25f};
    Vector3 euler{0.0f, 0.0f, 0.0f};
    if (const GeoAsset* geo = assets.getGeoAsset(geoPath); geo != nullptr && geo->view.present) {
        position = geo->view.position;
        scale = geo->view.scale;
        euler = {
            geo->view.rotationDeg.x * DEG2RAD,
            geo->view.rotationDeg.y * DEG2RAD,
            geo->view.rotationDeg.z * DEG2RAD,
        };
    }
    float* floats[] = {
        &position.x,
        &position.y,
        &position.z,
        &scale.x,
        &scale.y,
        &scale.z,
        &euler.x,
        &euler.y,
        &euler.z,
    };
    for (float* slot : floats) {
        *slot = in.optRealf(*slot);
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

    const Model source = assets.getGeoModel(geoPath);
    Model model = cloneGeoModelInstance(source);
    if (model.meshCount <= 0) {
        TraceLog(LOG_WARNING, "FP: failed to load geo '%s'", geoPath);
        return s7_f(sc);
    }

    LocalTransformation local{};
    local.position = position;
    local.scale = scale;
    local.rotation = QuaternionFromEuler(euler.x, euler.y, euler.z);
    GlobalTransformation global{};
    global.matrix = makeTrsMatrix(local.position, local.rotation, local.scale);

    flecs::entity attached = g_fpWorld->entity()
        .child_of(socket)
        .add<ViewSpace>()
        .add<ViewGeoRoot>()
        .set<LocalTransformation>(local)
        .set<GlobalTransformation>(global)
        .set<Model3D>({model, WHITE})
        .set<GeoSource>({geoPath});
    if (model.skeleton.boneCount > 0) {
        attachGeoAnimationPlayer(attached, assets, geoPath, {}, {}, true);
    }

    flecs::entity root = g_fpWorld->entity(scene.root);
    if (root.is_valid() && root.has<LocalTransformation>() && root.has<GlobalTransformation>()) {
        updateTransform(
            root,
            root.get_mut<LocalTransformation>(),
            root.get_mut<GlobalTransformation>());
    }
    return s7_t(sc);
}

flecs::entity findGeoUnderSocket(flecs::entity socket) {
    if (!socket.is_valid()) {
        return {};
    }
    flecs::entity found{};
    socket.children([&](flecs::entity child) {
        if (!found.is_valid() && child.has<Model3D>() && child.has<GeoSource>()) {
            found = child;
        }
    });
    return found;
}

flecs::entity resolveSocketPath(flecs::world& world, const FirstPersonScene& scene, std::string_view path) {
    const auto split = splitAttachmentPath(path, [&](std::string_view prefix) {
        return socketByName(world, scene, std::string(prefix).c_str()).is_valid();
    });
    if (!split) {
        return {};
    }
    return descendAttachmentPath(findGeoUnderSocket(socketByName(world, scene, split->anchor.c_str())), split->names);
}

s7_pointer g_fp_attach_held(s7_scheme* sc, s7_pointer args) {
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-attach-held", args);
    const char* names[4] = {"socket", "name", "point", "geo"};
    std::string values[4];
    for (int i = 0; i < 4; ++i) {
        values[i] = in.string(names[i]);
    }
    float numbers[7] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    for (float& value : numbers) {
        value = in.optRealf(value);
    }

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity host = resolveSocketPath(*g_fpWorld, scene, values[0]);
    if (!host.is_valid()) {
        TraceLog(LOG_WARNING, "fp-attach-held: '%s' names no geo", values[0].c_str());
        return s7_f(sc);
    }
    if (!g_fpWorld->has<AssetServices>() || g_fpWorld->get<AssetServices>().store == nullptr) {
        return s7_f(sc);
    }
    AssetStore& assets = *g_fpWorld->get_mut<AssetServices>().store;
    const Model& hostModel = host.get<Model3D>().model;
    BoneAttachment attachment{};
    if (!resolveBoneAttachment(
            hostModel,
            assets.getGeoAsset(host.get<GeoSource>().path),
            values[2],
            {numbers[0], numbers[1], numbers[2]},
            {numbers[3], numbers[4], numbers[5]},
            numbers[6],
            attachment)) {
        TraceLog(
            LOG_WARNING,
            "fp-attach-held: socket '%s' geo has no bone or attachment '%s'",
            values[0].c_str(),
            values[2].c_str());
        return s7_f(sc);
    }
    if (!assets.hasGeo(values[3])) {
        TraceLog(LOG_WARNING, "fp-attach-held: missing geo '%s'", values[3].c_str());
        return s7_f(sc);
    }
    const Model source = assets.getGeoModel(values[3]);
    Model model = cloneGeoModelInstance(source);
    if (model.meshCount <= 0) {
        return s7_f(sc);
    }

    destroyAttachmentTree(host.lookup(values[1].c_str()));

    LocalTransformation local{};
    local.scale = attachment.scale;
    flecs::entity child = g_fpWorld->entity().child_of(host);
    child.set_name(values[1].c_str());
    child.add<ViewSpace>()
        .set<LocalTransformation>(local)
        .set<GlobalTransformation>(GlobalTransformation{})
        .set<Model3D>({model, WHITE})
        .set<GeoSource>({values[3]})
        .set<BoneAttachment>(attachment);
    if (model.skeleton.boneCount > 0) {
        attachGeoAnimationPlayer(child, assets, values[3], {}, {}, true);
    }
    return s7_t(sc);
}

s7_pointer g_fp_detach_held(s7_scheme* sc, s7_pointer args) {
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-detach-held", args);
    const char* path = in.string("socket-name string");
    const char* name = in.string("name string");
    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity host = resolveSocketPath(*g_fpWorld, scene, path);
    if (!host.is_valid()) {
        return s7_f(sc);
    }
    flecs::entity child = findAttachmentChild(host, name);
    if (!child.is_valid()) {
        return s7_f(sc);
    }
    destroyAttachmentTree(child);
    return s7_t(sc);
}

s7_pointer g_fp_point(s7_scheme* sc, s7_pointer args) {
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-point", args);
    const std::string path = in.string("path string");
    float local[3] = {0.0f, 0.0f, 0.0f};
    for (float& value : local) {
        if (!in.more()) {
            break;
        }
        value = in.realf("x y z numbers");
    }
    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene) || !player.has<Lens>()) {
        return s7_f(sc);
    }
    flecs::entity host = resolveSocketPath(*g_fpWorld, scene, path);
    if (!host.is_valid() || !host.has<GlobalTransformation>()) {
        return s7_f(sc);
    }
    const Vector3 view =
        Vector3Transform({local[0], local[1], local[2]}, host.get<GlobalTransformation>().matrix);
    const Vector3 world = Vector3Transform(view, firstPersonWorldFromView(player.get<Lens>().camera));
    return makeVec3List(sc, world);
}

constexpr SchemeBinding kFpGeoBindings[] = {
    {"fp-clear-socket", g_fp_clear_socket, 1, 0, false, "(fp-clear-socket socket)", ScriptCap::FpPresent},
    {"fp-attach-geo", g_fp_attach_geo, 2, 9, false, "(fp-attach-geo socket geo [x y z sx sy sz pitch yaw roll])", ScriptCap::FpPresent},
    {"fp-attach-held", g_fp_attach_held, 4, 7, false, "(fp-attach-held path name point geo [x y z] [pitch yaw roll] [scale])", ScriptCap::FpPresent},
    {"fp-detach-held", g_fp_detach_held, 2, 0, false, "(fp-detach-held path name)", ScriptCap::FpPresent},
    {"fp-point", g_fp_point, 1, 3, false, "(fp-point path [x y z])", ScriptCap::ReadWorld},
};

s7_pointer g_fp_attach_sprite(s7_scheme* sc, s7_pointer args) {
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-attach-sprite", args);
    const char* socketName = in.string("socket-name string");
    const char* spritePath = in.string("sprite-path string");
    const char* initialClip = in.optString("idle");

    bool explicitAnchor = false;
    float explicitAnchorX = 0.0f;
    float explicitAnchorY = 0.0f;
    if (in.nextIsNumber()) {
        explicitAnchor = true;
        explicitAnchorX = in.realf();
        if (in.nextIsNumber()) {
            explicitAnchorY = in.realf();
        } else {
            ViewCanvas canvas{};
            if (g_fpWorld->has<ViewCanvas>()) {
                canvas = g_fpWorld->get<ViewCanvas>();
            }
            explicitAnchorY = static_cast<float>(canvas.height);
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
    viewSprite.anchorX = static_cast<float>(canvas.width) * 0.5f;
    viewSprite.anchorY = static_cast<float>(canvas.height);

    std::string frameId;
    if (const SpriteAsset* asset = assets.getSpriteAsset(spritePath);
        asset != nullptr) {
        if (asset->view.present) {
            viewSprite.anchorX = asset->view.anchorX;
            viewSprite.anchorY = asset->view.anchorY;
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
    if (explicitAnchor) {
        viewSprite.anchorX = explicitAnchorX;
        viewSprite.anchorY = explicitAnchorY;
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
        if (bank != nullptr && bank->clipIndexByName.find(initialClip) != bank->clipIndexByName.end()) {
            playSpriteAnim(animator, sprite, bank, initialClip, true);
        } else if (bank != nullptr && bank->clipIndexByName.find("idle") != bank->clipIndexByName.end()) {
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
        s7_make_real(sc, static_cast<double>(viewSprite.anchorX)),
        s7_make_real(sc, static_cast<double>(viewSprite.anchorY)));
}

s7_pointer g_fp_set_sprite_frame(s7_scheme* sc, s7_pointer args) {
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-set-sprite-frame", args);
    const char* socketName = in.string("socket-name string");
    const char* frameId = in.string("frame-id string");

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, socketName);
    flecs::entity entity = findSpriteUnderSocket(socket);
    if (!entity.is_valid() || !entity.has<SpriteInstance>()) {
        return s7_f(sc);
    }

    SpriteInstance sprite = entity.get<SpriteInstance>();
    sprite.frame = frameId;
    entity.set<SpriteInstance>(sprite);
    return s7_t(sc);
}

s7_pointer g_fp_play_anim(s7_scheme* sc, s7_pointer args) {
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-play-anim", args);
    const char* path = in.string("socket-name string");
    const char* clipName = in.string("clip-name string");
    const bool loop = in.more() && in.truthy();

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity entity = resolveSocketPath(*g_fpWorld, scene, path);
    if (!entity.is_valid() || !entity.has<AnimationPlayer>()) {
        return s7_f(sc);
    }
    AnimationPlayer animPlayer = entity.get<AnimationPlayer>();
    const AnimBank* bank = nullptr;
    if (g_fpWorld->has<AssetServices>() && g_fpWorld->get<AssetServices>().store != nullptr) {
        bank = g_fpWorld->get_mut<AssetServices>().store->getAnimBank(animPlayer.animBankPath);
    }
    if (bank == nullptr || bank->clipIndexByName.count(clipName) == 0) {
        TraceLog(
            LOG_WARNING,
            "FP: bank '%s' has no clip '%s'",
            animPlayer.animBankPath.c_str(),
            clipName);
        return s7_f(sc);
    }
    animPlayer.play(clipName, loop);
    entity.set<AnimationPlayer>(animPlayer);
    return s7_t(sc);
}

s7_pointer g_fp_anim_busy(s7_scheme* sc, s7_pointer args) {
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-anim-busy?", args);
    const char* path = in.string("socket-name string");
    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity entity = resolveSocketPath(*g_fpWorld, scene, path);
    if (!entity.is_valid() || !entity.has<AnimationPlayer>()) {
        return s7_f(sc);
    }
    const AnimationPlayer& animPlayer = entity.get<AnimationPlayer>();
    return s7_make_boolean(sc, animPlayer.playing && !animPlayer.loop);
}

s7_pointer g_fp_play_sprite_anim(s7_scheme* sc, s7_pointer args) {
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-play-sprite-anim", args);
    const char* socketName = in.string("socket-name string");
    const char* clipName = in.string("clip-name string");
    const bool loop = in.more() && in.truthy();

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, socketName);
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
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-sprite-anim-busy?", args);
    const char* socketName = in.string("socket-name string");

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, socketName);
    flecs::entity entity = findSpriteUnderSocket(socket);
    if (!entity.is_valid() || !entity.has<SpriteAnimator>()) {
        return s7_f(sc);
    }

    const SpriteAnimator& animator = entity.get<SpriteAnimator>();
    return s7_make_boolean(sc, animator.playing && !animator.loop);
}

s7_pointer g_fp_set_sprite_pos(s7_scheme* sc, s7_pointer args) {
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-set-sprite-pos", args);
    const char* socketName = in.string("socket-name string");
    const float anchorX = in.realf("anchor-x number");
    const float anchorY = in.realf("anchor-y number");

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, socketName);
    flecs::entity entity = findSpriteUnderSocket(socket);
    if (!entity.is_valid() || !entity.has<ViewSprite>()) {
        return s7_f(sc);
    }

    ViewSprite& viewSprite = entity.get_mut<ViewSprite>();
    viewSprite.anchorX = anchorX;
    viewSprite.anchorY = anchorY;
    return s7_t(sc);
}

s7_pointer g_fp_sprite_pos(s7_scheme* sc, s7_pointer args) {
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-sprite-pos", args);
    const char* socketName = in.string("socket-name string");

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, socketName);
    flecs::entity entity = findSpriteUnderSocket(socket);
    if (!entity.is_valid() || !entity.has<ViewSprite>()) {
        return s7_f(sc);
    }

    const ViewSprite& viewSprite = entity.get<ViewSprite>();
    return s7_list(
        sc,
        2,
        s7_make_real(sc, static_cast<double>(viewSprite.anchorX)),
        s7_make_real(sc, static_cast<double>(viewSprite.anchorY)));
}

s7_pointer g_fp_set_sprite_offset(s7_scheme* sc, s7_pointer args) {
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-set-sprite-offset", args);
    const char* socketName = in.string("socket-name string");
    const float offsetX = in.realf("offset-x number");
    const float offsetY = in.realf("offset-y number");

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, socketName);
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
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-sprite-offset", args);
    const char* socketName = in.string("socket-name string");

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, socketName);
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
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-set-sprite-scale", args);
    const char* socketName = in.string("socket-name string");
    const float scaleX = in.realf("scale-x number");
    const float scaleY = in.realf("scale-y number");

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, socketName);
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
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-set-sprite-rotation", args);
    const char* socketName = in.string("socket-name string");
    const float rotationDeg = in.realf("degrees number");

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, socketName);
    flecs::entity entity = findSpriteUnderSocket(socket);
    if (!entity.is_valid() || !entity.has<ViewSprite>()) {
        return s7_f(sc);
    }

    entity.get_mut<ViewSprite>().rotationDeg = rotationDeg;
    return s7_t(sc);
}

s7_pointer g_fp_set_sprite_origin(s7_scheme* sc, s7_pointer args) {
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-set-sprite-origin", args);
    const char* socketName = in.string("socket-name string");
    const float originX = in.realf("origin-x number");
    const float originY = in.realf("origin-y number");

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, socketName);
    flecs::entity entity = findSpriteUnderSocket(socket);
    if (!entity.is_valid() || !entity.has<ViewSprite>()) {
        return s7_f(sc);
    }

    ViewSprite& viewSprite = entity.get_mut<ViewSprite>();
    viewSprite.originX = originX;
    viewSprite.originY = originY;
    return s7_t(sc);
}

constexpr SchemeBinding kFpSpriteBindings[] = {
    {"fp-play-anim", g_fp_play_anim, 2, 1, false, "(fp-play-anim path clip [loop])", ScriptCap::FpPresent},
    {"fp-anim-busy?", g_fp_anim_busy, 1, 0, false, "(fp-anim-busy? path)", ScriptCap::FpPresent},
    {"fp-attach-sprite", g_fp_attach_sprite, 2, 3, false, "(fp-attach-sprite socket sprite [clip] [canvas-x canvas-y]) -> (x y)", ScriptCap::FpPresent},
    {"fp-set-sprite-frame", g_fp_set_sprite_frame, 2, 0, false, "(fp-set-sprite-frame socket frame-id)", ScriptCap::FpPresent},
    {"fp-play-sprite-anim", g_fp_play_sprite_anim, 2, 1, false, "(fp-play-sprite-anim socket clip [loop])", ScriptCap::FpPresent},
    {"fp-sprite-anim-busy?", g_fp_sprite_anim_busy, 1, 0, false, "(fp-sprite-anim-busy? socket)", ScriptCap::FpPresent},
    {"fp-set-sprite-pos", g_fp_set_sprite_pos, 3, 0, false, "(fp-set-sprite-pos socket x y)", ScriptCap::FpPresent},
    {"fp-sprite-pos", g_fp_sprite_pos, 1, 0, false, "(fp-sprite-pos socket)", ScriptCap::FpPresent},
    {"fp-set-sprite-offset", g_fp_set_sprite_offset, 3, 0, false, "(fp-set-sprite-offset socket x y)", ScriptCap::FpPresent},
    {"fp-sprite-offset", g_fp_sprite_offset, 1, 0, false, "(fp-sprite-offset socket)", ScriptCap::FpPresent},
    {"fp-set-sprite-scale", g_fp_set_sprite_scale, 3, 0, false, "(fp-set-sprite-scale socket sx sy)", ScriptCap::FpPresent},
    {"fp-set-sprite-rotation", g_fp_set_sprite_rotation, 2, 0, false, "(fp-set-sprite-rotation socket degrees)", ScriptCap::FpPresent},
    {"fp-set-sprite-origin", g_fp_set_sprite_origin, 3, 0, false, "(fp-set-sprite-origin socket ox oy)", ScriptCap::FpPresent},
};

s7_pointer g_fp_spawn_light(s7_scheme* sc, s7_pointer args) {
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-spawn-light", args);
    const char* socketName = in.string("socket-name string");
    const std::string kindName = in.name("kind symbol/string");

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
        *slot = in.optRealf(*slot);
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
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-set-light-enabled", args);
    const char* socketName = in.string("socket-name string");
    const bool enabled = in.truthy("enabled");

    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(*g_fpWorld, player, scene)) {
        return s7_f(sc);
    }
    flecs::entity socket = socketByName(*g_fpWorld, scene, socketName);
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
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-set-rad-tint", args);
    const bool enabled = in.truthy("enabled");
    flecs::entity player = scriptPlayerIsLocal() ? g_fpWorld->lookup("Player") : flecs::entity{};
    if (!player.is_valid() || !player.has<FirstPersonScene>()) {
        return s7_f(sc);
    }
    player.get_mut<FirstPersonScene>().useRadTint = enabled;
    return s7_t(sc);
}

s7_pointer g_fp_set_shading(s7_scheme* sc, s7_pointer args) {
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-set-shading", args);
    const bool enabled = in.truthy("enabled");
    flecs::entity player = scriptPlayerIsLocal() ? g_fpWorld->lookup("Player") : flecs::entity{};
    if (!player.is_valid() || !player.has<FirstPersonScene>()) {
        return s7_f(sc);
    }
    player.get_mut<FirstPersonScene>().useShading = enabled;
    return s7_t(sc);
}

s7_pointer g_fp_set_eye_offset(s7_scheme* sc, s7_pointer args) {
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-set-eye-offset", args);
    const float x = in.realf("x number");
    const float y = in.realf("y number");
    const float z = in.realf("z number");

    flecs::entity player = scriptPlayerIsLocal() ? g_fpWorld->lookup("Player") : flecs::entity{};
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

s7_pointer g_fp_set_view_roll(s7_scheme* sc, s7_pointer args) {
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-set-view-roll", args);
    const float roll = in.realf("roll number");
    flecs::entity player = scriptPlayerIsLocal() ? g_fpWorld->lookup("Player") : flecs::entity{};
    if (!player.is_valid()) {
        return s7_f(sc);
    }
    if (!player.has<ViewEyeOffset>()) {
        player.set<ViewEyeOffset>({});
    }
    player.get_mut<ViewEyeOffset>().roll = roll;
    return s7_t(sc);
}

s7_pointer g_fp_set_view_offset(s7_scheme* sc, s7_pointer args) {
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-set-view-offset", args);
    const Vector3 offset = in.vec3("x y z numbers");
    flecs::entity player = scriptPlayerIsLocal() ? g_fpWorld->lookup("Player") : flecs::entity{};
    if (!player.is_valid()) {
        return s7_f(sc);
    }
    player.set<ViewModelOffset>({offset.x, offset.y, offset.z});
    return s7_t(sc);
}

constexpr SchemeBinding kFpViewBindings[] = {
    {"fp-spawn-light", g_fp_spawn_light, 2, 9, false, "(fp-spawn-light socket kind [intensity range cone r g b x y z])", ScriptCap::FpPresent},
    {"fp-set-light-enabled", g_fp_set_light_enabled, 2, 0, false, "(fp-set-light-enabled socket enabled)", ScriptCap::FpPresent},
    {"fp-set-rad-tint", g_fp_set_rad_tint, 1, 0, false, "(fp-set-rad-tint enabled)", ScriptCap::FpPresent},
    {"fp-set-shading", g_fp_set_shading, 1, 0, false, "(fp-set-shading enabled)", ScriptCap::FpPresent},
    {"fp-set-eye-offset", g_fp_set_eye_offset, 3, 0, false, "(fp-set-eye-offset x y z)", ScriptCap::FpPresent},
    {"fp-set-view-roll", g_fp_set_view_roll, 1, 0, false, "(fp-set-view-roll radians)", ScriptCap::FpPresent},
    {"fp-set-view-offset", g_fp_set_view_offset, 3, 0, false, "(fp-set-view-offset x y z)", ScriptCap::FpPresent},
};

ViewRecoil* playerViewRecoil() {
    if (g_fpWorld == nullptr) {
        return nullptr;
    }
    flecs::entity player = g_fpWorld->lookup("Player");
    if (!player.is_valid()) {
        return nullptr;
    }
    if (!player.has<ViewRecoil>()) {
        player.set<ViewRecoil>({});
    }
    return player.try_get_mut<ViewRecoil>();
}

bool readAlistReal(s7_scheme* sc, s7_pointer value, float& out) {
    if (s7_is_pair(value) && s7_is_null(sc, s7_cdr(value))) {
        value = s7_car(value);
    }
    return readReal(sc, value, out);
}

bool applyRecoilTuneEntry(
    s7_scheme* sc,
    const char* key,
    s7_pointer value,
    ViewRecoilTuning& tuning) {
    struct RealKey {
        const char* name;
        float* target;
        float scale;
    };
    const RealKey reals[] = {
        {"aim-frequency", &tuning.aim.frequency, 1.0f},
        {"aim-damping", &tuning.aim.damping, 1.0f},
        {"kick-frequency", &tuning.kick.frequency, 1.0f},
        {"kick-damping", &tuning.kick.damping, 1.0f},
        {"twist-frequency", &tuning.twist.frequency, 1.0f},
        {"twist-damping", &tuning.twist.damping, 1.0f},
        {"climb-time", &tuning.climbTime, 1.0f},
        {"recovery", &tuning.recovery, 1.0f},
        {"recovery-delay", &tuning.recoveryDelay, 1.0f},
        {"recovery-time", &tuning.recoveryTime, 1.0f},
        {"shake-decay", &tuning.shakeDecay, 1.0f},
        {"shake-frequency", &tuning.shakeFrequency, 1.0f},
        {"shake-angle", &tuning.shakeAngle, DEG2RAD},
    };
    if (std::strcmp(key, "pivot") == 0) {
        return readVec3List(sc, value, tuning.pivot);
    }
    for (const RealKey& entry : reals) {
        if (std::strcmp(key, entry.name) == 0) {
            float v = 0.0f;
            if (!readAlistReal(sc, value, v)) {
                return false;
            }
            *entry.target = v * entry.scale;
            return true;
        }
    }
    return false;
}

bool applyRecoilKickEntry(s7_scheme* sc, const char* key, s7_pointer value, ViewRecoilKick& kick) {
    struct RealKey {
        const char* name;
        float* target;
        float scale;
    };
    const RealKey reals[] = {
        {"punch-up", &kick.punchUp, DEG2RAD},
        {"punch-right", &kick.punchRight, DEG2RAD},
        {"climb-up", &kick.climbUp, DEG2RAD},
        {"climb-right", &kick.climbRight, DEG2RAD},
        {"rise", &kick.rise, DEG2RAD},
        {"turn", &kick.turn, DEG2RAD},
        {"roll", &kick.roll, DEG2RAD},
        {"back", &kick.back, 1.0f},
        {"lift", &kick.lift, 1.0f},
        {"side", &kick.side, 1.0f},
        {"shake", &kick.shake, 1.0f},
    };
    for (const RealKey& entry : reals) {
        if (std::strcmp(key, entry.name) == 0) {
            float v = 0.0f;
            if (!readAlistReal(sc, value, v)) {
                return false;
            }
            *entry.target = v * entry.scale;
            return true;
        }
    }
    return false;
}

s7_pointer g_fp_recoil_tune(s7_scheme* sc, s7_pointer args) {
    if (!scriptPlayerIsLocal()) {
        return s7_f(sc);
    }
    ViewRecoil* recoil = playerViewRecoil();
    if (recoil == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-recoil-tune!", args);
    s7_pointer alist = in.alist("alist of recoil tuning keys");
    ViewRecoilTuning tuning{};
    s7_pointer rejected = rejectedAlistEntry(alist, [&](const char* key, s7_pointer value) {
        return applyRecoilTuneEntry(sc, key, value, tuning);
    });
    if (rejected != nullptr) {
        return in.wrongTypeAt(1, rejected, "alist of recoil tuning keys");
    }
    recoil->tuning = tuning;
    return s7_t(sc);
}

s7_pointer g_fp_recoil_kick(s7_scheme* sc, s7_pointer args) {
    if (!scriptPlayerIsLocal()) {
        return s7_f(sc);
    }
    ViewRecoil* recoil = playerViewRecoil();
    if (recoil == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-recoil-kick!", args);
    s7_pointer alist = in.alist("alist of recoil kick keys");
    ViewRecoilKick kick{};
    s7_pointer rejected = rejectedAlistEntry(alist, [&](const char* key, s7_pointer value) {
        return applyRecoilKickEntry(sc, key, value, kick);
    });
    if (rejected != nullptr) {
        return in.wrongTypeAt(1, rejected, "alist of recoil kick keys");
    }
    applyViewRecoilKick(*recoil, kick);
    return s7_t(sc);
}

s7_pointer g_fp_recoil_reset(s7_scheme* sc, s7_pointer) {
    if (!scriptPlayerIsLocal()) {
        return s7_f(sc);
    }
    ViewRecoil* recoil = playerViewRecoil();
    if (recoil == nullptr) {
        return s7_f(sc);
    }
    resetViewRecoil(*recoil);
    return s7_t(sc);
}

s7_pointer g_fp_view_punch(s7_scheme* sc, s7_pointer args) {
    if (!scriptPlayerIsLocal()) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-view-punch!", args);
    float angles[3] = {0.0f, 0.0f, 0.0f};
    for (float& angle : angles) {
        angle = in.realf("up right roll degrees") * DEG2RAD;
    }
    float recover = 1.0f;
    if (in.more()) {
        recover = in.realf("recover number");
    }
    ViewRecoil* recoil = playerViewRecoil();
    if (recoil == nullptr) {
        return s7_f(sc);
    }
    setViewPunch(*recoil, {angles[0], angles[1], angles[2]}, recover);
    return s7_t(sc);
}

s7_pointer g_fp_set_weapon_hidden(s7_scheme* sc, s7_pointer args) {
    if (!scriptPlayerIsLocal()) {
        return s7_f(sc);
    }
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "fp-set-weapon-hidden!", args);
    const bool hidden = in.truthy("hidden boolean");
    if (!g_fpWorld->has<FpViewOverride>()) {
        g_fpWorld->set<FpViewOverride>({});
    }
    g_fpWorld->get_mut<FpViewOverride>().hideWeapon = hidden;
    return s7_t(sc);
}

s7_pointer g_extra_eye_configure(s7_scheme* sc, s7_pointer args) {
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "extra-eye-configure!", args);
    const int width = in.integer("width number");
    const int height = in.integer("height number");
    const float fovy = in.realf("fovy number");

    flecs::entity player = scriptPlayerIsLocal() ? g_fpWorld->lookup("Player") : flecs::entity{};
    if (!player.is_valid()) {
        return s7_f(sc);
    }
    if (!player.has<ExtraEye>()) {
        ExtraEye eye{};
        eye.width = width;
        eye.height = height;
        eye.fovy = fovy;
        player.set<ExtraEye>(eye);
    } else {
        ExtraEye& eye = player.get_mut<ExtraEye>();
        eye.width = width;
        eye.height = height;
        eye.fovy = fovy;
    }
    return s7_t(sc);
}

s7_pointer g_extra_eye_set_active(s7_scheme* sc, s7_pointer args) {
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "extra-eye-set-active!", args);
    const bool active = in.truthy("active boolean");
    flecs::entity player = scriptPlayerIsLocal() ? g_fpWorld->lookup("Player") : flecs::entity{};
    if (!player.is_valid() || !player.has<ExtraEye>()) {
        return s7_f(sc);
    }
    player.get_mut<ExtraEye>().active = active;
    return s7_t(sc);
}

constexpr SchemeBinding kFpRecoilBindings[] = {
    {"fp-recoil-tune!", g_fp_recoil_tune, 1, 0, false, "(fp-recoil-tune! alist)", ScriptCap::FpPresent},
    {"fp-recoil-kick!", g_fp_recoil_kick, 1, 0, false, "(fp-recoil-kick! alist)", ScriptCap::FpPresent},
    {"fp-recoil-reset!", g_fp_recoil_reset, 0, 0, false, "(fp-recoil-reset!)", ScriptCap::FpPresent},
    {"fp-view-punch!", g_fp_view_punch, 3, 1, false, "(fp-view-punch! up right roll [recover])", ScriptCap::FpPresent},
    {"fp-set-weapon-hidden!", g_fp_set_weapon_hidden, 1, 0, false, "(fp-set-weapon-hidden! hidden)", ScriptCap::FpPresent},
    {"extra-eye-configure!", g_extra_eye_configure, 3, 0, false, "(extra-eye-configure! width height fovy)", ScriptCap::FpPresent},
    {"extra-eye-set-active!", g_extra_eye_set_active, 1, 0, false, "(extra-eye-set-active! active)", ScriptCap::FpPresent},
};

Vector3 remoteVelocity(flecs::entity player) {
    const SimEyePose* eye = player.try_get<SimEyePose>();
    if (eye == nullptr) {
        return {};
    }
    return Vector3Scale(Vector3Subtract(eye->cur, eye->prev), 1.0f / kSimTickDt);
}

s7_pointer g_player_speed(s7_scheme* sc, s7_pointer) {
    if (g_fpWorld == nullptr || !g_fpWorld->has<PhysicsContext>()) {
        return s7_make_real(sc, 0.0);
    }
    PhysicsWorld* physics = g_fpWorld->get<PhysicsContext>().world;
    const flecs::entity player = scriptPlayer(*g_fpWorld);
    if (player.is_valid() && g_fpWorld->has<SimRemote>()) {
        const Vector3 velocity = remoteVelocity(player);
        return s7_make_real(sc, std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z));
    }
    if (physics == nullptr || !player.is_valid() || !physics->hasCharacter(static_cast<std::uint64_t>(player.id()))) {
        return s7_make_real(sc, 0.0);
    }
    const JPH::Vec3 vel = physics->characterVelocity(static_cast<std::uint64_t>(player.id()));
    const float speed = std::sqrt(vel.GetX() * vel.GetX() + vel.GetZ() * vel.GetZ());
    return s7_make_real(sc, static_cast<double>(speed));
}

s7_pointer g_player_velocity(s7_scheme* sc, s7_pointer args) {
    if (g_fpWorld == nullptr || !g_fpWorld->has<PhysicsContext>()) {
        return makeVec3List(sc, 0.0, 0.0, 0.0);
    }
    PhysicsWorld* physics = g_fpWorld->get<PhysicsContext>().world;
    const flecs::entity player = scriptPlayer(*g_fpWorld, args);
    if (player.is_valid() && g_fpWorld->has<SimRemote>()) {
        const Vector3 velocity = remoteVelocity(player);
        return makeVec3List(sc, velocity.x, velocity.y, velocity.z);
    }
    if (physics == nullptr || !player.is_valid() || !physics->hasCharacter(static_cast<std::uint64_t>(player.id()))) {
        return makeVec3List(sc, 0.0, 0.0, 0.0);
    }
    const JPH::Vec3 vel = physics->characterVelocity(static_cast<std::uint64_t>(player.id()));
    return makeVec3List(sc, vel.GetX(), vel.GetY(), vel.GetZ());
}

s7_pointer g_player_grounded(s7_scheme* sc, s7_pointer args) {
    if (g_fpWorld == nullptr || !g_fpWorld->has<PhysicsContext>()) {
        return s7_f(sc);
    }
    PhysicsWorld* physics = g_fpWorld->get<PhysicsContext>().world;
    const flecs::entity player = scriptPlayer(*g_fpWorld, args);
    if (physics == nullptr || !player.is_valid()) {
        return s7_f(sc);
    }
    return s7_make_boolean(sc, physics->characterSupported(static_cast<std::uint64_t>(player.id())));
}

s7_pointer g_players(s7_scheme* sc, s7_pointer) {
    if (g_fpWorld == nullptr) {
        return s7_nil(sc);
    }
    return makeStringList(sc, playerBodyIds(*g_fpWorld));
}

s7_pointer g_player_jump(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "player-jump!", args);
    const float speed = in.realf("speed number");
    if (g_fpWorld == nullptr || !g_fpWorld->has<PhysicsContext>()) {
        return s7_f(sc);
    }
    PhysicsWorld* physics = g_fpWorld->get<PhysicsContext>().world;
    const flecs::entity player = scriptPlayer(*g_fpWorld);
    if (physics == nullptr || !player.is_valid() || !physics->hasCharacter(static_cast<std::uint64_t>(player.id()))) {
        return s7_f(sc);
    }
    return s7_make_boolean(sc, physics->launchCharacter(static_cast<std::uint64_t>(player.id()), speed));
}

s7_pointer g_player_set_gravity(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "player-set-gravity!", args);
    const float gravity = in.realf("gravity number");
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    flecs::entity player = scriptPlayer(*g_fpWorld);
    if (!player.is_valid() || !player.has<CharacterMotor>()) {
        return s7_f(sc);
    }
    player.get_mut<CharacterMotor>().gravity = gravity;
    return s7_t(sc);
}

s7_pointer g_player_move_speed(s7_scheme* sc, s7_pointer) {
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    flecs::entity player = scriptPlayer(*g_fpWorld);
    if (!player.is_valid() || !player.has<CharacterMotor>()) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(player.get<CharacterMotor>().moveSpeed));
}

s7_pointer g_player_set_move_speed(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "player-set-move-speed!", args);
    const float speed = std::max(0.0f, in.realf("speed number"));
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    flecs::entity player = scriptPlayer(*g_fpWorld);
    if (!player.is_valid() || !player.has<CharacterMotor>()) {
        return s7_f(sc);
    }
    player.get_mut<CharacterMotor>().moveSpeed = speed;
    if (player.has<FirstPersonController>()) {
        player.get_mut<FirstPersonController>().moveSpeed = speed;
    }
    return s7_t(sc);
}

s7_pointer g_player_wish_speed(s7_scheme* sc, s7_pointer) {
    if (g_fpWorld == nullptr) {
        return s7_make_real(sc, 0.0);
    }
    flecs::entity player = scriptPlayer(*g_fpWorld);
    if (!player.is_valid() || !player.has<CharacterMotor>()) {
        return s7_make_real(sc, 0.0);
    }
    const CharacterMotor& motor = player.get<CharacterMotor>();
    const float speed = std::sqrt(motor.wishX * motor.wishX + motor.wishZ * motor.wishZ);
    return s7_make_real(sc, static_cast<double>(speed));
}

s7_pointer g_player_eye(s7_scheme* sc, s7_pointer args) {
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    flecs::entity player = scriptPlayer(*g_fpWorld, args);
    if (!player.is_valid() || !player.has<Lens>()) {
        return s7_f(sc);
    }
    return makeVec3List(sc, player.get<Lens>().camera.position);
}

s7_pointer g_player_look_dir(s7_scheme* sc, s7_pointer args) {
    if (g_fpWorld == nullptr) {
        return s7_f(sc);
    }
    flecs::entity player = scriptPlayer(*g_fpWorld, args);
    if (!player.is_valid() || !player.has<Lens>()) {
        return s7_f(sc);
    }
    const Camera3D& camera = player.get<Lens>().camera;
    return makeVec3List(sc, Vector3Normalize(Vector3Subtract(camera.target, camera.position)));
}

constexpr SchemeBinding kPlayerBindings[] = {
    {"player-speed", g_player_speed, 0, 0, false, "(player-speed)", ScriptCap::ReadWorld},
    {"player-velocity", g_player_velocity, 0, 1, false, "(player-velocity [id])", ScriptCap::ReadWorld},
    {"player-grounded?", g_player_grounded, 0, 1, false, "(player-grounded? [id])", ScriptCap::ReadWorld},
    {"players", g_players, 0, 0, false, "(players)", ScriptCap::ReadWorld},
    {"player-jump!", g_player_jump, 1, 0, false, "(player-jump! speed)", ScriptCap::WorldMutate},
    {"player-set-gravity!", g_player_set_gravity, 1, 0, false, "(player-set-gravity! gravity)", ScriptCap::WorldMutate},
    {"player-move-speed", g_player_move_speed, 0, 0, false, "(player-move-speed)", ScriptCap::ReadWorld},
    {"player-set-move-speed!", g_player_set_move_speed, 1, 0, false, "(player-set-move-speed! speed)", ScriptCap::WorldMutate},
    {"player-wish-speed", g_player_wish_speed, 0, 0, false, "(player-wish-speed)", ScriptCap::ReadWorld},
    {"player-eye", g_player_eye, 0, 1, false, "(player-eye [id])", ScriptCap::ReadWorld},
    {"player-look-dir", g_player_look_dir, 0, 1, false, "(player-look-dir [id])", ScriptCap::ReadWorld},
};

} // namespace

void bindFirstPersonApi(flecs::world& world, s7_scheme* scheme) {
    g_fpWorld = &world;
    if (scheme == nullptr) {
        return;
    }
    registerBindings(scheme, kFpGeoBindings);
    registerBindings(scheme, kFpSpriteBindings);
    registerBindings(scheme, kFpViewBindings);
    registerBindings(scheme, kFpRecoilBindings);
    registerBindings(scheme, kPlayerBindings);
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

    if (!world.lookup(kFpLightRootName).is_valid()) {
        LocalTransformation local{};
        local.scale = {1.0f, 1.0f, 1.0f};
        local.rotation = QuaternionIdentity();
        GlobalTransformation global{};
        global.matrix = MatrixIdentity();
        world.entity(kFpLightRootName)
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
    callHook1String(
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
    const float pitch = controller.pitch + offset.shakeUp;
    const float yaw = controller.yaw - offset.shakeRight;
    const float cosPitch = std::cos(pitch);
    const Vector3 forward = Vector3Normalize({
        std::sin(yaw) * cosPitch,
        std::sin(pitch),
        std::cos(yaw) * cosPitch,
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
    const float roll = offset.roll + offset.shakeRoll;
    if (roll != 0.0f) {
        camera.up = Vector3Normalize(Vector3Add(
            Vector3Scale(up, std::cos(roll)),
            Vector3Scale(right, std::sin(roll))));
    }
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
    if (player.has<ViewModelOffset>()) {
        const ViewModelOffset& offset = player.get<ViewModelOffset>();
        local.position = {offset.x, offset.y, offset.z};
    }
    local.rotation = QuaternionIdentity();
    if (player.has<ViewRecoil>()) {
        const ViewRecoil& recoil = player.get<ViewRecoil>();
        local.position = Vector3Add(local.position, viewRecoilKickPosition(recoil));
        local.rotation = viewRecoilKickRotation(recoil);
    }
    local.scale = {1.0f, 1.0f, 1.0f};
    updateTransform(root, local, root.get_mut<GlobalTransformation>());
}

flecs::entity findFirstPersonSocketSprite(flecs::world& world, const char* socketName) {
    flecs::entity player;
    FirstPersonScene scene{};
    if (!tryGetPlayerScene(world, player, scene) || socketName == nullptr) {
        return {};
    }
    return findSpriteUnderSocket(socketByName(world, scene, socketName));
}

}
