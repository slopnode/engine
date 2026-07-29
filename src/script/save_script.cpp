#include "script/save_script.hpp"
#include "script/script_scope.hpp"

#include "assets/asset_store.hpp"
#include "camera/components.hpp"
#include "core/user_paths.hpp"
#include "game/game_state.hpp"
#include "map/bsp.hpp"
#include "physics/components.hpp"
#include "physics/physics_module.hpp"
#include "physics/physics_world.hpp"
#include "render/components.hpp"
#include "script/hook_registry.hpp"
#include "script/package_load_context.hpp"
#include "script/scheme_call.hpp"
#include "script/script_context.hpp"

#include <raymath.h>
#include <s7.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace slopengine {

namespace {

flecs::world* g_saveWorld = nullptr;
AssetStore* g_saveAssets = nullptr;
const std::unordered_map<std::string, std::string>* g_startupArgs = nullptr;

std::filesystem::path currentSaveRoot() {
    if (g_saveAssets == nullptr) {
        return {};
    }
    return buildSaveContextRoot(g_saveAssets->packages());
}

s7_pointer g_save_root(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::SaveIo)) {
        return s7_f(sc);
    }
    const std::filesystem::path root = currentSaveRoot();
    if (root.empty()) {
        return s7_f(sc);
    }
    return s7_make_string(sc, root.string().c_str());
}

bool requireRelativePath(
    s7_scheme* sc,
    s7_pointer arg,
    const char* proc,
    std::filesystem::path& out,
    s7_pointer* typeError) {
    if (!s7_is_string(arg)) {
        *typeError = s7_wrong_type_arg_error(sc, proc, 1, arg, "string");
        return false;
    }
    *typeError = nullptr;
    const std::filesystem::path root = currentSaveRoot();
    if (root.empty()) {
        return false;
    }
    if (!resolveSaveRelativePath(root, s7_string(arg), out)) {
        return false;
    }
    return true;
}

s7_pointer g_save_write(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::SaveIo)) {
        return s7_f(sc);
    }
    if (g_saveAssets == nullptr) {
        return s7_f(sc);
    }
    s7_pointer typeError = nullptr;
    std::filesystem::path absolute;
    if (!requireRelativePath(sc, s7_car(args), "save-write", absolute, &typeError)) {
        return typeError != nullptr ? typeError : s7_f(sc);
    }
    const s7_pointer form = s7_cadr(args);

    const std::filesystem::path root = currentSaveRoot();
    if (!ensureSaveMountSidecar(root, g_saveAssets->packages())) {
        return s7_f(sc);
    }

    std::error_code dirEc;
    std::filesystem::create_directories(absolute.parent_path(), dirEc);
    if (dirEc) {
        return s7_f(sc);
    }

    char* text = s7_object_to_c_string(sc, form);
    if (text == nullptr) {
        return s7_f(sc);
    }

    std::ofstream out(absolute, std::ios::binary | std::ios::trunc);
    if (!out) {
        return s7_f(sc);
    }
    out << text << '\n';
    return out ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_save_read(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::SaveIo)) {
        return s7_f(sc);
    }
    s7_pointer typeError = nullptr;
    std::filesystem::path absolute;
    if (!requireRelativePath(sc, s7_car(args), "save-read", absolute, &typeError)) {
        return typeError != nullptr ? typeError : s7_f(sc);
    }

    std::ifstream in(absolute, std::ios::binary);
    if (!in) {
        return s7_f(sc);
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string contents = buffer.str();
    if (contents.empty()) {
        return s7_f(sc);
    }

    const s7_pointer port = s7_open_input_string(sc, contents.c_str());
    if (!s7_is_input_port(sc, port)) {
        return s7_f(sc);
    }
    const s7_pointer form = s7_read(sc, port);
    s7_close_input_port(sc, port);
    if (s7_is_eq(form, s7_eof_object(sc))) {
        return s7_f(sc);
    }
    return form;
}

