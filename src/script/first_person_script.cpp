#include "script/first_person_script.hpp"

#include "assets/asset_services.hpp"
#include "assets/asset_store.hpp"
#include "assets/skeleton_loader.hpp"
#include "camera/components.hpp"
#include "render/components.hpp"
#include "render/dynamic_light.hpp"
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

s7_pointer g_fp_clear_socket(s7_scheme* sc, s7_pointer args) {
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

s7_pointer g_fp_spawn_light(s7_scheme* sc, s7_pointer args) {
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
    dyn.castShadows = false;
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

} // namespace

void bindFirstPersonApi(flecs::world& world, s7_scheme* scheme) {
    g_fpWorld = &world;
    if (scheme == nullptr) {
        return;
    }

    s7_define_function(scheme, "fp-clear-socket", g_fp_clear_socket, 1, 0, false, "(fp-clear-socket socket)");
    s7_define_function(scheme, "fp-attach-geo", g_fp_attach_geo, 2, 6, false,
                       "(fp-attach-geo socket geo [x y z sx sy sz])");
    s7_define_function(scheme, "fp-spawn-light", g_fp_spawn_light, 2, 9, false,
                       "(fp-spawn-light socket kind [intensity range cone r g b x y z])");
    s7_define_function(scheme, "fp-set-light-enabled", g_fp_set_light_enabled, 2, 0, false,
                       "(fp-set-light-enabled socket enabled)");
    s7_define_function(scheme, "fp-set-rad-tint", g_fp_set_rad_tint, 1, 0, false,
                       "(fp-set-rad-tint enabled)");
    s7_define_function(scheme, "fp-set-shading", g_fp_set_shading, 1, 0, false,
                       "(fp-set-shading enabled)");
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
    tryCallSchemeProc1String(world.get<ScriptContext>().scheme, "prepare-first-person", "Player");
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
