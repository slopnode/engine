#pragma once

#include "render/components.hpp"

#include <flecs.h>
#include <raylib.h>

namespace slopengine {

struct SimPose {
    LocalTransformation prev{};
    LocalTransformation cur{};
    LocalTransformation presented{};
};

struct SimEyePose {
    Vector3 prev{};
    Vector3 cur{};
    Vector3 presented{};
    float ownAlpha = -1.0f;
};

struct SimViewOffsets {
    Vector4 prevEye{};
    Vector4 curEye{};
    Vector4 presentedEye{};
    Vector3 prevModel{};
    Vector3 curModel{};
    Vector3 presentedModel{};
};

struct SimSpriteOffset {
    Vector2 prev{};
    Vector2 cur{};
    Vector2 presented{};
};

inline constexpr float kSimPoseSnapDistance = 4.0f;

LocalTransformation blendSimPose(const SimPose& pose, float alpha);
bool sameSimPose(const LocalTransformation& a, const LocalTransformation& b, bool compareRotation);
void registerSimInterpolation(flecs::world& world);

}