s7_pointer g_save_exists(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::SaveIo)) {
        return s7_f(sc);
    }
    s7_pointer typeError = nullptr;
    std::filesystem::path absolute;
    if (!requireRelativePath(sc, s7_car(args), "save-exists?", absolute, &typeError)) {
        return typeError != nullptr ? typeError : s7_f(sc);
    }
    std::error_code ec;
    return std::filesystem::is_regular_file(absolute, ec) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_save_delete(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::SaveIo)) {
        return s7_f(sc);
    }
    s7_pointer typeError = nullptr;
    std::filesystem::path absolute;
    if (!requireRelativePath(sc, s7_car(args), "save-delete", absolute, &typeError)) {
        return typeError != nullptr ? typeError : s7_f(sc);
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(absolute, ec)) {
        return s7_f(sc);
    }
    return std::filesystem::remove(absolute, ec) && !ec ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_save_timestamp(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::SaveIo)) {
        return s7_f(sc);
    }
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[32];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%04d%02d%02d_%02d%02d%02d",
        local.tm_year + 1900,
        local.tm_mon + 1,
        local.tm_mday,
        local.tm_hour,
        local.tm_min,
        local.tm_sec);
    return s7_make_string(sc, buffer);
}

s7_pointer g_request_map_load(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::MapControl)) {
        return s7_f(sc);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "request-map-load", 1, s7_car(args), "string");
    }
    const char* mapName = s7_string(s7_car(args));
    std::string reason = "fresh";
    const s7_pointer rest = s7_cdr(args);
    if (s7_is_pair(rest)) {
        if (!s7_is_string(s7_car(rest))) {
            return s7_wrong_type_arg_error(sc, "request-map-load", 2, s7_car(rest), "string");
        }
        reason = s7_string(s7_car(rest));
    }
    requestMapLoad(mapName, reason);
    return s7_t(sc);
}

s7_pointer g_list_maps(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_nil(sc);
    }
    if (g_saveAssets == nullptr) {
        return s7_nil(sc);
    }
    const std::vector<AssetStore::MapListEntry> maps = g_saveAssets->listMaps();
    s7_pointer list = s7_nil(sc);
    for (auto it = maps.rbegin(); it != maps.rend(); ++it) {
        const s7_pointer pair = s7_cons(
            sc,
            s7_make_string(sc, it->id.c_str()),
            s7_make_string(sc, it->name.c_str()));
        list = s7_cons(sc, pair, list);
    }
    return list;
}

s7_pointer g_current_map(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (g_saveWorld == nullptr || !g_saveWorld->has<CurrentMap>()) {
        return s7_f(sc);
    }
    return s7_make_string(sc, g_saveWorld->get<CurrentMap>().id.c_str());
}

s7_pointer g_player_pose(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (g_saveWorld == nullptr) {
        return s7_f(sc);
    }
    flecs::entity player = g_saveWorld->lookup("Player");
    if (!player.is_valid() || !player.has<FirstPersonController>()) {
        return s7_f(sc);
    }

    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    if (g_saveWorld->has<PhysicsContext>()) {
        PhysicsWorld* physics = g_saveWorld->get<PhysicsContext>().world;
        if (physics != nullptr && physics->hasPlayer()) {
            const JPH::RVec3 feet = physics->playerPosition();
            x = static_cast<float>(feet.GetX());
            y = static_cast<float>(feet.GetY());
            z = static_cast<float>(feet.GetZ());
        } else if (player.has<Lens>()) {
            const Lens& lens = player.get<Lens>();
            const float eye =
                player.has<CharacterMotor>() ? player.get<CharacterMotor>().eyeHeight : 1.7f;
            x = lens.camera.position.x;
            y = lens.camera.position.y - eye;
            z = lens.camera.position.z;
        } else {
            return s7_f(sc);
        }
    } else if (player.has<Lens>()) {
        const Lens& lens = player.get<Lens>();
        const float eye =
            player.has<CharacterMotor>() ? player.get<CharacterMotor>().eyeHeight : 1.7f;
        x = lens.camera.position.x;
        y = lens.camera.position.y - eye;
        z = lens.camera.position.z;
    } else {
        return s7_f(sc);
    }

    const FirstPersonController& controller = player.get<FirstPersonController>();
    return s7_list(
        sc,
        5,
        s7_make_real(sc, static_cast<double>(x)),
        s7_make_real(sc, static_cast<double>(y)),
        s7_make_real(sc, static_cast<double>(z)),
        s7_make_real(sc, static_cast<double>(controller.yaw)),
        s7_make_real(sc, static_cast<double>(controller.pitch)));
}

