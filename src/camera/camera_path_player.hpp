#pragma once

#include "map/camera_path.hpp"

#include <flecs.h>
#include <raylib.h>

#include <string>

namespace slopengine {

struct CameraPathPlayer {
    bool active = false;
    std::string pathId;
    float time = 0.0f;
    float speed = 1.0f;
    bool loop = false;
    float blendOut = 0.0f;
    bool havePose = false;
    CameraPose pose{};
    float blendRemaining = 0.0f;
    float blendDuration = 0.0f;
    CameraPose blendFrom{};
};

void registerCameraPathPlayer(flecs::world& world);

bool startCameraPath(
    flecs::world& world,
    const std::string& pathId,
    bool loop,
    float speed,
    float blendOut);
void stopCameraPath(flecs::world& world, float blendOut);
void resetCameraPathPlayer(flecs::world& world);

bool cameraPathActive(const flecs::world& world);
bool cameraPathOverridesView(const flecs::world& world);
float cameraPathProgress(const flecs::world& world);

bool cameraPathApplyToCamera(const flecs::world& world, Camera3D& camera);

}
