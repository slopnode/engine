#include "script/save_script.hpp"
#include "script/s7_read.hpp"
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
    const std::filesystem::path root = currentSaveRoot();
    if (root.empty()) {
        return s7_f(sc);
    }
    return s7_make_string(sc, root.string().c_str());
}

bool requireRelativePath(ArgReader& in, std::filesystem::path& out) {
    const char* rel = in.string("string");
    const std::filesystem::path root = currentSaveRoot();
    if (root.empty()) {
        return false;
    }
    return resolveSaveRelativePath(root, rel, out);
}

s7_pointer g_save_write(s7_scheme* sc, s7_pointer args) {
    if (g_saveAssets == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "save-write", args);
    std::filesystem::path absolute;
    if (!requireRelativePath(in, absolute)) {
        return s7_f(sc);
    }
    const s7_pointer form = in.any("form");

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
    ArgReader in(sc, "save-read", args);
    std::filesystem::path absolute;
    if (!requireRelativePath(in, absolute)) {
        return s7_f(sc);
    }

    std::ifstream file(absolute, std::ios::binary);
    if (!file) {
        return s7_f(sc);
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
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
    ArgReader in(sc, "save-exists?", args);
    std::filesystem::path absolute;
    if (!requireRelativePath(in, absolute)) {
        return s7_f(sc);
    }
    std::error_code ec;
    return s7_make_boolean(sc, std::filesystem::is_regular_file(absolute, ec));
}

s7_pointer g_save_delete(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "save-delete", args);
    std::filesystem::path absolute;
    if (!requireRelativePath(in, absolute)) {
        return s7_f(sc);
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(absolute, ec)) {
        return s7_f(sc);
    }
    return s7_make_boolean(sc, std::filesystem::remove(absolute, ec) && !ec);
}

s7_pointer g_save_timestamp(s7_scheme* sc, s7_pointer) {
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
    ArgReader in(sc, "request-map-load", args);
    const char* mapName = in.string("string");
    std::string reason = "fresh";
    if (in.more()) {
        reason = in.string("string");
    }
    requestMapLoad(mapName, reason);
    return s7_t(sc);
}

s7_pointer g_return_to_menu(s7_scheme* sc, s7_pointer) {
    requestMapLoad("", "menu");
    return s7_t(sc);
}

s7_pointer g_list_maps(s7_scheme* sc, s7_pointer) {
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
    if (g_saveWorld == nullptr || !g_saveWorld->has<CurrentMap>()) {
        return s7_f(sc);
    }
    return s7_make_string(sc, g_saveWorld->get<CurrentMap>().id.c_str());
}

constexpr SchemeBinding kMapBindings[] = {
    {"request-map-load", g_request_map_load, 1, 1, false, "(request-map-load name [reason])", ScriptCap::MapControl},
    {"return-to-menu", g_return_to_menu, 0, 0, false, "(return-to-menu)", ScriptCap::MapControl},
    {"list-maps", g_list_maps, 0, 0, false, "(list-maps)", ScriptCap::ReadWorld},
    {"current-map", g_current_map, 0, 0, false, "(current-map)", ScriptCap::ReadWorld},
};

