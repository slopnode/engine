#pragma once

#include "assets/material_loader.hpp"
#include "map/lightmap.hpp"
#include "map/map_meta.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

struct RadiositySettings {
    float luxelsPerMeter = 16.0f;
    int bounces = 2;
    int samples = 32;
    int atlasSize = 512;
    float directWrap = 0.35f;
    float coplanarFill = 0.15f;
    float ambientScale = 1.25f;
    bool preferGpu = true;
    std::string directComputeShaderSource;
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

enum class RadiosityLightKind {
    Point,
    Spot,
};

struct RadiosityLight {
    RadiosityLightKind kind = RadiosityLightKind::Point;
    Vector3 position{};
    Vector3 direction{0.0f, 0.0f, 1.0f};
    Vector3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 8.0f;
    float coneAngle = 0.7f;
};

RadiosityBakeResult bakeRadiosity(
    const std::vector<LightmapFace>& faces,
    const MapMeta& meta,
    const MaterialBakeResolver& resolveMaterial,
    const RadiositySettings& settings,
    const std::vector<RadiosityLight>& lights = {});

}
