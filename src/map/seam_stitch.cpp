#include "map/seam_stitch.hpp"

#include <algorithm>
#include <cmath>

namespace slopengine {

namespace {

float dot3(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 sub3(Vector3 a, Vector3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

float length3(Vector3 v) {
    return std::sqrt(dot3(v, v));
}

} // namespace

bool luxelsShareSeam(
    Vector3 posA,
    Vector3 normalA,
    Vector3 posB,
    Vector3 normalB,
    const SeamStitchParams& params) {
    if (params.maxDistance <= 0.0f) {
        return false;
    }
    if (dot3(normalA, normalB) < params.normalCosThreshold) {
        return false;
    }
    const float planeDistA = dot3(normalA, posA);
    const float planeDistB = dot3(normalA, posB);
    if (std::fabs(planeDistA - planeDistB) > params.planeEps) {
        return false;
    }
    return length3(sub3(posA, posB)) <= params.maxDistance;
}

float seamBlendWeight(float distance, float maxDistance) {
    if (maxDistance <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(1.0f - distance / maxDistance, 0.0f, 1.0f);
}

} // namespace slopengine
