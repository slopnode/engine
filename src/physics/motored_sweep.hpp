#pragma once

#include "physics/components.hpp"

#include <raylib.h>

#include <optional>

namespace slopengine {

/** Ray vs finite capsule. `dir` must be unit length. Returns distance along dir. */
std::optional<float> raycastCapsule(
    Vector3 origin,
    Vector3 dir,
    float maxDistance,
    Vector3 a,
    Vector3 b,
    float radius);

/** Sweep a sphere along origin→origin+dir*distance against an upright actor capsule.
 *  Returns cast fraction in [0,1] on hit. `dir` must be unit length. */
std::optional<float> sweepSphereActorCapsule(
    Vector3 origin,
    Vector3 dir,
    float distance,
    float sphereRadius,
    Vector3 feet,
    const CharacterMotor& motor);

}
