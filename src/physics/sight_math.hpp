#pragma once

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

inline bool sightHasTag(const std::vector<std::string>& tags, std::string_view tag) {
    for (const std::string& t : tags) {
        if (t == tag) {
            return true;
        }
    }
    return false;
}

inline bool sightTagsAllow(
    const std::vector<std::string>& seeTags,
    const std::vector<std::string>& ignoreTags,
    const std::vector<std::string>& targetTags) {
    for (const std::string& ignored : ignoreTags) {
        if (sightHasTag(targetTags, ignored)) {
            return false;
        }
    }
    if (seeTags.empty()) {
        return true;
    }
    for (const std::string& required : seeTags) {
        if (sightHasTag(targetTags, required)) {
            return true;
        }
    }
    return false;
}

inline float sightAngleDeltaRad(float fromYaw, float toYaw) {
    float d = toYaw - fromYaw;
    while (d > 3.14159265358979323846f) {
        d -= 6.28318530717958647692f;
    }
    while (d < -3.14159265358979323846f) {
        d += 6.28318530717958647692f;
    }
    return d;
}

inline bool sightInFov(
    float observerYaw,
    float toTargetX,
    float toTargetZ,
    float fovDegrees) {
    if (fovDegrees >= 360.0f) {
        return true;
    }
    if (fovDegrees <= 0.0f) {
        return false;
    }
    const float lenSq = toTargetX * toTargetX + toTargetZ * toTargetZ;
    if (lenSq <= 1.0e-12f) {
        return true;
    }
    const float bearing = std::atan2(toTargetX, toTargetZ);
    const float half = fovDegrees * 0.5f * (3.14159265358979323846f / 180.0f);
    return std::fabs(sightAngleDeltaRad(observerYaw, bearing)) <= half + 1.0e-5f;
}

inline float sightEyeOffset(float motorHeight, float motorRadius, float eyeLift) {
    return (motorHeight + motorRadius) * eyeLift;
}

}
