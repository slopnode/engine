#pragma once

#include <raylib.h>
#include <raymath.h>

#include <cmath>

namespace slopengine {

inline Vector3 forwardFromYawPitch(float yaw, float pitch) {
    const float cosPitch = std::cos(pitch);
    return Vector3Normalize({
        std::sin(yaw) * cosPitch,
        std::sin(pitch),
        std::cos(yaw) * cosPitch,
    });
}

}
