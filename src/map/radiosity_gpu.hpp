#pragma once

#include "map/emitter_bvh.hpp"
#include "map/quad_bvh.hpp"

#include <raylib.h>

#include <cstdint>
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
    std::int32_t pad1 = 0;
    Vector3 peakRadiance{};
    float castRange = 0.0f;
    Vector3 aabbMins{};
    float pad3 = 0.0f;
    Vector3 aabbMaxs{};
    float pad4 = 0.0f;
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
    bool gpuSafeMode = false;
};

bool accumulateDirectLightingGpu(
    std::vector<RadGpuLuxel>& luxels,
    const std::vector<RadGpuEmissiveFace>& emissiveFaces,
    std::span<const Vector3> emissionGrid,
    const std::vector<RadGpuLight>& lights,
    const QuadBvh& occlusionBvh,
    const EmitterBvh& emitterBvh,
    std::string_view computeShaderSource,
    const RadGpuDirectParams& params = {},
    const RadGpuReachability& reachability = {},
    const std::vector<std::int32_t>& faceIsSky = {},
    const std::vector<std::int32_t>& faceIsTransparent = {});

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

bool accumulateBounceLightingGpu(
    std::vector<RadGpuBounceLuxel>& luxels,
    std::vector<Vector3>& gatheredRgb,
    const std::vector<Vector3>& shootRgb,
    const std::vector<RadGpuFaceGrid>& faceGrids,
    const QuadBvh& sceneBvh,
    std::string_view computeShaderSource,
    const RadGpuBounceParams& params,
    const std::vector<char>& faceTransparent = {});

}
