#pragma once

#include "map/emitter_bvh.hpp"
#include "map/light_occlusion.hpp"
#include "map/quad_bvh.hpp"
#include "map/radiosity_emitters.hpp"

#include <raylib.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace slopengine {

struct RadGpuLuxel {
    Vector3 position{};
    Vector3 normal{};
    float irradianceR = 0.0f;
    float irradianceG = 0.0f;
    float irradianceB = 0.0f;
    float sunIrradianceR = 0.0f;
    float sunIrradianceG = 0.0f;
    float sunIrradianceB = 0.0f;
    std::int32_t faceIndex = -1;
    std::int32_t covered = 0;
    std::int32_t interiorLeaf = -1;
};

struct RadGpuEmissiveFace {
    Vector3 uAxis{};
    float planeD = 0.0f;
    float uMin = 0.0f;
    float uMax = 0.0f;
    Vector3 vAxis{};
    float vMin = 0.0f;
    float vMax = 0.0f;
    float pad0 = 0.0f;
    Vector3 normal{};
    float area = 0.0f;
    std::int32_t faceIndex = -1;
    std::int32_t interiorLeaf = -1;
    std::int32_t gridWidth = 0;
    std::int32_t gridHeight = 0;
    std::int32_t gridOffset = 0;
    std::int32_t directSampleOffset = -1;
    Vector3 peakRadiance{};
    float castRange = 0.0f;
    Vector3 aabbMins{};
    std::int32_t directSampleCount = 0;
    Vector3 aabbMaxs{};
    /** 0 = use params.emitterDirectSamples; see EmissiveFace::directSampleAxis. */
    std::int32_t directSampleAxis = 0;
};

struct RadGpuLight {
    Vector3 position{};
    Vector3 direction{0.0f, 0.0f, 1.0f};
    Vector3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 8.0f;
    float coneAngle = 0.7f;
    std::int32_t kind = 0;
    std::int32_t interiorLeaf = -1;
};

struct RadGpuReachability {
    std::int32_t leafCount = 0;
    std::int32_t wordsPerRow = 0;
    std::vector<std::uint32_t> bits;
};

bool radiosityGpuContextReady();

/** OpenGL GL_RENDERER string, or empty when unavailable. */
const char* radiosityGpuRenderer();

/** True for integrated / software renderers that need conservative GPU lighting. */
bool radiosityGpuIsIntegrated();

struct RadGpuDirectParams {
    float directWrap = 0.35f;
    float coplanarFill = 0.15f;
    float coplanarSoft = 0.25f;
    float minDist2 = 0.0025f;
    float emitterQueryRadius = 0.0f;
    int emitterDirectSamples = 4;
    int emissionGridFloats = 0;
    int sunRayCount = 1;
    float sunAngularSpread = 0.0f;
    float sunLeakThreshold = 0.0f;
    float sunRayMaxDistance = 1000.0f;
    bool gpuSafeMode = false;
};

/** CPU reference data used to validate/fallback GPU direct lighting. */
struct RadGpuDirectCpuReference {
    const std::vector<LightmapFace>& faces;
    const std::unordered_map<std::string, MaterialBakeInfo>& materialCache;
    const std::vector<char>& faceSky;
    const std::vector<char>& faceTransparent;
    SunShadowSoftnessParams sunParams{};
    float directWrap = 0.35f;
};

bool accumulateDirectLightingGpu(
    std::vector<RadGpuLuxel>& luxels,
    const std::vector<RadGpuEmissiveFace>& emissiveFaces,
    std::span<const Vector3> emissionGrid,
    std::span<const EmitterDirectSample> directSampleData,
    const std::vector<RadGpuLight>& lights,
    const QuadBvh& occlusionBvh,
    const EmitterBvh& emitterBvh,
    std::string_view computeShaderSource,
    const RadGpuDirectParams& params = {},
    const RadGpuReachability& reachability = {},
    const std::vector<std::int32_t>& faceIsSky = {},
    const std::vector<std::int32_t>& faceIsTransparent = {},
    const RadGpuOcclusionResources& occlusionResources = {},
    const RadGpuDirectCpuReference* cpuReference = nullptr);

