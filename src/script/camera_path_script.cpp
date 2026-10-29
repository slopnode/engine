#include "script/camera_path_script.hpp"

#include "camera/camera_path_player.hpp"
#include "map/graph.hpp"
#include "script/script_scope.hpp"

#include <s7.h>

#include <string>

namespace slopengine {

namespace {

flecs::world* g_world = nullptr;

bool readOptionalBool(s7_scheme* sc, s7_pointer& cursor, bool fallback) {
    if (!s7_is_pair(cursor)) {
        return fallback;
    }
    const s7_pointer value = s7_car(cursor);
    cursor = s7_cdr(cursor);
    return s7_is_boolean(value) ? s7_boolean(sc, value) : fallback;
}

float readOptionalReal(s7_scheme* sc, s7_pointer& cursor, float fallback) {
    if (!s7_is_pair(cursor)) {
        return fallback;
    }
    const s7_pointer value = s7_car(cursor);
    cursor = s7_cdr(cursor);
    return s7_is_number(value) ? static_cast<float>(s7_number_to_real(sc, value)) : fallback;
}

const CameraPath* findPath(const char* id) {
    if (g_world == nullptr || id == nullptr || !g_world->has<MapGraphs>()) {
        return nullptr;
    }
    return g_world->get<MapGraphs>().document.findPath(id);
}

s7_pointer g_camera_path_play(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::MapControl) || g_world == nullptr) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "camera-path-play", 1, args, "string");
    }
    const std::string id = s7_string(s7_car(args));
    s7_pointer cursor = s7_cdr(args);
    const bool loop = readOptionalBool(sc, cursor, false);
    const float speed = readOptionalReal(sc, cursor, 1.0f);
    const float blendOut = readOptionalReal(sc, cursor, 0.0f);
    return s7_make_boolean(sc, startCameraPath(*g_world, id, loop, speed, blendOut));
}

s7_pointer g_camera_path_stop(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::MapControl) || g_world == nullptr) {
        return s7_f(sc);
    }
    s7_pointer cursor = args;
    const float blendOut = readOptionalReal(sc, cursor, 0.0f);
    const bool wasActive = cameraPathActive(*g_world);
    stopCameraPath(*g_world, blendOut);
    return s7_make_boolean(sc, wasActive);
}

s7_pointer g_camera_path_playing(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::ReadWorld) || g_world == nullptr) {
        return s7_f(sc);
    }
    return s7_make_boolean(sc, cameraPathActive(*g_world));
}

s7_pointer g_camera_path_progress(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::ReadWorld) || g_world == nullptr) {
        return s7_f(sc);
    }
    const float progress = cameraPathProgress(*g_world);
    if (progress < 0.0f) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(progress));
}

s7_pointer g_camera_path_duration(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "camera-path-duration", 1, args, "string");
    }
    const CameraPath* path = findPath(s7_string(s7_car(args)));
    if (path == nullptr) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(cameraPathDuration(*path)));
}

s7_pointer g_camera_path_sample(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (!s7_is_pair(args) || !s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "camera-path-sample", 1, args, "string");
    }
    s7_pointer rest = s7_cdr(args);
    if (!s7_is_pair(rest) || !s7_is_number(s7_car(rest))) {
        return s7_wrong_type_arg_error(sc, "camera-path-sample", 2, rest, "number");
    }
    const CameraPath* path = findPath(s7_string(s7_car(args)));
    if (path == nullptr) {
        return s7_f(sc);
    }
    const CameraPose pose =
        sampleCameraPath(*path, static_cast<float>(s7_number_to_real(sc, s7_car(rest))));
    return s7_list(
        sc,
        6,
        s7_make_real(sc, static_cast<double>(pose.position.x)),
        s7_make_real(sc, static_cast<double>(pose.position.y)),
        s7_make_real(sc, static_cast<double>(pose.position.z)),
        s7_make_real(sc, static_cast<double>(pose.yaw)),
        s7_make_real(sc, static_cast<double>(pose.pitch)),
        s7_make_real(sc, static_cast<double>(pose.fov)));
}

s7_pointer g_camera_paths(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::ReadWorld) || g_world == nullptr) {
        return s7_f(sc);
    }
    s7_pointer result = s7_nil(sc);
    if (!g_world->has<MapGraphs>()) {
        return result;
    }
    const GraphDocument& doc = g_world->get<MapGraphs>().document;
    for (auto it = doc.paths.rbegin(); it != doc.paths.rend(); ++it) {
        result = s7_cons(sc, s7_make_string(sc, it->id.c_str()), result);
    }
    return result;
}

} // namespace

void bindCameraPathApi(flecs::world& world, s7_scheme* scheme) {
    g_world = &world;
    if (scheme == nullptr) {
        return;
    }
    s7_define_function(
        scheme,
        "camera-path-play",
        g_camera_path_play,
        1,
        3,
        false,
        "(camera-path-play id [loop?] [speed] [blend-out])");
    s7_define_function(
        scheme, "camera-path-stop", g_camera_path_stop, 0, 1, false, "(camera-path-stop [blend-out])");
    s7_define_function(
        scheme, "camera-path-playing?", g_camera_path_playing, 0, 0, false, "(camera-path-playing?)");
    s7_define_function(
        scheme, "camera-path-progress", g_camera_path_progress, 0, 0, false, "(camera-path-progress)");
    s7_define_function(
        scheme, "camera-path-duration", g_camera_path_duration, 1, 0, false, "(camera-path-duration id)");
    s7_define_function(
        scheme, "camera-path-sample", g_camera_path_sample, 2, 0, false, "(camera-path-sample id t)");
    s7_define_function(scheme, "camera-paths", g_camera_paths, 0, 0, false, "(camera-paths)");
}

}
