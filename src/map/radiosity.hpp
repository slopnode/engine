#pragma once

#include "assets/material_loader.hpp"
#include "map/bsp.hpp"
#include "map/lightmap.hpp"
#include "map/map_meta.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

/** Bake knobs for sloprad. */
struct RadiositySettings {
    float luxelsPerMeter = 16.0f;
    int bounces = 2;
    int samples = 16;
    int atlasSize = 1024;
    float directWrap = 0.35f;
    float coplanarFill = 0.15f;
    float ambientScale = 1.25f;
    bool preferGpu = true;
    /** Conservative GPU dispatch + merge when set (auto-enabled on integrated GPUs). */
    bool gpuSafeMode = false;
    std::string directComputeShaderSource;
    std::string bounceComputeShaderSource;
};

/** Material + optional albedo/emission images for bake sampling. */
struct MaterialBakeInfo {
    MaterialAsset asset;
    Image albedoImage{};
    Image emissionImage{};
    bool hasAlbedoImage = false;
    bool hasEmissionImage = false;
};

using MaterialBakeResolver = std::function<MaterialBakeInfo(std::string_view materialPath)>;

/** Packed rad file plus atlas Image pixels from a bake. */
struct RadiosityBakeResult {
    RadFile rad;
    std::vector<Image> atlasImages;
};

/** Bake-time light kind. */
enum class RadiosityLightKind {
    Point,
    Spot,
    Sun,
};

/** Thing light collected for the radiosity bake. */
struct RadiosityLight {
    RadiosityLightKind kind = RadiosityLightKind::Point;
    Vector3 position{};
    Vector3 direction{0.0f, 0.0f, 1.0f};
    Vector3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 8.0f;
    float coneAngle = 0.7f;
};

/** Runs radiosity on @p faces and returns atlases + rad metadata. */
RadiosityBakeResult bakeRadiosity(
    const std::vector<LightmapFace>& faces,
    const MapMeta& meta,
    const MaterialBakeResolver& resolveMaterial,
    const RadiositySettings& settings,
    const std::vector<RadiosityLight>& lights = {},
    const BspTree* tree = nullptr,
    bool hullSealed = false);

}
