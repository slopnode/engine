#include "camera/camera_path_player.hpp"

#include "game/game_state.hpp"
#include "map/graph.hpp"
#include "script/hook_registry.hpp"
#include "script/script_context.hpp"

#include <raymath.h>

#include <algorithm>
#include <cmath>

namespace slopengine {

namespace {

const CameraPath* findPath(const flecs::world& world, const std::string& id) {
    if (!world.has<MapGraphs>()) {
        return nullptr;
    }
    return world.get<MapGraphs>().document.findPath(id);
}

Camera3D cameraFromPose(const CameraPose& pose, const Camera3D& base) {
    Camera3D camera = base;
    camera.position = pose.position;
    camera.target = Vector3Add(pose.position, cameraPoseForward(pose));
    camera.up = {0.0f, 1.0f, 0.0f};
    if (pose.fov > 0.0f) {
        camera.fovy = pose.fov;
    }
    return camera;
}

void finishPath(flecs::world& world, CameraPathPlayer& player) {
    const std::string endedId = player.pathId;
    if (player.blendOut > 0.0f && player.havePose) {
        player.blendFrom = player.pose;
        player.blendDuration = player.blendOut;
        player.blendRemaining = player.blendOut;
    }
    player.active = false;
    if (world.has<ScriptContext>() && world.get<ScriptContext>().scheme != nullptr) {
        callHook1String(
            world.get<ScriptContext>().scheme,
            "on-camera-path-end",
            endedId,
            ScriptScope::World);
    }
}

} // namespace

void registerCameraPathPlayer(flecs::world& world) {
    world.component<CameraPathPlayer>();

    world.system("CameraPathController")
        .kind(flecs::PreUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            if (!world.has<CameraPathPlayer>()) {
                return;
            }
            if (isSimulationPaused(world)) {
                return;
            }
            CameraPathPlayer& player = world.get_mut<CameraPathPlayer>();
            const float dt = GetFrameTime();
            if (player.blendRemaining > 0.0f) {
                player.blendRemaining = std::max(0.0f, player.blendRemaining - dt);
            }
            if (!player.active) {
                return;
            }
            const CameraPath* path = findPath(world, player.pathId);
            if (path == nullptr) {
                player.active = false;
                return;
            }
            const float duration = cameraPathDuration(*path);
            player.time += dt * player.speed;
            bool ended = false;
            if (duration <= 0.0f) {
                player.time = 0.0f;
                ended = true;
            } else if (player.time >= duration || player.time < 0.0f) {
                if (player.loop) {
                    player.time = std::fmod(player.time, duration);
                    if (player.time < 0.0f) {
                        player.time += duration;
                    }
                } else {
                    player.time = std::clamp(player.time, 0.0f, duration);
                    ended = true;
                }
            }
            player.pose = sampleCameraPath(*path, player.time);
            player.havePose = true;
            if (ended) {
                finishPath(world, player);
            }
        });
}

bool startCameraPath(
    flecs::world& world,
    const std::string& pathId,
    bool loop,
    float speed,
    float blendOut) {
    const CameraPath* path = findPath(world, pathId);
    if (path == nullptr || path->knots.empty()) {
        return false;
    }
    if (!world.has<CameraPathPlayer>()) {
        world.set<CameraPathPlayer>(CameraPathPlayer{});
    }
    CameraPathPlayer& player = world.get_mut<CameraPathPlayer>();
    player.active = true;
    player.pathId = pathId;
    player.loop = loop;
    player.speed = speed == 0.0f ? 1.0f : speed;
    player.blendOut = std::max(0.0f, blendOut);
    player.time = player.speed < 0.0f ? cameraPathDuration(*path) : 0.0f;
    player.pose = sampleCameraPath(*path, player.time);
    player.havePose = true;
    player.blendRemaining = 0.0f;
    player.blendDuration = 0.0f;
    return true;
}

void stopCameraPath(flecs::world& world, float blendOut) {
    if (!world.has<CameraPathPlayer>()) {
        return;
    }
    CameraPathPlayer& player = world.get_mut<CameraPathPlayer>();
    if (!player.active) {
        return;
    }
    if (blendOut > 0.0f && player.havePose) {
        player.blendFrom = player.pose;
        player.blendDuration = blendOut;
        player.blendRemaining = blendOut;
    }
    player.active = false;
}

void resetCameraPathPlayer(flecs::world& world) {
    if (world.has<CameraPathPlayer>()) {
        world.set<CameraPathPlayer>(CameraPathPlayer{});
    }
}

bool cameraPathActive(const flecs::world& world) {
    return world.has<CameraPathPlayer>() && world.get<CameraPathPlayer>().active;
}

bool cameraPathOverridesView(const flecs::world& world) {
    if (!world.has<CameraPathPlayer>()) {
        return false;
    }
    const CameraPathPlayer& player = world.get<CameraPathPlayer>();
    return (player.active && player.havePose) || player.blendRemaining > 0.0f;
}

float cameraPathProgress(const flecs::world& world) {
    if (!cameraPathActive(world)) {
        return -1.0f;
    }
    const CameraPathPlayer& player = world.get<CameraPathPlayer>();
    const CameraPath* path = findPath(world, player.pathId);
    if (path == nullptr) {
        return -1.0f;
    }
    const float duration = cameraPathDuration(*path);
    if (duration <= 0.0f) {
        return 1.0f;
    }
    return std::clamp(player.time / duration, 0.0f, 1.0f);
}

bool cameraPathApplyToCamera(const flecs::world& world, Camera3D& camera) {
    if (!world.has<CameraPathPlayer>()) {
        return false;
    }
    const CameraPathPlayer& player = world.get<CameraPathPlayer>();
    if (player.active && player.havePose) {
        camera = cameraFromPose(player.pose, camera);
        return true;
    }
    if (player.blendRemaining > 0.0f && player.blendDuration > 0.0f) {
        const Camera3D from = cameraFromPose(player.blendFrom, camera);
        const float raw = 1.0f - player.blendRemaining / player.blendDuration;
        const float w = applyPathEase(PathEase::Smooth, raw);
        const Vector3 fromDir = Vector3Normalize(Vector3Subtract(from.target, from.position));
        const Vector3 toDir = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
        Vector3 dir = Vector3Lerp(fromDir, toDir, w);
        if (Vector3LengthSqr(dir) < 1e-6f) {
            dir = toDir;
        }
        camera.position = Vector3Lerp(from.position, camera.position, w);
        camera.target = Vector3Add(camera.position, Vector3Normalize(dir));
        camera.fovy = from.fovy + (camera.fovy - from.fovy) * w;
        return true;
    }
    return false;
}

}
