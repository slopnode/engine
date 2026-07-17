#pragma once

#include "map/brush.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace slopengine {

constexpr std::uint32_t kRadMagic = 0x31444152u; // "RAD1" LE
constexpr std::uint32_t kRadVersion = 2;

struct LightmapFace {
    std::string id;
    std::string material;
    Vector3 normal{};
    std::vector<Vector3> vertices;
    Vector2 uvShiftPixels{};
};

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

struct LightmapAtlasInfo {
    std::string texturePath;
    std::int32_t width = 0;
    std::int32_t height = 0;
};

struct RadFile {
    float luxelsPerMeter = 16.0f;
    std::vector<LightmapAtlasInfo> atlases;
    std::vector<LightmapChart> charts;
};

struct LightmapPackResult {
    RadFile rad;
    std::vector<std::vector<float>> atlasRgb;
};

std::vector<LightmapFace> collectLightmapFaces(const std::vector<Brush>& brushes);

LightmapPackResult packLightmapCharts(
    const std::vector<LightmapFace>& faces,
    float luxelsPerMeter,
    int atlasSize = 512);

bool writeRadFile(const std::filesystem::path& path, const RadFile& rad);
std::optional<RadFile> readRadFile(const std::filesystem::path& path);
std::optional<RadFile> readRadBytes(std::span<const std::byte> data);

}
