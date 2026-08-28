#pragma once

#include <raylib.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

/** Optional directional sun from map.meta (bake only). */
struct MapSun {
    bool enabled = false;
    Vector3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    Vector3 angles{}; /**< Pitch yaw roll; yaw-only maps to angles.y. */
};

/** Saved RAD bake options, mirroring slopmap::RadCompileOptions. gpuSafetyMode is
 *  0 = Auto, 1 = Fast, 2 = Safe. */
struct MapRadOptions {
    float luxelsPerMeter = 16.0f;
    int bounces = 2;
    int samples = 16;
    int emitterDirectSamples = 4;
    float emitterGridLuxelsPerMeter = 8.0f;
    int emitterGridMaxSize = 32;
    int exactEmissionGridMaxSize = 256;
    int exactEmissionMaxSamples = 8192;
    float sunShadowSoftness = 0.0f;
    float seamStitchRadiusLuxels = 1.5f;
    float probeCellSize = 4.0f;
    float probeFineCellSize = 2.0f;
    int probeSampleCount = 32;
    bool preferGpu = true;
    bool forceDiscreteGpu = true;
    int gpuSafetyMode = 0;
    float gpuWatchdogLimitSeconds = 2.0f;
    int gpuMaxLuxelBatch = 0;
};

/** Fields from `maps/<name>/map.meta`. */
struct MapMeta {
    std::string id;
    std::string name;
    std::string author;
    std::string description;
    std::string package; /**< Owning package id (filled from directory at load). */
    std::vector<std::string> depends; /**< Other package ids that must be mounted. */
    Vector3 ambient{0.0f, 0.0f, 0.0f}; /**< Legacy; bake/runtime ambient comes from ambient-light things. */
    MapSun sun; /**< Legacy; bake sun comes from sun things. */
    MapRadOptions rad; /**< Saved RAD bake options, editor-only. */
    bool hasRad = false; /**< Whether the file had a (rad ...) form (vs. rad being unset defaults). */
};

/** Parses map.meta text into @p out. */
bool parseMapMeta(std::string_view source, MapMeta& out);

/** Serializes @p meta into map.meta s-expression text. */
std::string formatMapMeta(const MapMeta& meta);

/** Writes @p meta to @p path as map.meta text, creating parent directories as needed. */
bool writeMapMeta(const std::filesystem::path& path, const MapMeta& meta);

}
