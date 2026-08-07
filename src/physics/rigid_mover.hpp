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

enum class MoverPushMode {
    Full,
    Horizontal,
    Off,
};

/** Kinematic door/platform: tweens between closed and open pose and shoves/crushes blockers.
 *  @ingroup physics_components
 */
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
    float autoClose = 0.0f;
    float autoCloseTimer = 0.0f;
    MoverBlockMode blockMode = MoverBlockMode::Shove;
    MoverPushMode pushMode = MoverPushMode::Full;
    bool slide = true;
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

Matrix moverClosedMatrix(const RigidMover& mover, Vector3 scale);

Vector3 moverCollideWorldCenter(const Vector3& pos, const Quaternion& rot, const RigidMover& mover);

bool moverComputeShove(
    MoverPushMode mode,
    Vector3 normal,
    float pen,
    Vector3& outDelta);

void tickRigidMover(RigidMover& mover, float dt);

void registerRigidMoverSystem(flecs::world& world);

bool moverRequestOpen(flecs::entity entity);
bool moverRequestClose(flecs::entity entity);
bool moverRequestToggle(flecs::entity entity);
void moverRequestOpenGroup(flecs::world& world, std::string_view groupId);
void moverRequestCloseGroup(flecs::world& world, std::string_view groupId);
void moverRequestToggleGroup(flecs::world& world, std::string_view groupId);
void moverApplyState(flecs::entity entity, bool open, float progress, bool setLocked, bool locked);

}
