#pragma once

#include "map/bsp.hpp"
#include "map/lightmap.hpp"
#include "map/quad_bvh.hpp"

#include <raylib.h>

#include <vector>

namespace slopengine {

struct LightProbeBakeSettings {
    float cellSize = 4.0f;
    float fineCellSize = 2.0f;
    int sampleCount = 32;
};

std::vector<Vector3> placeCoarseLightProbes(const BspTree& tree, float cellSize);

std::vector<Vector3> placeFineLightProbes(const BspTree& tree, float cellSize, float fineCellSize);

std::vector<LightProbe> bakeLightProbes(
    const std::vector<Vector3>& positions,
    float cellSize,
    const QuadBvh& sceneBvh,
    const std::vector<LightmapFace>& faces,
    const RadFile& rad,
    const std::vector<Image>& atlasImages,
    Vector3 ambientFallback,
    int sampleCount);

struct LightProbeBakeResult {
    LightProbeGridInfo coarse;
    LightProbeGridInfo fine;
};

LightProbeBakeResult bakeLightProbeGrids(
    const BspTree& tree,
    const QuadBvh& sceneBvh,
    const std::vector<LightmapFace>& faces,
    const RadFile& rad,
    const std::vector<Image>& atlasImages,
    Vector3 ambientFallback,
    const LightProbeBakeSettings& settings);

}
