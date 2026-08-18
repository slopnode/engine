#pragma once

#include <raylib.h>

#include <cstdint>

namespace slopengine {

/** Resolved per-tick movement/look intent, captured once from InputState. */
struct InputCommand {
    std::uint64_t tick = 0;
    float moveForward = 0.0f;
    float moveStrafe = 0.0f;
    Vector2 look{};
};

}
