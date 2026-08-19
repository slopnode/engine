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
    /** Conservative GPU dispatch when set (auto-enabled on integrated GPUs). */
    bool gpuSafeMode = false;
    /** N×N stratified UV samples per receiver–emissive-face pair in direct lighting. */
    int emitterDirectSamples = 4;
    /** For materials with MaterialAsset::preciseEmission: direct-sample axis density per meter
     *  of face length (replaces emitterDirectSamples for that face, scaled to length so long
     *  faces keep separate mask features distinct instead of averaging them into one block). */
    float precisionDirectSamplesPerMeter = 2.0f;
    /** Upper bound on the scaled axis count from @ref precisionDirectSamplesPerMeter, so a very
     *  long precise-emission face can't blow up bake time or the direct-sample buffer size. */
    int precisionMaxDirectSamples = 16;
    int exactEmissionGridMaxSize = 256;
    int exactEmissionMaxSamples = 8192;
    /** World-space resolution for pre-baked per-face emission cast grids. */
    float emitterGridLuxelsPerMeter = 8.0f;
    /** Maximum emission grid dimension per emissive face axis. */
    int emitterGridMaxSize = 32;
    /** 0 = sharp sun/window shadows (legacy), 1 = fuzzy penumbra. */
    float sunShadowSoftness = 0.0f;
    float seamStitchRadiusLuxels = 1.5f;
    std::string directComputeShaderSource;
    std::string bounceComputeShaderSource;
    /** Volumetric light probe grid: coarse cell spacing covering all open space. */
    float probeCellSize = 4.0f;
    /** Fine probe cell spacing, populated only near geometry (walls/corners/doorways). */
    float probeFineCellSize = 2.0f;
    /** Sphere samples gathered per probe for the SH L1 projection. */
    int probeSampleCount = 32;
};

/** Resolved bake parameters derived from @ref RadiositySettings::sunShadowSoftness. */
struct SunShadowSoftnessParams {
    int rayCount = 1;
    float angularSpreadRad = 0.0f;
    float leakThreshold = 0.0f;
    float sunDenoiseSpatialSigma = 1.0f;
    float sunDenoiseRangeSigma = 0.35f;
    int sunDenoiseKernelRadius = 1;
    float maxRayDistance = 1000.0f;
};

SunShadowSoftnessParams resolveSunShadowSoftness(float softness);

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
