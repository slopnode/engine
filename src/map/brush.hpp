#pragma once

#include <raylib.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace slopengine {

/** Named sides of an axis-aligned brush-box. */
enum class BrushBoxSide {
    Top,
    Bottom,
    North,
    South,
    East,
    West,
};

/** Brush participation in BSP / VIS / physics. See maps.md role matrix. */
enum class BrushRole {
    Hull,
    Detail,
    Hint,
    Trigger,
    Water,
    Window,
};

/** One polygonal face of a convex brush. */
struct BrushFace {
    std::string id;
    std::string material; /**< Material virtual path. */
    Vector3 normal{};
    std::vector<Vector3> vertices; /**< Outward winding. */
    Vector2 uvShiftPixels{};
    Vector3 uvUAxis{};
    Vector3 uvVAxis{};
    bool nodraw = false; /**< Omit from mesh and lightmaps. */
    bool uvLock = false; /**< Keep texture glued under transforms. */
};

/** Convex solid used for CSG maps and prefabs. */
struct Brush {
    std::string id;
    BrushRole role = BrushRole::Hull;
    Vector3 mins{};
    Vector3 maxs{};
    std::vector<BrushFace> faces;
    bool box = false;      /**< True when authored as brush-box. */
    bool nocollide = false; /**< Skip physics body when true. */
};

const char* brushBoxSideName(BrushBoxSide side);
const char* brushRoleName(BrushRole role);
bool parseBrushRoleName(std::string_view name, BrushRole& out);
bool brushRoleContributesSplits(BrushRole role);
bool brushRoleSeals(BrushRole role);
bool brushRoleEmitsVisFaces(BrushRole role);
bool brushRoleDefaultNocollide(BrushRole role);
bool brushRoleNeedsInteriorPlacement(BrushRole role);

Vector3 faceNormalFromVertices(const std::vector<Vector3>& vertices);
void recomputeBrushBounds(Brush& brush);
bool pointInsideBrush(Vector3 point, const Brush& brush, float epsilon = 1e-4f);
bool pointInsideBrushInclusive(Vector3 point, const Brush& brush, float epsilon = 1e-4f);

/** Validation failure for a convex brush. */
struct BrushConvexError {
    std::string message;
};

std::optional<BrushConvexError> validateBrushConvex(const Brush& brush);

/** Builds an axis-aligned box brush with optional per-side overrides. */
Brush makeBrushBox(
    std::string id,
    Vector3 mins,
    Vector3 maxs,
    const std::string& defaultMaterial,
    const std::vector<std::pair<BrushBoxSide, BrushFace>>& faceOverrides,
    BrushRole role = BrushRole::Hull);

/** Builds a convex brush from faces, or nullopt with @p errorOut. */
std::optional<Brush> makeBrushConvex(
    std::string id,
    std::vector<BrushFace> faces,
    BrushRole role,
    std::string& errorOut);

std::vector<std::array<Vector3, 3>> triangulateFace(const std::vector<Vector3>& vertices);

}
