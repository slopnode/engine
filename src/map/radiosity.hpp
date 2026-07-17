#pragma once

#include "assets/material_loader.hpp"
#include "map/bsp.hpp"
#include "map/lightmap.hpp"
#include "map/map_meta.hpp"

#include <functional>
#include <string_view>
#include <vector>

namespace slopengine {

struct RadiositySettings {
    float luxelsPerMeter = 16.0f;
    int bounces = 2;
    int samples = 32;
    int atlasSize = 512;
};

struct MaterialBakeInfo {
    MaterialAsset asset;
    Image albedoImage{};
    Image emissionImage{};
    bool hasAlbedoImage = false;
    bool hasEmissionImage = false;
};

using MaterialBakeResolver = std::function<MaterialBakeInfo(std::string_view materialPath)>;

struct RadiosityBakeResult {
    RadFile rad;
    std::vector<Image> atlasImages;
};

RadiosityBakeResult bakeRadiosity(
    const std::vector<LightmapFace>& faces,
    const BspTree& occlusionTree,
    const MapMeta& meta,
    const MaterialBakeResolver& resolveMaterial,
    const RadiositySettings& settings);

}
