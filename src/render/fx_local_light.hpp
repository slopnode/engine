#pragma once

#include "map/quad_bvh.hpp"
#include "render/dynamic_light.hpp"
#include "render/render_frustum.hpp"

#include <flecs.h>
#include <raylib.h>
#include <raymath.h>

#include <functional>
#include <unordered_map>
#include <vector>

namespace slopengine {

constexpr float kFxLightGridCellSize = 4.0f;
constexpr int kMaxFxLightsPerReceiver = 16;

/** High-count point light that tints dynamic receivers only (not the map lightmap).
 *  @ingroup render_components
 */
struct FxLocalLight {
    DynamicLightColor color{};
    float intensity = 1.0f;
    float range = 3.0f;
};

/** One FX light in world space for the current frame. */
struct FxLightEval {
    Vector3 position{};
    Vector3 linearRgb{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 3.0f;
};

struct FxLightGridCell {
    int x = 0;
    int y = 0;
    int z = 0;

    bool operator==(const FxLightGridCell& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct FxLightGridCellHash {
    std::size_t operator()(const FxLightGridCell& cell) const {
        std::size_t h = std::hash<int>{}(cell.x);
        h ^= std::hash<int>{}(cell.y) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(cell.z) + 0x9e3779b9u + (h << 6) + (h >> 2);
        return h;
    }
};

/** Frame cache: dense lights plus a uniform grid for nearby queries. */
struct FxLightFrameState {
    std::vector<FxLightEval> lights;
    std::unordered_map<FxLightGridCell, std::vector<int>, FxLightGridCellHash> grid;
    float cellSize = kFxLightGridCellSize;
};

Vector3 fxLocalLightLinearRgb(const FxLocalLight& light);

flecs::entity spawnFxLocalLight(
    flecs::world& world,
    const char* name,
    Vector3 position,
    const FxLocalLight& light);

void clearFxLightFrameState(FxLightFrameState& state);

void buildFxLightFrameState(
    flecs::world& world,
    const Frustum* frustum,
    bool unlit,
    FxLightFrameState& out);

/** True if world geo blocks the segment from light to receiver (nudged endpoints). */
bool lightSegmentOccluded(
    const QuadBvh* bvh,
    Vector3 lightPos,
    Vector3 point,
    const std::vector<char>* skipFaces = nullptr);

Vector3 evaluateFxLightsAtPoint(
    const FxLightFrameState& state,
    Vector3 point,
    Vector3 normal,
    const QuadBvh* occlusionBvh = nullptr,
    const std::vector<char>* occlusionSkipFaces = nullptr,
    int maxLights = kMaxFxLightsPerReceiver);

/** Sums ranked DynamicLights and FX local lights at @p point. */
Vector3 evaluateOverlayLightsAtPoint(
    const std::vector<RankedDynamicLight>* dynLights,
    const FxLightFrameState* fxLights,
    Vector3 point,
    Vector3 normal,
    const QuadBvh* occlusionBvh = nullptr,
    const std::vector<char>* occlusionSkipFaces = nullptr);

/** Composites baked tint with dyn/FX overlay in lighting-multiplier space. */
Color composeBakeTintWithOverlay(Color bakeTint, Vector3 overlay);

void storeFxLightFrameState(flecs::world& world, FxLightFrameState state);

/** Baked probe (when available) plus ranked dyn + FX overlay at @p origin.
 *  @p bakeMaxDistance is the downward bake ray length (sprites use ~2m; airborne FX need more). */
Color sampleReceiverTintColor(
    flecs::world& world,
    Vector3 origin,
    bool unlit,
    float bakeMaxDistance = 2.0f);

/** Tint for a world model using AABB sample points (survives mover motion). */
Color sampleReceiverTintColorForModel(
    flecs::world& world,
    const Model& model,
    const Matrix& globalMatrix,
    bool unlit,
    const Matrix* secondaryMatrix = nullptr);

/** Bake/ambient probe only for GPU-lit world models (no dyn/FX overlay). */
Color sampleBakeTintColorForModel(
    flecs::world& world,
    const Model& model,
    const Matrix& globalMatrix,
    bool unlit,
    const Matrix* secondaryMatrix = nullptr);

}
