#pragma once

#include <raylib.h>

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
