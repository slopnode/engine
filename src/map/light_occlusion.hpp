#pragma once

#include "map/lightmap.hpp"
#include "map/quad_bvh.hpp"
#include "map/radiosity.hpp"

#include <raylib.h>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace slopengine {

constexpr float kLightOcclusionAlphaThreshold = 0.5f;
constexpr int kMaxAlphaOcclusionSteps = 8;

float sampleImageAlpha(const Image& image, float u, float v);

float sampleFaceOcclusionAlpha(
    const LightmapFace& face,
    const MaterialBakeInfo& material,
    Vector3 worldPos);

std::optional<QuadBvhHit> raycastWithAlphaOcclusion(
    const QuadBvh& bvh,
    Vector3 origin,
    Vector3 direction,
    float maxDistance,
    std::int32_t ignoreFaceA,
    std::int32_t ignoreFaceB,
    const std::vector<LightmapFace>& faces,
    const std::unordered_map<std::string, MaterialBakeInfo>& materialCache,
    const std::vector<char>& faceTransparent);

bool segmentOccludedWithAlphaOcclusion(
    const QuadBvh& bvh,
    Vector3 from,
    Vector3 to,
    std::int32_t ignoreFaceA,
    std::int32_t ignoreFaceB,
    const std::vector<LightmapFace>& faces,
    const std::unordered_map<std::string, MaterialBakeInfo>& materialCache,
    const std::vector<char>& faceTransparent);

float sunSkyVisibilityWithAlphaOcclusion(
    Vector3 luxelPos,
    std::int32_t luxelFaceIndex,
    Vector3 toLight,
    Vector3 tangent,
    Vector3 bitangent,
    const SunShadowSoftnessParams& sunParams,
    const QuadBvh& occlusionBvh,
    const std::vector<char>& faceSky,
    const std::vector<char>& faceTransparent,
    const std::vector<LightmapFace>& faces,
    const std::unordered_map<std::string, MaterialBakeInfo>& materialCache);

struct RadGpuMaterialRect {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    float baseColorAlpha = 1.0f;
    float textureWidth = 1.0f;
    float textureHeight = 1.0f;
    float pad0 = 0.0f;
};

struct RadGpuFaceOcclusion {
    float uvUAxisX = 0.0f;
    float uvUAxisY = 0.0f;
    float uvUAxisZ = 0.0f;
    float isTransparent = 0.0f;
    float uvVAxisX = 0.0f;
    float uvVAxisY = 0.0f;
    float uvVAxisZ = 0.0f;
    float materialIndex = -1.0f;
    float uvShiftX = 0.0f;
    float uvShiftY = 0.0f;
    float uvScaleX = 1.0f;
    float uvScaleY = 1.0f;
    float pixelsPerMeter = 64.0f;
    float baseColorAlpha = 1.0f;
};

struct RadGpuOcclusionResources {
    Texture2D alphaAtlas{};
    Image alphaAtlasImage{};
    std::vector<RadGpuFaceOcclusion> faceOcclusion;
    std::vector<RadGpuMaterialRect> materialRects;
    int atlasWidth = 1;
    int atlasHeight = 1;
    bool valid = false;
};

/** Builds a vertical-stack RGBA atlas + per-face SSBO data for GPU alpha occlusion. */
bool buildRadGpuOcclusionResources(
    const std::vector<LightmapFace>& faces,
    const std::unordered_map<std::string, MaterialBakeInfo>& materialCache,
    const std::vector<char>& faceTransparent,
    RadGpuOcclusionResources& out);

void unloadRadGpuOcclusionResources(RadGpuOcclusionResources& resources);

}