s7_pointer g_player_pose(s7_scheme* sc, s7_pointer) {
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
    if (g_saveWorld == nullptr) {
        return s7_f(sc);
    }
    flecs::entity player = g_saveWorld->lookup("Player");
    if (!player.is_valid() || !player.has<FirstPersonController>() || !player.has<Lens>()) {
        return s7_f(sc);
    }

    ArgReader in(sc, "player-set-pose", args);
    const Vector3 at = in.vec3("number");
    const float yaw = in.realf("number");
    const float pitch = in.realf("number");
    const float x = at.x;
    const float y = at.y;
    const float z = at.z;

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
    if (g_saveWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "player-set-eye-height", args);
    const float h = in.realf("number");
    flecs::entity player = g_saveWorld->lookup("Player");
    if (!player.is_valid() || !player.has<FirstPersonController>()) {
        return s7_f(sc);
    }

    FirstPersonController& controller = player.get_mut<FirstPersonController>();
    controller.eyeHeight = h;
    bool motorResized = false;
    if (player.has<CharacterMotor>()) {
        CharacterMotor& motor = player.get_mut<CharacterMotor>();
        motor.eyeHeight = h;
        const float radius = motor.radius > 0.0f ? motor.radius : 0.3f;
        const float bodyHeight = std::max(motor.height, h - 2.0f * radius);
        motorResized = bodyHeight > motor.height + 1.0e-4f;
        motor.height = bodyHeight;
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
                if (motorResized && player.has<CharacterMotor>()) {
                    physics->createPlayerCharacter(
                        static_cast<float>(feet.GetX()),
                        static_cast<float>(feet.GetY()),
                        static_cast<float>(feet.GetZ()),
                        player.get<CharacterMotor>());
                }
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

s7_pointer g_player_collider(s7_scheme* sc, s7_pointer) {
    if (g_saveWorld == nullptr) {
        return s7_f(sc);
    }
    flecs::entity player = g_saveWorld->lookup("Player");
    if (!player.is_valid() || !player.has<CharacterMotor>()) {
        return s7_f(sc);
    }
    const CharacterMotor& motor = player.get<CharacterMotor>();
    return s7_list(
        sc,
        2,
        s7_make_real(sc, static_cast<double>(motor.radius)),
        s7_make_real(sc, static_cast<double>(motor.height)));
}

s7_pointer g_player_set_collider(s7_scheme* sc, s7_pointer args) {
    if (g_saveWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "player-set-collider", args);
    const float radius = in.realf("number");
    const float height = in.realf("number");
    flecs::entity player = g_saveWorld->lookup("Player");
    if (!player.is_valid() || !player.has<CharacterMotor>()) {
        return s7_f(sc);
    }

    if (!(radius > 0.0f) || !(height > 0.0f)) {
        return s7_f(sc);
    }

    if (!g_saveWorld->has<PhysicsContext>()) {
        return s7_f(sc);
    }
    PhysicsWorld* physics = g_saveWorld->get_mut<PhysicsContext>().world;
    if (physics == nullptr || !physics->hasPlayer()) {
        return s7_f(sc);
    }

    CharacterMotor resized = player.get<CharacterMotor>();
    resized.radius = radius;
    resized.height = height;
    if (!physics->resizePlayerCharacter(resized)) {
        return s7_f(sc);
    }

    CharacterMotor& motor = player.get_mut<CharacterMotor>();
    motor.radius = radius;
    motor.height = height;
    return s7_t(sc);
}

s7_pointer g_player_set_control(s7_scheme* sc, s7_pointer args) {
    if (g_saveWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "player-set-control", args);
    const bool allowMove = in.truthy("boolean");
    const bool allowLook = in.truthy("boolean");
    flecs::entity player = g_saveWorld->lookup("Player");
    if (!player.is_valid() || !player.has<FirstPersonController>()) {
        return s7_f(sc);
    }
    FirstPersonController& controller = player.get_mut<FirstPersonController>();
    controller.allowMove = allowMove;
    controller.allowLook = allowLook;
    if (!controller.allowMove && player.has<CharacterMotor>()) {
        CharacterMotor& motor = player.get_mut<CharacterMotor>();
        motor.wishX = 0.0f;
        motor.wishZ = 0.0f;
    }
    return s7_t(sc);
}

s7_pointer g_player_set_pitch_locked(s7_scheme* sc, s7_pointer args) {
    if (g_saveWorld == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "player-set-pitch-locked", args);
    const bool locked = in.truthy("boolean");
    flecs::entity player = g_saveWorld->lookup("Player");
    if (!player.is_valid() || !player.has<FirstPersonController>()) {
        return s7_f(sc);
    }
    FirstPersonController& controller = player.get_mut<FirstPersonController>();
    controller.allowPitch = !locked;
    if (locked) {
        controller.pitch = 0.0f;
    }
    return s7_t(sc);
}

constexpr SchemeBinding kPlayerBindings[] = {
    {"player-pose", g_player_pose, 0, 0, false, "(player-pose)", ScriptCap::ReadWorld},
    {"player-set-pose", g_player_set_pose, 5, 0, false, "(player-set-pose x y z yaw pitch)", ScriptCap::MapControl},
    {"player-eye-height", g_player_eye_height, 0, 0, false, "(player-eye-height)", ScriptCap::MapControl},
    {"player-set-eye-height", g_player_set_eye_height, 1, 0, false, "(player-set-eye-height h)", ScriptCap::MapControl},
    {"player-collider", g_player_collider, 0, 0, false, "(player-collider) -> (radius height)", ScriptCap::ReadWorld},
    {"player-set-collider", g_player_set_collider, 2, 0, false, "(player-set-collider radius height)", ScriptCap::MapControl},
    {"player-set-control", g_player_set_control, 2, 0, false, "(player-set-control move? look?)", ScriptCap::MapControl},
    {"player-set-pitch-locked", g_player_set_pitch_locked, 1, 0, false, "(player-set-pitch-locked locked?)", ScriptCap::MapControl},
};

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
    if (g_saveAssets == nullptr) {
        return s7_nil(sc);
    }
    ArgReader in(sc, "save-list", args);
    const std::string relDir = in.string("dir suffix");
    const std::string suffix = in.string("dir suffix");
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

constexpr SchemeBinding kSaveFileBindings[] = {
    {"save-root", g_save_root, 0, 0, false, "(save-root)", ScriptCap::SaveIo},
    {"save-write", g_save_write, 2, 0, false, "(save-write rel form)", ScriptCap::SaveIo},
    {"save-read", g_save_read, 1, 0, false, "(save-read rel)", ScriptCap::SaveIo},
    {"save-exists?", g_save_exists, 1, 0, false, "(save-exists? rel)", ScriptCap::SaveIo},
    {"save-delete", g_save_delete, 1, 0, false, "(save-delete rel)", ScriptCap::SaveIo},
    {"save-timestamp", g_save_timestamp, 0, 0, false, "(save-timestamp)", ScriptCap::SaveIo},
    {"save-list", g_save_list, 2, 0, false, "(save-list dir suffix)", ScriptCap::SaveIo},
};

s7_pointer g_package_load_data(s7_scheme* sc, s7_pointer args) {
    if (g_saveAssets == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "package-load-data", args);
    const char* packageId = in.string("string");
    const char* path = in.string("string");
    return s7_make_boolean(sc, g_saveAssets->loadDataFromPackage(sc, packageId, path));
}

s7_pointer g_package_load_script(s7_scheme* sc, s7_pointer args) {
    if (g_saveAssets == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "package-load-script", args);
    const char* packageId = in.string("string");
    const char* path = in.string("string");
    return s7_make_boolean(sc, g_saveAssets->loadScriptFromPackage(sc, packageId, path));
}

s7_pointer g_current_package_id(s7_scheme* sc, s7_pointer) {
    const std::string_view id = currentPackageLoadId();
    if (id.empty()) {
        return s7_f(sc);
    }
    return s7_make_string(sc, std::string(id).c_str());
}

s7_pointer g_package_mounted_p(s7_scheme* sc, s7_pointer args) {
    if (g_saveAssets == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "package-mounted?", args);
    return s7_make_boolean(sc, g_saveAssets->hasPackageId(in.string("string")));
}

constexpr SchemeBinding kPackageBindings[] = {
    {"package-load-data", g_package_load_data, 2, 0, false, "(package-load-data package-id path)", ScriptCap::PackageLoad},
    {"package-load-script", g_package_load_script, 2, 0, false, "(package-load-script package-id path)", ScriptCap::PackageLoad},
    {"current-package-id", g_current_package_id, 0, 0, false, "(current-package-id)", ScriptCap::PackageLoad},
    {"package-mounted?", g_package_mounted_p, 1, 0, false, "(package-mounted? package-id)", ScriptCap::PackageLoad},
};

s7_pointer g_startup_arg(s7_scheme* sc, s7_pointer args) {
    if (g_startupArgs == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "startup-arg", args);
    const auto it = g_startupArgs->find(in.string("string"));
    if (it == g_startupArgs->end()) {
        return s7_f(sc);
    }
    return s7_make_string(sc, it->second.c_str());
}

s7_pointer g_startup_args(s7_scheme* sc, s7_pointer) {
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

constexpr SchemeBinding kStartupBindings[] = {
    {"startup-arg", g_startup_arg, 1, 0, false, "(startup-arg name)", ScriptCap::StartupQuery},
    {"startup-args", g_startup_args, 0, 0, false, "(startup-args)", ScriptCap::StartupQuery},
};

} // namespace

void bindPackageApi(AssetStore& assets, s7_scheme* scheme) {
    g_saveAssets = &assets;
    if (scheme == nullptr) {
        return;
    }

    registerBindings(scheme, kPackageBindings);
}

void bindSaveApi(flecs::world& world, AssetStore& assets, s7_scheme* scheme) {
    g_saveWorld = &world;
    g_saveAssets = &assets;
    if (scheme == nullptr) {
        return;
    }

    registerBindings(scheme, kSaveFileBindings);
    registerBindings(scheme, kMapBindings);
    registerBindings(scheme, kPlayerBindings);
}

void bindStartupApi(s7_scheme* scheme, const std::unordered_map<std::string, std::string>& args) {
    g_startupArgs = &args;
    if (scheme == nullptr) {
        return;
    }
    registerBindings(scheme, kStartupBindings);
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