s7_pointer g_player_set_pose(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::MapControl)) {
        return s7_f(sc);
    }
    if (g_saveWorld == nullptr) {
        return s7_f(sc);
    }
    flecs::entity player = g_saveWorld->lookup("Player");
    if (!player.is_valid() || !player.has<FirstPersonController>() || !player.has<Lens>()) {
        return s7_f(sc);
    }

    s7_pointer rest = args;
    auto readReal = [&](int index, float& out) -> bool {
        if (!s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
            s7_wrong_type_arg_error(sc, "player-set-pose", index, rest, "number");
            return false;
        }
        out = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
        rest = s7_cdr(rest);
        return true;
    };

    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
    if (!readReal(1, x) || !readReal(2, y) || !readReal(3, z) || !readReal(4, yaw)
        || !readReal(5, pitch)) {
        return s7_f(sc);
    }

    FirstPersonController& controller = player.get_mut<FirstPersonController>();
    controller.yaw = yaw;
    controller.pitch = pitch;

    const float eyeHeight =
        player.has<CharacterMotor>() ? player.get<CharacterMotor>().eyeHeight : controller.eyeHeight;

    if (g_saveWorld->has<PhysicsContext>()) {
        PhysicsWorld* physics = g_saveWorld->get_mut<PhysicsContext>().world;
        if (physics != nullptr && physics->hasPlayer()) {
            physics->setPlayerPosition(x, y, z);
        }
    }

    Lens& lens = player.get_mut<Lens>();
    lens.camera.position = {x, y + eyeHeight, z};
    const float cosPitch = std::cos(pitch);
    const Vector3 forward = {
        std::sin(yaw) * cosPitch,
        std::sin(pitch),
        std::cos(yaw) * cosPitch,
    };
    lens.camera.target = Vector3Add(lens.camera.position, forward);
    lens.camera.up = {0.0f, 1.0f, 0.0f};
    return s7_t(sc);
}

s7_pointer g_player_eye_height(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::MapControl)) {
        return s7_f(sc);
    }
    if (g_saveWorld == nullptr) {
        return s7_f(sc);
    }
    flecs::entity player = g_saveWorld->lookup("Player");
    if (!player.is_valid()) {
        return s7_f(sc);
    }
    if (player.has<CharacterMotor>()) {
        return s7_make_real(sc, static_cast<double>(player.get<CharacterMotor>().eyeHeight));
    }
    if (player.has<FirstPersonController>()) {
        return s7_make_real(sc, static_cast<double>(player.get<FirstPersonController>().eyeHeight));
    }
    return s7_f(sc);
}

