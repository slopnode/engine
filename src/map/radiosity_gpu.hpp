#pragma once

#include "map/quad_bvh.hpp"

#include <raylib.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace slopengine {

struct RadGpuLuxel {
    Vector3 position{};
    Vector3 normal{};
    float irradianceR = 0.0f;
    float irradianceG = 0.0f;
    float irradianceB = 0.0f;
    std::int32_t faceIndex = -1;
    std::int32_t covered = 0;
};

struct RadGpuEmitter {
    Vector3 position{};
    Vector3 normal{};
    float radianceR = 0.0f;
    float radianceG = 0.0f;
    float radianceB = 0.0f;
    float area = 0.0f;
    std::int32_t faceIndex = -1;
};

bool radiosityGpuContextReady();

struct RadGpuDirectParams {
    float directWrap = 0.35f;
    float coplanarFill = 0.15f;
    float coplanarSoft = 0.25f;
    float minDist2 = 0.0025f;
};

bool accumulateDirectLightingGpu(
    std::vector<RadGpuLuxel>& luxels,
    const std::vector<RadGpuEmitter>& emitters,
    const QuadBvh& occlusionBvh,
    std::string_view computeShaderSource,
    const RadGpuDirectParams& params = {});

}