struct RadGpuBounceLuxel {
    Vector3 position{};
    Vector3 normal{};
    std::int32_t faceIndex = -1;
    std::int32_t covered = 0;
    std::int32_t localX = 0;
    std::int32_t localY = 0;
};

struct RadGpuFaceGrid {
    std::int32_t luxelBase = 0;
    std::int32_t luxelWidth = 0;
    std::int32_t luxelHeight = 0;
    std::int32_t valid = 0;
    Vector3 uAxis{};
    Vector3 vAxis{};
    float uMin = 0.0f;
    float uMax = 0.0f;
    float vMin = 0.0f;
    float vMax = 0.0f;
    float pad0 = 0.0f;
    float pad1 = 0.0f;
};

struct RadGpuBounceParams {
    int sampleCount = 16;
    float rayMaxDistance = 1000.0f;
    float ambientR = 0.0f;
    float ambientG = 0.0f;
    float ambientB = 0.0f;
    std::uint32_t seed = 1;
    bool gpuSafeMode = false;
};

/** Owns everything that's invariant across a bake's bounce loop (compiled shader,
 *  uploaded BVH/luxel/face-grid/material SSBOs) so createBounceGpuSession() only runs
 *  once per bake instead of once per bounce. Only the `shoot` buffer changes bounce to
 *  bounce; runBounceGpuPass() updates it in place. */
struct RadGpuBounceSession {
    unsigned int program = 0;
    unsigned int luxelSsbo = 0;
    unsigned int gatherSsbo = 0;
    unsigned int shootSsbo = 0;
    unsigned int faceSsbo = 0;
    unsigned int nodeSsbo = 0;
    unsigned int primSsbo = 0;
    unsigned int paramsSsbo = 0;
    unsigned int faceTransparentSsbo = 0;
    unsigned int faceOcclusionSsbo = 0;
    unsigned int materialRectSsbo = 0;
    int luxelCount = 0;
    int faceCount = 0;
    std::int32_t bvhRoot = -1;
    int luxelBatch = 0;
    /** Not owned; caller (bakeRadiosity) keeps this alive for the whole bake. */
    const RadGpuOcclusionResources* occlusionResources = nullptr;
};

/** One-time setup: compiles the bounce shader and uploads everything that doesn't
 *  change between bounces (scene BVH, luxel positions/normals, face grids, material/
 *  occlusion data). Returns nullopt on failure (missing GL context, compile failure,
 *  SSBO allocation failure) — caller should fall back to the CPU bounce path for the
 *  whole bake. `luxels` and `faceGrids` are read-only static per-luxel/per-face data;
 *  none of it is mutated during the bounce loop. */
std::optional<RadGpuBounceSession> createBounceGpuSession(
    const std::vector<RadGpuBounceLuxel>& luxels,
    const std::vector<RadGpuFaceGrid>& faceGrids,
    const QuadBvh& sceneBvh,
    std::string_view computeShaderSource,
    const std::vector<char>& faceTransparent,
    const RadGpuOcclusionResources& occlusionResources,
    bool gpuSafeMode);

/** Runs one bounce: uploads `shootRgb` (the only per-bounce input), dispatches, reads
 *  back `gatheredRgb`. Returns false on GL error or non-finite output — on failure the
 *  caller should call destroyBounceGpuSession() and fall back to CPU for this bounce
 *  and all remaining ones (a GPU failure is generally persistent; retrying would mean
 *  rebuilding the whole session anyway). */
bool runBounceGpuPass(
    RadGpuBounceSession& session,
    std::vector<Vector3>& gatheredRgb,
    const std::vector<Vector3>& shootRgb,
    const RadGpuBounceParams& bounceParams);

/** Frees everything owned by the session. Must be called exactly once when the bounce
 *  loop finishes (or aborts on failure). Safe to call on a default-constructed or
 *  already-destroyed session (all handles are 0). */
void destroyBounceGpuSession(RadGpuBounceSession& session);

}
