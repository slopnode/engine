#pragma once

#include <raylib.h>
#include <raymath.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>

#include <flecs.h>

namespace slopengine {

enum class MoverRotAxis {
    Pitch,
    Yaw,
    Roll,
};

enum class MoverBlockMode {
    Shove,
    Crush,
};

struct RigidMover {
    Vector3 closedPos = {0.0f, 0.0f, 0.0f};
    Quaternion closedRot = QuaternionIdentity();
    Vector3 openPosOffset = {0.0f, 0.0f, 0.0f};
    float openAngleRadians = 0.0f;
    MoverRotAxis rotAxis = MoverRotAxis::Yaw;
    Vector3 pivotLocal = {0.0f, 0.0f, 0.0f};
    float duration = 0.8f;
    float progress = 0.0f;
    float target = 0.0f;
    MoverBlockMode blockMode = MoverBlockMode::Shove;
    std::string groupId;
    Vector3 collideHalfExtents = {0.5f, 1.0f, 0.05f};
    Vector3 collideCenterLocal = {0.0f, 1.0f, 0.0f};
    bool locked = false;
    std::string onCrush;
    std::unordered_set<std::uint64_t> crushing;
    bool kinematicReady = false;
};

void computeMoverPose(
    const RigidMover& mover,
    float t,
    Vector3& outPos,
    Quaternion& outRot);

Vector3 moverCollideWorldCenter(const Vector3& pos, const Quaternion& rot, const RigidMover& mover);

void registerRigidMoverSystem(flecs::world& world);

bool moverRequestOpen(flecs::entity entity);
bool moverRequestClose(flecs::entity entity);
bool moverRequestToggle(flecs::entity entity);
void moverRequestOpenGroup(flecs::world& world, std::string_view groupId);
void moverRequestCloseGroup(flecs::world& world, std::string_view groupId);
void moverRequestToggleGroup(flecs::world& world, std::string_view groupId);
void moverApplyState(flecs::entity entity, bool open, float progress, bool setLocked, bool locked);

}
