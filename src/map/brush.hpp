#pragma once

#include <raylib.h>

#include <array>
#include <optional>
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

enum class BrushRole {
    Hull,
    Detail,
};

struct BrushFace {
    std::string id;
    std::string material;
    Vector3 normal{};
    std::vector<Vector3> vertices;
    Vector2 uvShiftPixels{};
    Vector3 uvUAxis{};
    Vector3 uvVAxis{};
    bool nodraw = false;
    bool uvLock = false;
};

struct Brush {
    std::string id;
    BrushRole role = BrushRole::Hull;
    Vector3 mins{};
    Vector3 maxs{};
    std::vector<BrushFace> faces;
    bool box = false;
    bool nocollide = false;
};

const char* brushBoxSideName(BrushBoxSide side);
const char* brushRoleName(BrushRole role);

Vector3 faceNormalFromVertices(const std::vector<Vector3>& vertices);
void recomputeBrushBounds(Brush& brush);
bool pointInsideBrush(Vector3 point, const Brush& brush, float epsilon = 1e-4f);
bool pointInsideBrushInclusive(Vector3 point, const Brush& brush, float epsilon = 1e-4f);

struct BrushConvexError {
    std::string message;
};

std::optional<BrushConvexError> validateBrushConvex(const Brush& brush);

Brush makeBrushBox(
    std::string id,
    Vector3 mins,
    Vector3 maxs,
    const std::string& defaultMaterial,
    const std::vector<std::pair<BrushBoxSide, BrushFace>>& faceOverrides,
    BrushRole role = BrushRole::Hull);

std::optional<Brush> makeBrushConvex(
    std::string id,
    std::vector<BrushFace> faces,
    BrushRole role,
    std::string& errorOut);

std::vector<std::array<Vector3, 3>> triangulateFace(const std::vector<Vector3>& vertices);

}
