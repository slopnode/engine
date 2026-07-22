#pragma once

#include "assets/asset_store.hpp"
#include "map/brush.hpp"
#include "map/vis.hpp"

#include <raylib.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace slopengine {

constexpr std::uint32_t kRadMagic = 0x31444152u; // "RAD1" LE
constexpr std::uint32_t kRadVersion = 2;

/** Brush face prepared for lightmap chart packing. */
struct LightmapFace {
    std::string id;
    std::string material;
    Vector3 normal{};
    std::vector<Vector3> vertices;
    Vector2 uvShiftPixels{};
    Vector2 uvScale{1.0f, 1.0f};
    Vector3 uvUAxis{};
    Vector3 uvVAxis{};
    bool uvLock = false;
    std::int32_t interiorLeaf = -1;
};

/** One chart's placement in a lightmap atlas. */
struct LightmapChart {
    std::int32_t faceIndex = -1;
    std::string faceId;
    std::int32_t atlasIndex = 0;
    std::int32_t luxelWidth = 0;
    std::int32_t luxelHeight = 0;
    std::int32_t atlasX = 0;
    std::int32_t atlasY = 0;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
};

/** Atlas texture path and size recorded in a .rad file. */
struct LightmapAtlasInfo {
    std::string texturePath;
    std::int32_t width = 0;
    std::int32_t height = 0;
};

/** Parsed rad/static.rad sidecar (charts + atlas metadata). */
struct RadFile {
    float luxelsPerMeter = 16.0f;
    std::vector<LightmapAtlasInfo> atlases;
    std::vector<LightmapChart> charts;
};

/** Packed charts plus RGB luxel buffers before PNG write. */
struct LightmapPackResult {
    RadFile rad;
    std::vector<std::vector<float>> atlasRgb;
};

/** Collects drawable faces from brushes for packing / bake. */
std::vector<LightmapFace> collectLightmapFaces(const std::vector<Brush>& brushes);

/** Collects drawable faces from a VIS visible-face set. */
std::vector<LightmapFace> collectLightmapFaces(const VisFile& vis);

/** Packs faces into atlas charts at @p luxelsPerMeter. */
LightmapPackResult packLightmapCharts(
    const std::vector<LightmapFace>& faces,
    float luxelsPerMeter,
    int atlasSize = 512);

bool writeRadFile(const std::filesystem::path& path, const RadFile& rad);
std::optional<RadFile> readRadFile(const std::filesystem::path& path);
std::optional<RadFile> readRadBytes(std::span<const std::byte> data);

Shader loadLightmapShader(AssetStore& assets, int& useLightmapLoc);

}