s7_pointer g_player_set_eye_height(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::MapControl)) {
        return s7_f(sc);
    }
    if (g_saveWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_number(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "player-set-eye-height", 1, args, "number");
    }
    flecs::entity player = g_saveWorld->lookup("Player");
    if (!player.is_valid() || !player.has<FirstPersonController>()) {
        return s7_f(sc);
    }

    const float h = static_cast<float>(s7_number_to_real(sc, s7_car(args)));
    FirstPersonController& controller = player.get_mut<FirstPersonController>();
    controller.eyeHeight = h;
    if (player.has<CharacterMotor>()) {
        player.get_mut<CharacterMotor>().eyeHeight = h;
    }
    if (player.has<Lens>()) {
        Lens& lens = player.get_mut<Lens>();
        if (g_saveWorld->has<PhysicsContext>()) {
            PhysicsWorld* physics = g_saveWorld->get_mut<PhysicsContext>().world;
            if (physics != nullptr && physics->hasPlayer()) {
                const JPH::RVec3 feet = physics->playerPosition();
                lens.camera.position.x = static_cast<float>(feet.GetX());
                lens.camera.position.y = static_cast<float>(feet.GetY()) + h;
                lens.camera.position.z = static_cast<float>(feet.GetZ());
            } else {
                lens.camera.position.y = h;
            }
        } else {
            lens.camera.position.y = h;
        }
        const float cosPitch = std::cos(controller.pitch);
        const Vector3 forward = {
            std::sin(controller.yaw) * cosPitch,
            std::sin(controller.pitch),
            std::cos(controller.yaw) * cosPitch,
        };
        lens.camera.target = Vector3Add(lens.camera.position, forward);
    }
    return s7_t(sc);
}

s7_pointer g_player_set_control(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::MapControl)) {
        return s7_f(sc);
    }
    if (g_saveWorld == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "player-set-control", 1, args, "boolean");
    }
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest)) {
        return s7_wrong_type_arg_error(sc, "player-set-control", 2, rest, "boolean");
    }
    flecs::entity player = g_saveWorld->lookup("Player");
    if (!player.is_valid() || !player.has<FirstPersonController>()) {
        return s7_f(sc);
    }
    FirstPersonController& controller = player.get_mut<FirstPersonController>();
    controller.allowMove = s7_boolean(sc, s7_car(args));
    controller.allowLook = s7_boolean(sc, s7_car(rest));
    if (!controller.allowMove && player.has<CharacterMotor>()) {
        CharacterMotor& motor = player.get_mut<CharacterMotor>();
        motor.wishX = 0.0f;
        motor.wishZ = 0.0f;
    }
    return s7_t(sc);
}

bool parseSaveStemDisplay(std::string_view stem, std::string& displayOut) {
    const std::size_t under = stem.rfind('_');
    if (under == std::string_view::npos || under == 0 || under + 1 >= stem.size()) {
        displayOut = std::string(stem);
        return false;
    }
    const std::string_view timePart = stem.substr(under + 1);
    if (timePart.size() != 15 || timePart[8] != '_') {
        displayOut = std::string(stem);
        return false;
    }
    for (std::size_t i = 0; i < timePart.size(); ++i) {
        if (i == 8) {
            continue;
        }
        if (timePart[i] < '0' || timePart[i] > '9') {
            displayOut = std::string(stem);
            return false;
        }
    }
    std::string name(stem.substr(0, under));
    for (char& c : name) {
        if (c == '_') {
            c = ' ';
        }
    }
    displayOut = name + " — " + std::string(timePart.substr(0, 4)) + "-"
        + std::string(timePart.substr(4, 2)) + "-" + std::string(timePart.substr(6, 2)) + " "
        + std::string(timePart.substr(9, 2)) + ":" + std::string(timePart.substr(11, 2)) + ":"
        + std::string(timePart.substr(13, 2));
    return true;
}

