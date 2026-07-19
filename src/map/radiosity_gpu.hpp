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

struct RadGpuLight {
    Vector3 position{};
    Vector3 direction{0.0f, 0.0f, 1.0f};
    Vector3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 8.0f;
    float coneAngle = 0.7f;
    std::int32_t kind = 0;
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
    const std::vector<RadGpuLight>& lights,
    const QuadBvh& occlusionBvh,
    std::string_view computeShaderSource,
    const RadGpuDirectParams& params = {});

}
