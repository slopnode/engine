#pragma once

#include "physics/components.hpp"

#include <raymath.h>

#include <cmath>

namespace slopengine {

inline void applyMoveWish(CharacterMotor& motor, float moveForward, float moveStrafe, float moveUp, float yaw) {
    const Vector3 forwardFlat = Vector3Normalize({std::sin(yaw), 0.0f, std::cos(yaw)});
    const Vector3 right = Vector3CrossProduct(forwardFlat, {0.0f, 1.0f, 0.0f});
    const Vector3 wish = Vector3Add(Vector3Scale(forwardFlat, moveForward), Vector3Scale(right, moveStrafe));
    motor.wishX = wish.x;
    motor.wishZ = wish.z;
    motor.wishY = moveUp;
}

}
