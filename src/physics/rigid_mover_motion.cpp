#include "physics/rigid_mover.hpp"

#include <raymath.h>

#include <algorithm>
#include <cmath>

namespace slopengine {

namespace {

constexpr float kShoveSkin = 0.02f;

Vector3 localAxis(MoverRotAxis axis) {
    switch (axis) {
    case MoverRotAxis::Pitch:
        return {1.0f, 0.0f, 0.0f};
    case MoverRotAxis::Roll:
        return {0.0f, 0.0f, 1.0f};
    case MoverRotAxis::Yaw:
    default:
        return {0.0f, 1.0f, 0.0f};
    }
}

Vector3 rotateVec(Quaternion q, Vector3 v) {
    return Vector3RotateByQuaternion(v, q);
}

}

void computeMoverPose(
    const RigidMover& mover,
    float t,
    Vector3& outPos,
    Quaternion& outRot) {
    t = std::clamp(t, 0.0f, 1.0f);
    const Vector3 pivotWorld =
        Vector3Add(mover.closedPos, rotateVec(mover.closedRot, mover.pivotLocal));
    const float angle = t * mover.openAngleRadians;
    Quaternion delta = QuaternionIdentity();
    if (std::fabs(angle) > 1.0e-8f) {
        const Vector3 axisLocal = localAxis(mover.rotAxis);
        const Vector3 axisWorld = Vector3Normalize(rotateVec(mover.closedRot, axisLocal));
        delta = QuaternionFromAxisAngle(axisWorld, angle);
    }
    outRot = QuaternionNormalize(QuaternionMultiply(delta, mover.closedRot));
    const Vector3 slide = rotateVec(mover.closedRot, Vector3Scale(mover.openPosOffset, t));
    outPos = Vector3Add(
        Vector3Add(pivotWorld, rotateVec(outRot, Vector3Negate(mover.pivotLocal))),
        slide);
}

Vector3 moverCollideWorldCenter(const Vector3& pos, const Quaternion& rot, const RigidMover& mover) {
    return Vector3Add(pos, rotateVec(rot, mover.collideCenterLocal));
}

bool moverComputeShove(
    MoverPushMode mode,
    Vector3 normal,
    float pen,
    Vector3& outDelta) {
    outDelta = {0.0f, 0.0f, 0.0f};
    if (mode == MoverPushMode::Off || pen <= 0.0f) {
        return false;
    }
    Vector3 shoveNormal = normal;
    if (mode == MoverPushMode::Horizontal) {
        shoveNormal.y = 0.0f;
        const float hLenSq = shoveNormal.x * shoveNormal.x + shoveNormal.z * shoveNormal.z;
        if (hLenSq < 1.0e-8f) {
            return false;
        }
        const float inv = 1.0f / std::sqrt(hLenSq);
        shoveNormal.x *= inv;
        shoveNormal.z *= inv;
    }
    if (shoveNormal.x == 0.0f && shoveNormal.y == 0.0f && shoveNormal.z == 0.0f) {
        return false;
    }
    const float push = pen + kShoveSkin;
    outDelta = {shoveNormal.x * push, shoveNormal.y * push, shoveNormal.z * push};
    return true;
}

void tickRigidMover(RigidMover& mover, float dt) {
    if (!(dt > 0.0f) || !std::isfinite(dt)) {
        return;
    }

    const float duration = mover.duration > 1.0e-4f ? mover.duration : 0.8f;
    const float step = dt / duration;
    if (mover.progress < mover.target) {
        mover.progress = std::min(mover.target, mover.progress + step);
    } else if (mover.progress > mover.target) {
        mover.progress = std::max(mover.target, mover.progress - step);
    }

    if (mover.autoClose > 0.0f && mover.target >= 0.5f && mover.progress >= 0.999f) {
        mover.autoCloseTimer += dt;
        if (mover.autoCloseTimer >= mover.autoClose) {
            mover.target = 0.0f;
            mover.autoCloseTimer = 0.0f;
        }
    } else {
        mover.autoCloseTimer = 0.0f;
    }
}

}
