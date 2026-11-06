#pragma once

#include <flecs.h>
#include <raylib.h>

#include <string>
#include <vector>

namespace slopengine {

struct BoneSpringSpec {
    std::string bone;
    std::string param;
    float stiffness = 0.0f;
    float damping = 0.0f;
    float gain = 0.0f;
    float limit = 0.0f;
};

struct BoneSpringState {
    int boneIndex = -1;
    int paramIndex = -1;
    int frames = 0;
    Vector3 lastPos{};
    Vector3 lastVel{};
    Vector3 accel{};
    Vector3 offset{};
    Vector3 velocity{};
};

struct BoneSpringTuning {
    float step = 1.0f / 120.0f;
    float maxDt = 0.1f;
    float accelSmoothing = 20.0f;
};

struct BoneSprings {
    std::vector<BoneSpringSpec> specs;
    std::vector<BoneSpringState> states;
    BoneSpringTuning tuning;
    const void* resolvedFor = nullptr;
    bool active = false;
    bool cleared = true;
};

void registerBoneSpringSystem(flecs::world& world);

}
