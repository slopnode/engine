#include "physics/motored_sweep.hpp"

#include <raymath.h>

#include <cmath>

namespace slopengine {

namespace {

bool pointInsideCapsule(Vector3 p, Vector3 a, Vector3 b, float radius) {
    const Vector3 ba = Vector3Subtract(b, a);
    const Vector3 pa = Vector3Subtract(p, a);
    const float baba = Vector3DotProduct(ba, ba);
    float t = 0.0f;
    if (baba > 1.0e-12f) {
        t = Vector3DotProduct(pa, ba) / baba;
        if (t < 0.0f) {
            t = 0.0f;
        } else if (t > 1.0f) {
            t = 1.0f;
        }
    }
    const Vector3 closest = Vector3Add(a, Vector3Scale(ba, t));
    return Vector3LengthSqr(Vector3Subtract(p, closest)) <= radius * radius;
}

} // namespace

std::optional<float> raycastCapsule(
    Vector3 origin,
    Vector3 dir,
    float maxDistance,
    Vector3 a,
    Vector3 b,
    float radius) {
    if (radius <= 0.0f || maxDistance <= 1.0e-8f) {
        return std::nullopt;
    }

    const Vector3 ba = Vector3Subtract(b, a);
    const Vector3 oa = Vector3Subtract(origin, a);
    const float baba = Vector3DotProduct(ba, ba);
    const float bard = Vector3DotProduct(ba, dir);
    const float baoa = Vector3DotProduct(ba, oa);
    const float rdoa = Vector3DotProduct(dir, oa);
    const float oaoa = Vector3DotProduct(oa, oa);

    if (baba <= 1.0e-12f) {
        const float bDot = rdoa;
        const float c = oaoa - radius * radius;
        const float h = bDot * bDot - c;
        if (h < 0.0f) {
            return std::nullopt;
        }
        const float t = -bDot - std::sqrt(h);
        if (t < 0.0f || t > maxDistance) {
            return std::nullopt;
        }
        return t;
    }

    const float aa = baba - bard * bard;
    const float bb = baba * rdoa - baoa * bard;
    const float cc = baba * oaoa - baoa * baoa - radius * radius * baba;
    float tBody = -1.0f;
    if (std::fabs(aa) > 1.0e-8f) {
        const float h = bb * bb - aa * cc;
        if (h >= 0.0f) {
            const float t = (-bb - std::sqrt(h)) / aa;
            const float y = baoa + t * bard;
            if (y > 0.0f && y < baba && t >= 0.0f && t <= maxDistance) {
                tBody = t;
            }
        }
    }

    auto capHit = [&](Vector3 center) -> float {
        const Vector3 oc = Vector3Subtract(origin, center);
        const float bCap = Vector3DotProduct(dir, oc);
        const float cCap = Vector3DotProduct(oc, oc) - radius * radius;
        const float hCap = bCap * bCap - cCap;
        if (hCap < 0.0f) {
            return -1.0f;
        }
        const float t = -bCap - std::sqrt(hCap);
        if (t < 0.0f || t > maxDistance) {
            return -1.0f;
        }
        return t;
    };

    const float tA = capHit(a);
    const float tB = capHit(b);
    float best = tBody;
    if (tA >= 0.0f && (best < 0.0f || tA < best)) {
        best = tA;
    }
    if (tB >= 0.0f && (best < 0.0f || tB < best)) {
        best = tB;
    }
    if (best < 0.0f) {
        return std::nullopt;
    }
    return best;
}

std::optional<float> sweepSphereActorCapsule(
    Vector3 origin,
    Vector3 dir,
    float distance,
    float sphereRadius,
    Vector3 feet,
    const CharacterMotor& motor) {
    const float actorRadius = motor.radius > 0.0f ? motor.radius : 0.3f;
    const float actorHeight = motor.height > 0.0f ? motor.height : 1.1f;
    const float combined = sphereRadius + actorRadius;
    if (combined <= 0.0f || distance <= 1.0e-8f) {
        return std::nullopt;
    }

    const Vector3 axisA{feet.x, feet.y + actorRadius, feet.z};
    const Vector3 axisB{feet.x, feet.y + actorRadius + actorHeight, feet.z};

    if (pointInsideCapsule(origin, axisA, axisB, combined)) {
        return 0.0f;
    }

    if (const auto hitDist = raycastCapsule(origin, dir, distance, axisA, axisB, combined)) {
        return *hitDist / distance;
    }
    return std::nullopt;
}

}
