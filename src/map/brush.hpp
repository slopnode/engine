#pragma once

#include "map/handler_binding.hpp"

#include <raylib.h>

#include <array>
#include <cstdint>
#include <functional>
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
    Door,
    Hint,
    Trigger,
    Water,
    Window,
    Transparent,
};

/** Per-brush physics query blocking flags. */
namespace BrushBlock {
constexpr std::uint8_t Los = 1u << 0; /**< los?, actor-los?, ActorSight scans. */
constexpr std::uint8_t Linescan = 1u << 1; /**< hitscan-actors wall clip. */
constexpr std::uint8_t Projectile = 1u << 2; /**< MotoredBody / particle ray/sphere casts. */
constexpr std::uint8_t Player = 1u << 3; /**< Player character movement. */
constexpr std::uint8_t Actor = 1u << 4; /**< Non-player character movement. */
constexpr std::uint8_t All = Los | Linescan | Projectile | Player | Actor;
}

/** Engine door motion on a detail brush leaf. */
enum class DoorMotion {
    Raise,
    Slide,
    Swing,
};

/** Rotation axis for a Swing door, evaluated in the brush's closed-pose local space. */
enum class DoorAxis {
    Pitch, /**< Tilts around the local X axis, e.g. a hatch/ramp rotating up. */
    Yaw, /**< Rotates around the local vertical axis, e.g. a standard swinging door. */
    Roll, /**< Rotates around the local Z axis. */
};

/** Underwater screen-effect metadata when role is Water. Read by the runtime
 *  into a MapWaterVolumes entry; unauthored fields keep their engine defaults. */
struct BrushWater {
    Vector3 tint{0.05f, 0.25f, 0.35f};
    bool haveTint = false;
    float wobble = 0.4f;
    bool haveWobble = false;
    float vignette = 0.35f;
    bool haveVignette = false;
};

/** Door motion/interact metadata when role is Door (spawns RigidMover at load). */
struct BrushDoor {
    DoorMotion motion = DoorMotion::Raise;
    float duration = 0.6f;
    bool haveDuration = false;
    float autoClose = 0.0f;
    bool haveAutoClose = false;
    float angle = 1.5707963267948966f;
    bool haveAngle = false;
    DoorAxis axis = DoorAxis::Yaw;
    bool haveAxis = false;
    float travel = 0.0f;
    bool haveTravel = false;
    std::string hingeThingId;
    std::string group;
    std::string prompt = "Open";
    bool havePrompt = false;
    HandlerBinding canUse;
};

/** One polygonal face of a convex brush. */
struct BrushFace {
    std::string id;
    std::string material; /**< Material virtual path. */
    Vector3 normal{};
    std::vector<Vector3> vertices; /**< Outward winding. */
    Vector2 uvShiftPixels{};
    Vector2 uvScale{1.0f, 1.0f};
    Vector3 uvUAxis{};
    Vector3 uvVAxis{};
    bool nodraw = false; /**< Omit from mesh and lightmaps. */
    bool uvLock = false; /**< Keep texture glued under transforms. */
    HandlerBinding onUse; /**< Package Scheme on-use binding; empty id = none. */
    HandlerBinding onTouch; /**< Package Scheme on-touch binding; empty id = none. */
};

/** Convex solid used for CSG maps and prefabs. */
struct Brush {
    std::string id;
    BrushRole role = BrushRole::Hull;
    Vector3 mins{};
    Vector3 maxs{};
    std::vector<BrushFace> faces;
    bool box = false; /**< True when authored as brush-box. */
    std::uint8_t blocks = BrushBlock::All; /**< Physics query blocking mask. */
    bool nocollide = false; /**< Derived: true when @p blocks is zero. */
    BrushDoor door{}; /**< Meaningful when role is Door. */
    BrushWater water{}; /**< Meaningful when role is Water. */
};

const char* brushBoxSideName(BrushBoxSide side);
BrushBoxSide brushBoxSideFromNormal(Vector3 normal);
const char* brushRoleName(BrushRole role);
bool parseBrushRoleName(std::string_view name, BrushRole& out);
const char* doorMotionName(DoorMotion motion);
bool parseDoorMotionName(std::string_view name, DoorMotion& out);
const char* doorAxisName(DoorAxis axis);
bool parseDoorAxisName(std::string_view name, DoorAxis& out);
bool brushRoleContributesSplits(BrushRole role);
bool brushRoleSeals(BrushRole role);
bool brushRoleEmitsVisFaces(BrushRole role);
bool brushRoleOccludesVisFaces(BrushRole role);
bool brushRoleReceivesVisOcclusion(BrushRole role);
bool brushRoleDefaultNocollide(BrushRole role);
std::uint8_t brushRoleDefaultBlocks(BrushRole role);
bool brushBlocksAny(std::uint8_t blocks);
void syncBrushNocollide(Brush& brush);
void setBrushBlocks(Brush& brush, std::uint8_t blocks);
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