s7_pointer g_save_list(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::SaveIo)) {
        return s7_f(sc);
    }
    if (g_saveAssets == nullptr) {
        return s7_nil(sc);
    }
    if (!s7_is_string(s7_car(args)) || !s7_is_string(s7_cadr(args))) {
        return s7_wrong_type_arg_error(sc, "save-list", 1, s7_car(args), "dir suffix");
    }
    const std::string relDir = s7_string(s7_car(args));
    const std::string suffix = s7_string(s7_cadr(args));
    const std::filesystem::path root = currentSaveRoot();
    if (root.empty()) {
        return s7_nil(sc);
    }
    std::filesystem::path dirAbsolute;
    if (!resolveSaveRelativePath(root, relDir, dirAbsolute)) {
        return s7_nil(sc);
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(dirAbsolute, ec)) {
        return s7_nil(sc);
    }

    struct Entry {
        std::string relativePath;
        std::string display;
    };
    std::vector<Entry> entries;
    for (const auto& file : std::filesystem::directory_iterator(dirAbsolute, ec)) {
        if (ec || !file.is_regular_file(ec)) {
            continue;
        }
        const std::filesystem::path path = file.path();
        const std::string filename = path.filename().string();
        if (filename.size() < suffix.size()
            || filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) != 0) {
            continue;
        }
        Entry entry{};
        entry.relativePath = (std::filesystem::path(relDir) / path.filename()).generic_string();
        parseSaveStemDisplay(path.stem().string(), entry.display);
        entries.push_back(std::move(entry));
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.relativePath > b.relativePath;
    });

    s7_pointer list = s7_nil(sc);
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        const s7_pointer pair = s7_cons(
            sc,
            s7_make_string(sc, it->relativePath.c_str()),
            s7_make_string(sc, it->display.c_str()));
        list = s7_cons(sc, pair, list);
    }
    return list;
}

s7_pointer g_package_load_data(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::PackageLoad)) {
        return s7_f(sc);
    }
    if (g_saveAssets == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "package-load-data", 1, s7_car(args), "string");
    }
    if (!s7_is_string(s7_cadr(args))) {
        return s7_wrong_type_arg_error(sc, "package-load-data", 2, s7_cadr(args), "string");
    }
    return g_saveAssets->loadDataFromPackage(
               sc, s7_string(s7_car(args)), s7_string(s7_cadr(args)))
        ? s7_t(sc)
        : s7_f(sc);
}

s7_pointer g_package_load_script(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::PackageLoad)) {
        return s7_f(sc);
    }
    if (g_saveAssets == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "package-load-script", 1, s7_car(args), "string");
    }
    if (!s7_is_string(s7_cadr(args))) {
        return s7_wrong_type_arg_error(sc, "package-load-script", 2, s7_cadr(args), "string");
    }
    return g_saveAssets->loadScriptFromPackage(
               sc, s7_string(s7_car(args)), s7_string(s7_cadr(args)))
        ? s7_t(sc)
        : s7_f(sc);
}

s7_pointer g_current_package_id(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::PackageLoad)) {
        return s7_f(sc);
    }
    const std::string_view id = currentPackageLoadId();
    if (id.empty()) {
        return s7_f(sc);
    }
    return s7_make_string(sc, std::string(id).c_str());
}

s7_pointer g_package_mounted_p(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::PackageLoad)) {
        return s7_f(sc);
    }
    if (g_saveAssets == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "package-mounted?", 1, s7_car(args), "string");
    }
    return g_saveAssets->hasPackageId(s7_string(s7_car(args))) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_startup_arg(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::StartupQuery)) {
        return s7_f(sc);
    }
    if (g_startupArgs == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "startup-arg", 1, s7_car(args), "string");
    }
    const auto it = g_startupArgs->find(s7_string(s7_car(args)));
    if (it == g_startupArgs->end()) {
        return s7_f(sc);
    }
    return s7_make_string(sc, it->second.c_str());
}

s7_pointer g_startup_args(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::StartupQuery)) {
        return s7_f(sc);
    }
    if (g_startupArgs == nullptr) {
        return s7_nil(sc);
    }
    s7_pointer alist = s7_nil(sc);
    for (const auto& [key, value] : *g_startupArgs) {
        const s7_pointer pair =
            s7_cons(sc, s7_make_string(sc, key.c_str()), s7_make_string(sc, value.c_str()));
        alist = s7_cons(sc, pair, alist);
    }
    return alist;
}

} // namespace

