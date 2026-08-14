#pragma once

#include "assets/asset_store.hpp"
#include "map/brush.hpp"
#include "map/fac.hpp"

#include <raylib.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace slopengine {

constexpr std::uint32_t kRadMagic = 0x31444152u; // "RAD1" LE
constexpr std::uint32_t kRadVersion = 5;
constexpr std::uint32_t kRadVersionLegacy = 2;
constexpr std::uint32_t kRadVersionPrevious = 3;
constexpr std::uint32_t kRadVersionGroups = 4;
constexpr std::uint32_t kRadVersionProbes = 5;

/** Atlas pixel encoding recorded in rad v3+. */
enum class LightmapEncoding : std::uint32_t {
    Ldr = 0,  /**< Legacy Reinhard-baked RGB irradiance. */
    Rgbe = 1, /**< HDR shared-exponent linear irradiance. */
};

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
    bool transparent = false;
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
    float groupUMin = 0.0f;
    float groupUMax = 0.0f;
    float groupVMin = 0.0f;
    float groupVMax = 0.0f;
};

/** A cluster of coplanar, UV-frame-matching, edge-adjacent faces sharing one chart. */
struct LightmapFaceGroup {
    std::vector<std::int32_t> faceIndices;
    Vector3 uAxis{};
    Vector3 vAxis{};
    float uMin = 0.0f;
    float uMax = 0.0f;
    float vMin = 0.0f;
    float vMax = 0.0f;
    std::int32_t atlasIndex = 0;
    int atlasX = 0;
    int atlasY = 0;
    int luxelWidth = 0;
    int luxelHeight = 0;
};

/** Atlas texture path and size recorded in a .rad file. */
struct LightmapAtlasInfo {
    std::string texturePath;
    std::int32_t width = 0;
    std::int32_t height = 0;
    LightmapEncoding encoding = LightmapEncoding::Ldr;
};

struct LightProbe {
    std::int32_t cellX = 0;
    std::int32_t cellY = 0;
    std::int32_t cellZ = 0;
    std::array<Color, 4> shRgbe{};
};

struct LightProbeGridInfo {
    float cellSize = 4.0f;
    std::vector<LightProbe> probes;
};

/** Parsed rad/static.rad sidecar (charts + atlas metadata). */
struct RadFile {
    float luxelsPerMeter = 16.0f;
    std::vector<LightmapAtlasInfo> atlases;
    std::vector<LightmapChart> charts;
    LightProbeGridInfo probeGridCoarse;
    LightProbeGridInfo probeGridFine;
};

/** Packed charts plus RGB luxel buffers before PNG write. */
struct LightmapPackResult {
    RadFile rad;
    std::vector<std::vector<float>> atlasRgb;
    std::vector<LightmapFaceGroup> groups;
};

/** Collects drawable faces from brushes for packing / bake. */
std::vector<LightmapFace> collectLightmapFaces(const std::vector<Brush>& brushes);

/** Collects drawable faces from a VIS visible-face set. */
std::vector<LightmapFace> collectLightmapFaces(const FacFile& vis);

/** Clusters coplanar, UV-frame-matching, edge-adjacent faces so they can share one chart. */
std::vector<LightmapFaceGroup> groupCoplanarLightmapFaces(const std::vector<LightmapFace>& faces);

/** Packs faces into atlas charts at @p luxelsPerMeter.
 *  When @p skipFaces is non-null and sized to faces, non-zero entries are omitted. */
LightmapPackResult packLightmapCharts(
    const std::vector<LightmapFace>& faces,
    float luxelsPerMeter,
    int atlasSize = 512,
    const std::vector<char>* skipFaces = nullptr);

bool writeRadFile(const std::filesystem::path& path, const RadFile& rad);
std::optional<RadFile> readRadFile(const std::filesystem::path& path);
std::optional<RadFile> readRadBytes(std::span<const std::byte> data);

/** Ward-style shared-exponent RGBE for linear irradiance atlases. */
Color encodeRgbe(float r, float g, float b);
Vector3 decodeRgbe(Color pixel);
Color linearIrradianceToDisplayColor(float r, float g, float b);

/** Primary encoding for a map (Rgbe if any atlas uses it). */
LightmapEncoding primaryLightmapEncoding(const RadFile& rad);

Shader loadLightmapShader(AssetStore& assets, int& useLightmapLoc);
void applyLightmapEncoding(Shader shader, LightmapEncoding encoding);

/** Loads the infinite-sky background shader. */
Shader loadSkyboxBackgroundShader(AssetStore& assets);

/** Loads the sky-face shader used by map sky brush materials. */
Shader loadSkyFaceShader(AssetStore& assets);

struct DynamicLightShaderBindings;
struct DynamicLightShadowState;

/** Binds a dummy sampler2DArray so lit preview/map draw does not conflict with albedo. */
void bindLightmapDummyShadowMaps(Shader shader);

}
