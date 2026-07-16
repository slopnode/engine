#pragma once

#include <raylib.h>

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace slopengine {

enum class BrushBoxSide {
    Top,
    Bottom,
    North,
    South,
    East,
    West,
};

struct BrushFace {
    std::string id;
    std::string material;
    Vector3 normal{};
    std::array<Vector3, 4> corners{};
    Vector2 uvShiftPixels{};
};

struct Brush {
    std::string id;
    Vector3 mins{};
    Vector3 maxs{};
    std::vector<BrushFace> faces;
};

const char* brushBoxSideName(BrushBoxSide side);

Brush makeBrushBox(
    std::string id,
    Vector3 mins,
    Vector3 maxs,
    const std::string& defaultMaterial,
    const std::vector<std::pair<BrushBoxSide, BrushFace>>& faceOverrides);

}
