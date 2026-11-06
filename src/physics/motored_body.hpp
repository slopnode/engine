#pragma once

#include <raylib.h>
#include <raymath.h>

#include <cmath>
#include <string>

namespace slopengine {

/** Motor-driven flyer: package sets velocity/gravity; engine integrates vs static hulls and actors.
 *  @ingroup physics_components
 */
struct MotoredBody {
    Vector3 velocity = {0.0f, 0.0f, 0.0f};
    float gravity = 0.0f;
    float radius = 0.12f;
    float lifetime = 8.0f;
    float age = 0.0f;
    std::string onImpact;
    std::string ignoreId;
};

struct SphereCastHit {
    Vector3 point = {0.0f, 0.0f, 0.0f};
    Vector3 normal = {0.0f, 1.0f, 0.0f};
    float fraction = 1.0f;
};

inline Quaternion motoredFacing(Vector3 velocity) {
    const float speed = Vector3Length(velocity);
    if (speed < 1.0e-6f) {
        return QuaternionIdentity();
    }
    const Vector3 dir = Vector3Scale(velocity, 1.0f / speed);
    const float pitch = -asinf(Clamp(dir.y, -1.0f, 1.0f));
    const float yaw = atan2f(dir.x, dir.z);
    return QuaternionMultiply(
        QuaternionFromAxisAngle({0.0f, 1.0f, 0.0f}, yaw),
        QuaternionFromAxisAngle({1.0f, 0.0f, 0.0f}, pitch));
}

constexpr float kMotoredImpactClearance = 0.08f;

inline Vector3 impactEffectPosition(
    Vector3 surfacePoint,
    Vector3 outwardNormal,
    float clearance = kMotoredImpactClearance) {
    const float nLenSq =
        outwardNormal.x * outwardNormal.x + outwardNormal.y * outwardNormal.y +
        outwardNormal.z * outwardNormal.z;
    if (nLenSq < 1e-12f || clearance <= 0.0f) {
        return surfacePoint;
    }
    const float invLen = 1.0f / sqrtf(nLenSq);
    return {
        surfacePoint.x + outwardNormal.x * invLen * clearance,
        surfacePoint.y + outwardNormal.y * invLen * clearance,
        surfacePoint.z + outwardNormal.z * invLen * clearance,
    };
}

}