void bindSaveApi(flecs::world& world, AssetStore& assets, s7_scheme* scheme) {
    g_saveWorld = &world;
    g_saveAssets = &assets;
    if (scheme == nullptr) {
        return;
    }

    s7_define_function(scheme, "save-root", g_save_root, 0, 0, false, "(save-root)");
    s7_define_function(scheme, "save-write", g_save_write, 2, 0, false, "(save-write rel form)");
    s7_define_function(scheme, "save-read", g_save_read, 1, 0, false, "(save-read rel)");
    s7_define_function(scheme, "save-exists?", g_save_exists, 1, 0, false, "(save-exists? rel)");
    s7_define_function(scheme, "save-delete", g_save_delete, 1, 0, false, "(save-delete rel)");
    s7_define_function(
        scheme, "save-timestamp", g_save_timestamp, 0, 0, false, "(save-timestamp)");
    s7_define_function(scheme, "save-list", g_save_list, 2, 0, false, "(save-list dir suffix)");
    s7_define_function(
        scheme,
        "package-load-data",
        g_package_load_data,
        2,
        0,
        false,
        "(package-load-data package-id path)");
    s7_define_function(
        scheme,
        "package-load-script",
        g_package_load_script,
        2,
        0,
        false,
        "(package-load-script package-id path)");
    s7_define_function(
        scheme, "current-package-id", g_current_package_id, 0, 0, false, "(current-package-id)");
    s7_define_function(
        scheme, "package-mounted?", g_package_mounted_p, 1, 0, false, "(package-mounted? package-id)");
    s7_define_function(
        scheme,
        "request-map-load",
        g_request_map_load,
        1,
        1,
        false,
        "(request-map-load name [reason])");
    s7_define_function(scheme, "list-maps", g_list_maps, 0, 0, false, "(list-maps)");
    s7_define_function(scheme, "current-map", g_current_map, 0, 0, false, "(current-map)");
    s7_define_function(scheme, "player-pose", g_player_pose, 0, 0, false, "(player-pose)");
    s7_define_function(
        scheme, "player-set-pose", g_player_set_pose, 5, 0, false, "(player-set-pose x y z yaw pitch)");
    s7_define_function(
        scheme, "player-eye-height", g_player_eye_height, 0, 0, false, "(player-eye-height)");
    s7_define_function(
        scheme,
        "player-set-eye-height",
        g_player_set_eye_height,
        1,
        0,
        false,
        "(player-set-eye-height h)");
    s7_define_function(
        scheme,
        "player-set-control",
        g_player_set_control,
        2,
        0,
        false,
        "(player-set-control move? look?)");
}

void bindStartupApi(s7_scheme* scheme, const std::unordered_map<std::string, std::string>& args) {
    g_startupArgs = &args;
    if (scheme == nullptr) {
        return;
    }
    s7_define_function(scheme, "startup-arg", g_startup_arg, 1, 0, false, "(startup-arg name)");
    s7_define_function(scheme, "startup-args", g_startup_args, 0, 0, false, "(startup-args)");
}

void callOnMapReady(flecs::world& world, std::string_view mapId, std::string_view reason) {
    if (!world.has<ScriptContext>() || world.get<ScriptContext>().scheme == nullptr) {
        return;
    }
    const std::string mapStr(mapId);
    const std::string reasonStr = reason.empty() ? std::string("fresh") : std::string(reason);
    callHook2String(
        world.get<ScriptContext>().scheme,
        "on-map-ready",
        mapStr,
        reasonStr,
        ScriptScope::World);
}

void callOnStartup(flecs::world& world) {
    if (!world.has<ScriptContext>() || world.get<ScriptContext>().scheme == nullptr) {
        return;
    }
    callHook(world.get<ScriptContext>().scheme, "on-startup", ScriptScope::Startup);
}

}
