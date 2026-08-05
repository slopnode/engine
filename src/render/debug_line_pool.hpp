#pragma once

#include <raylib.h>
#include <raymath.h>

#include <vector>

namespace slopengine {

struct DebugLine {
    Vector3 from{};
    Vector3 to{};
    Color color{};
};

struct DebugLinePool {
    std::vector<DebugLine> lines;
};

void drawDebugLinePool(const DebugLinePool& pool);

} // namespace slopengine
