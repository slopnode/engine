#include "script/camera_path_script.hpp"

#include "camera/camera_path_player.hpp"
#include "map/graph.hpp"
#include "script/s7_read.hpp"
#include "script/script_scope.hpp"

#include <s7.h>

#include <string>

namespace slopengine {

namespace {

flecs::world* g_world = nullptr;

const CameraPath* findPath(const char* id) {
    if (g_world == nullptr || id == nullptr || !g_world->has<MapGraphs>()) {
        return nullptr;
    }
    return g_world->get<MapGraphs>().document.findPath(id);
}

s7_pointer g_camera_path_play(s7_scheme* sc, s7_pointer args) {
    if (g_world == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "camera-path-play", args);
    const std::string id = in.string("string");
    const bool loop = in.optBoolean(false);
    const float speed = in.optRealf(1.0f);
    const float blendOut = in.optRealf(0.0f);
    return s7_make_boolean(sc, startCameraPath(*g_world, id, loop, speed, blendOut));
}

s7_pointer g_camera_path_stop(s7_scheme* sc, s7_pointer args) {
    if (g_world == nullptr) {
        return s7_f(sc);
    }
    ArgReader in(sc, "camera-path-stop", args);
    const float blendOut = in.optRealf(0.0f);
    const bool wasActive = cameraPathActive(*g_world);
    stopCameraPath(*g_world, blendOut);
    return s7_make_boolean(sc, wasActive);
}

s7_pointer g_camera_path_playing(s7_scheme* sc, s7_pointer) {
    if (g_world == nullptr) {
        return s7_f(sc);
    }
    return s7_make_boolean(sc, cameraPathActive(*g_world));
}

s7_pointer g_camera_path_progress(s7_scheme* sc, s7_pointer) {
    if (g_world == nullptr) {
        return s7_f(sc);
    }
    const float progress = cameraPathProgress(*g_world);
    if (progress < 0.0f) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(progress));
}

s7_pointer g_camera_path_duration(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "camera-path-duration", args);
    const CameraPath* path = findPath(in.string("string"));
    if (path == nullptr) {
        return s7_f(sc);
    }
    return s7_make_real(sc, static_cast<double>(cameraPathDuration(*path)));
}

s7_pointer g_camera_path_sample(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "camera-path-sample", args);
    const char* id = in.string("string");
    const float t = in.realf("number");
    const CameraPath* path = findPath(id);
    if (path == nullptr) {
        return s7_f(sc);
    }
    const CameraPose pose = sampleCameraPath(*path, t);
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
    if (g_world == nullptr) {
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

constexpr SchemeBinding kCameraPathBindings[] = {
    {"camera-path-play", g_camera_path_play, 1, 3, false, "(camera-path-play id [loop?] [speed] [blend-out])", ScriptCap::MapControl},
    {"camera-path-stop", g_camera_path_stop, 0, 1, false, "(camera-path-stop [blend-out])", ScriptCap::MapControl},
    {"camera-path-playing?", g_camera_path_playing, 0, 0, false, "(camera-path-playing?)", ScriptCap::ReadWorld},
    {"camera-path-progress", g_camera_path_progress, 0, 0, false, "(camera-path-progress)", ScriptCap::ReadWorld},
    {"camera-path-duration", g_camera_path_duration, 1, 0, false, "(camera-path-duration id)", ScriptCap::ReadWorld},
    {"camera-path-sample", g_camera_path_sample, 2, 0, false, "(camera-path-sample id t)", ScriptCap::ReadWorld},
    {"camera-paths", g_camera_paths, 0, 0, false, "(camera-paths)", ScriptCap::ReadWorld},
};

} // namespace

void bindCameraPathApi(flecs::world& world, s7_scheme* scheme) {
    g_world = &world;
    if (scheme == nullptr) {
        return;
    }
    registerBindings(scheme, kCameraPathBindings);
}

}