/** Weld verts within brush, merge exact overlaps, drop degenerates. */
void cleanupBrushGeometry(Brush& brush, float grid);

/** Assign face ids/normals/UV axes and bounds; does not require convexity. */
Brush finalizeBrushFaces(
    std::string id,
    std::vector<BrushFace> faces,
    BrushRole role = BrushRole::Hull);

/** Builds an axis-aligned box brush with optional per-side overrides. */
Brush makeBrushBox(
    std::string id,
    Vector3 mins,
    Vector3 maxs,
    const std::string& defaultMaterial,
    const std::vector<std::pair<BrushBoxSide, BrushFace>>& faceOverrides,
    BrushRole role = BrushRole::Hull);

/** If @p brush's current faces geometrically form an axis-aligned box
 *  (regardless of its current @c box flag), rebuilds it via makeBrushBox
 *  (preserving id/role/blocks/door/water and each face's material/uv/nodraw
 *  by side) and sets @c box = true. Leaves @p brush untouched and returns
 *  false otherwise. */
bool reclassifyBrushAsBox(Brush& brush);

/** Builds a convex brush from faces, or nullopt with @p errorOut. */
std::optional<Brush> makeBrushConvex(
    std::string id,
    std::vector<BrushFace> faces,
    BrushRole role,
    std::string& errorOut);

/** Prism inscribed in the AABB footprint, revolved around whichever world axis
 *  (X/Y/Z) @p axis is most closely aligned to (default Y). The two other axes
 *  form the ring; the chosen axis's AABB extent is the prism's height. Faces
 *  get default (unlocked, zero-shift) UV, same as any other freshly built
 *  brush face; texture alignment across the ring is a manual step. */
std::optional<Brush> makeBrushCylinder(
    std::string id,
    Vector3 mins,
    Vector3 maxs,
    int sides,
    const std::string& material,
    BrushRole role,
    std::string& errorOut,
    Vector3 axis = {0.0f, 1.0f, 0.0f});

/** Stacked box steps filling the AABB; rise along Y, run along longer of X/Z. */
std::vector<Brush> makeBrushStairs(
    const std::string& idPrefix,
    Vector3 mins,
    Vector3 maxs,
    int steps,
    const std::string& material,
    BrushRole role);

/** Wedge steps spiraling around whichever world axis @p axis is most closely
 *  aligned to (default Y), same axis-selection rule as makeBrushCylinder.
 *  Unlike makeBrushCylinder, the outer radius is not read independently per
 *  ring axis (which would let a non-square footprint draw an oval): it's the
 *  average of the AABB's two ring-axis half-extents, so callers are expected
 *  to hand in a square footprint (the create-tool enforces this while
 *  dragging). The ring center is the footprint's centroid; steps climb from
 *  the AABB's axis mins to maxs, each spanning one step's own rise (not
 *  solid down to the base, since a spiral revisits the same angle every
 *  @p sides steps at a higher elevation). Each step advances 360/sides
 *  degrees and keeps turning past a full revolution for as many turns as
 *  the height needs. Step count is chosen so risers divide the drawn height
 *  evenly, as close to @p stepHeight as an integer count allows. Empty if
 *  sides < 3, innerRadius is negative or would meet/exceed the derived outer
 *  radius, stepHeight <= 0, or the AABB is degenerate. */
std::vector<Brush> makeBrushSpiralStairs(
    const std::string& idPrefix,
    Vector3 mins,
    Vector3 maxs,
    float innerRadius,
    float stepHeight,
    int sides,
    const std::string& material,
    BrushRole role,
    Vector3 axis = {0.0f, 1.0f, 0.0f});

/** Six wall slabs leaving an inner void; empty if thickness is invalid.
 *  When @p outward is false, walls grow inward inside the source AABB.
 *  When @p outward is true, the source AABB is the inner void and walls expand outside. */
std::vector<Brush> hollowBrushBox(
    const Brush& source,
    float thickness,
    const std::function<std::string()>& allocateId,
    bool outward = false);

/**
 * Rebuild an AABB brush around a rectangular opening punched from @p faceSide.
 * Opening is in face UV space: u/v along the two face axes from face mins.
 * @p depth is distance into the solid along -normal; use brush thickness for full cut.
 */
std::vector<Brush> punchOutBrushBox(
    const Brush& source,
    BrushBoxSide faceSide,
    float u0,
    float u1,
    float v0,
    float v1,
    float depth,
    const std::function<std::string()>& allocateId);

struct BrushSplitResult {
    Brush front;
    Brush back;
};

/** Split a brush by a plane. Nullopt if the plane misses or a half is empty. Does not require convex halves. */
std::optional<BrushSplitResult> splitBrushByPlane(
    const Brush& source,
    Vector3 planePoint,
    Vector3 planeNormal,
    const std::function<std::string()>& allocateId,
    std::string& errorOut);

std::vector<std::array<Vector3, 3>> triangulateFace(const std::vector<Vector3>& vertices);

}
