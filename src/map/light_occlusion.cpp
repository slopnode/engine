#include "map/light_occlusion.hpp"

#include "map/uv_math.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace slopengine {

namespace {

constexpr float kRayAdvanceEpsilon = 0.001f;

float dot3(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 add3(Vector3 a, Vector3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 sub3(Vector3 a, Vector3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vector3 scale3(Vector3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}

Vector3 normalize3(Vector3 v) {
    const float len = std::sqrt(dot3(v, v));
    if (len < 1e-8f) {
        return {0.0f, 1.0f, 0.0f};
    }
    return scale3(v, 1.0f / len);
}

Vector3 refract3(Vector3 incident, Vector3 normal, float eta) {
    const float nDotI = dot3(normal, incident);
    const float k = 1.0f - eta * eta * (1.0f - nDotI * nDotI);
    if (k < 0.0f) {
        return {0.0f, 0.0f, 0.0f};
    }
    return sub3(scale3(incident, eta), scale3(normal, eta * nDotI + std::sqrt(k)));
}

Vector3 materialTint(const MaterialBakeInfo& material) {
    return {
        static_cast<float>(material.asset.baseColor.r) / 255.0f,
        static_cast<float>(material.asset.baseColor.g) / 255.0f,
        static_cast<float>(material.asset.baseColor.b) / 255.0f,
    };
}

Vector3 mul3(Vector3 a, Vector3 b) {
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

bool faceIsTransparentRole(
    const std::vector<char>& faceTransparent,
    std::int32_t faceIndex) {
    return faceIndex >= 0 && static_cast<std::size_t>(faceIndex) < faceTransparent.size()
        && faceTransparent[static_cast<std::size_t>(faceIndex)] != 0;
}

bool faceIsSky(const std::vector<char>& faceSky, std::int32_t faceIndex) {
    return faceIndex >= 0 && static_cast<std::size_t>(faceIndex) < faceSky.size()
        && faceSky[static_cast<std::size_t>(faceIndex)] != 0;
}

const MaterialBakeInfo* materialForFace(
    const LightmapFace& face,
    const std::unordered_map<std::string, MaterialBakeInfo>& materialCache) {
    const auto it = materialCache.find(face.material);
    if (it == materialCache.end()) {
        return nullptr;
    }
    return &it->second;
}

Vector3 sampleSunRayDirection(
    Vector3 toLight,
    Vector3 tangent,
    Vector3 bitangent,
    float angularSpreadRad,
    int rayIndex,
    int rayCount) {
    if (rayCount <= 1 || angularSpreadRad <= 0.0f) {
        return toLight;
    }
    const int strataN =
        std::max(1, static_cast<int>(std::floor(std::sqrt(static_cast<float>(rayCount)))));
    const int strataM = std::max(1, (rayCount + strataN - 1) / strataN);
    const int sy = rayIndex / strataN;
    const int sx = rayIndex % strataN;
    const float fu = (static_cast<float>(sx) + 0.5f) / static_cast<float>(strataN);
    const float fv = (static_cast<float>(sy) + 0.5f) / static_cast<float>(strataM);
    const float r = std::sqrt(fu);
    const float theta = fv * 6.28318530718f;
    const float dx = r * std::cos(theta);
    const float dy = r * std::sin(theta);
    const float spread = std::tan(angularSpreadRad);
    return normalize3(
        add3(toLight, add3(scale3(tangent, dx * spread), scale3(bitangent, dy * spread))));
}

bool shouldIgnoreFace(std::int32_t faceIndex, std::int32_t ignoreFaceA, std::int32_t ignoreFaceB) {
    return faceIndex == ignoreFaceA || faceIndex == ignoreFaceB;
}

bool hitBlocksLight(
    const QuadBvhHit& hit,
    const std::vector<LightmapFace>& faces,
    const std::unordered_map<std::string, MaterialBakeInfo>& materialCache,
    const std::vector<char>& faceTransparent) {
    if (hit.faceIndex < 0 || hit.faceIndex >= static_cast<std::int32_t>(faces.size())) {
        return true;
    }
    if (!faceIsTransparentRole(faceTransparent, hit.faceIndex)) {
        return true;
    }
    const LightmapFace& face = faces[static_cast<std::size_t>(hit.faceIndex)];
    const MaterialBakeInfo* material = materialForFace(face, materialCache);
    if (material == nullptr) {
        return true;
    }
    const float alpha = sampleFaceOcclusionAlpha(face, *material, hit.point);
    return alpha >= kLightOcclusionAlphaThreshold;
}

std::optional<QuadBvhHit> raycastSunWithRefraction(
    const QuadBvh& bvh,
    Vector3 origin,
    Vector3 direction,
    float maxDistance,
    std::int32_t ignoreFaceA,
    std::int32_t ignoreFaceB,
    const std::vector<LightmapFace>& faces,
    const std::unordered_map<std::string, MaterialBakeInfo>& materialCache,
    const std::vector<char>& faceTransparent,
    Vector3* tintOut) {
    if (tintOut != nullptr) {
        *tintOut = {1.0f, 1.0f, 1.0f};
    }
    if (bvh.empty() || maxDistance <= 0.0f) {
        return std::nullopt;
    }
    Vector3 dir = normalize3(direction);
    Vector3 rayOrigin = origin;
    float traveled = 0.0f;
    bool bent = false;

    for (int step = 0; step < kMaxAlphaOcclusionSteps; ++step) {
        if (traveled >= maxDistance) {
            return std::nullopt;
        }
        const float remaining = maxDistance - traveled;
        const auto hit = raycastQuadBvh(bvh, rayOrigin, dir, remaining, -1, nullptr);
        if (!hit) {
            return std::nullopt;
        }
        if (shouldIgnoreFace(hit->faceIndex, ignoreFaceA, ignoreFaceB)) {
            rayOrigin = add3(rayOrigin, scale3(dir, hit->distance + kRayAdvanceEpsilon));
            traveled += hit->distance + kRayAdvanceEpsilon;
            continue;
        }
        if (hitBlocksLight(*hit, faces, materialCache, faceTransparent)) {
            QuadBvhHit adjusted = *hit;
            adjusted.distance += traveled;
            adjusted.point = add3(rayOrigin, scale3(dir, hit->distance));
            return adjusted;
        }
        const LightmapFace& face = faces[static_cast<std::size_t>(hit->faceIndex)];
        const MaterialBakeInfo* material = materialForFace(face, materialCache);
        const Vector3 hitPoint = add3(rayOrigin, scale3(dir, hit->distance));
        if (material != nullptr) {
            if (tintOut != nullptr) {
                *tintOut = mul3(*tintOut, materialTint(*material));
            }
            if (!bent && material->asset.ior > 1.0f) {
                const Vector3 refracted = refract3(dir, face.normal, 1.0f / material->asset.ior);
                if (dot3(refracted, refracted) > 1e-8f) {
                    dir = refracted;
                }
                bent = true;
            }
        }
        rayOrigin = add3(hitPoint, scale3(dir, kRayAdvanceEpsilon));
        traveled += hit->distance + kRayAdvanceEpsilon;
    }
    return std::nullopt;
}

} // namespace

float sampleImageAlpha(const Image& image, float u, float v) {
    if (image.data == nullptr || image.width <= 0 || image.height <= 0) {
        return 1.0f;
    }
    u = u - std::floor(u);
    v = v - std::floor(v);
    const int x =
        std::clamp(static_cast<int>(u * static_cast<float>(image.width)), 0, image.width - 1);
    const int y =
        std::clamp(static_cast<int>(v * static_cast<float>(image.height)), 0, image.height - 1);
    return static_cast<float>(GetImageColor(image, x, y).a) / 255.0f;
}

float sampleFaceOcclusionAlpha(
    const LightmapFace& face,
    const MaterialBakeInfo& material,
    Vector3 worldPos) {
    const float baseAlpha = static_cast<float>(material.asset.baseColor.a) / 255.0f;
    if (!material.hasAlbedoImage) {
        return baseAlpha;
    }

    Vector3 uAxis{};
    Vector3 vAxis{};
    faceUvAxes(face.uvLock, face.normal, face.uvUAxis, face.uvVAxis, uAxis, vAxis);
    MaterialUvInfo uvInfo{};
    uvInfo.pixelsPerMeter = material.asset.pixelsPerMeter;
    uvInfo.textureWidth = static_cast<float>(material.albedoImage.width);
    uvInfo.textureHeight = static_cast<float>(material.albedoImage.height);
    const Vector2 uv =
        worldPlanarUv(worldPos, uAxis, vAxis, face.uvShiftPixels, face.uvScale, uvInfo);
    return sampleImageAlpha(material.albedoImage, uv.x, uv.y) * baseAlpha;
}

std::optional<QuadBvhHit> raycastWithAlphaOcclusion(
    const QuadBvh& bvh,
    Vector3 origin,
    Vector3 direction,
    float maxDistance,
    std::int32_t ignoreFaceA,
    std::int32_t ignoreFaceB,
    const std::vector<LightmapFace>& faces,
    const std::unordered_map<std::string, MaterialBakeInfo>& materialCache,
    const std::vector<char>& faceTransparent,
    Vector3* tintOut) {
    if (tintOut != nullptr) {
        *tintOut = {1.0f, 1.0f, 1.0f};
    }
    if (bvh.empty() || maxDistance <= 0.0f) {
        return std::nullopt;
    }
    const Vector3 dir = normalize3(direction);
    float traveled = 0.0f;

    for (int step = 0; step < kMaxAlphaOcclusionSteps; ++step) {
        if (traveled >= maxDistance) {
            return std::nullopt;
        }
        const Vector3 rayOrigin = add3(origin, scale3(dir, traveled));
        const float remaining = maxDistance - traveled;
        const auto hit = raycastQuadBvh(bvh, rayOrigin, dir, remaining, -1, nullptr);
        if (!hit) {
            return std::nullopt;
        }
        if (shouldIgnoreFace(hit->faceIndex, ignoreFaceA, ignoreFaceB)) {
            traveled += hit->distance + kRayAdvanceEpsilon;
            continue;
        }
        if (hitBlocksLight(*hit, faces, materialCache, faceTransparent)) {
            QuadBvhHit adjusted = *hit;
            adjusted.distance += traveled;
            adjusted.point = add3(origin, scale3(dir, adjusted.distance));
            return adjusted;
        }
        if (tintOut != nullptr) {
            const MaterialBakeInfo* material =
                materialForFace(faces[static_cast<std::size_t>(hit->faceIndex)], materialCache);
            if (material != nullptr) {
                *tintOut = mul3(*tintOut, materialTint(*material));
            }
        }
        traveled += hit->distance + kRayAdvanceEpsilon;
    }
    return std::nullopt;
}

bool segmentOccludedWithAlphaOcclusion(
    const QuadBvh& bvh,
    Vector3 from,
    Vector3 to,
    std::int32_t ignoreFaceA,
    std::int32_t ignoreFaceB,
    const std::vector<LightmapFace>& faces,
    const std::unordered_map<std::string, MaterialBakeInfo>& materialCache,
    const std::vector<char>& faceTransparent,
    Vector3* tintOut) {
    if (tintOut != nullptr) {
        *tintOut = {1.0f, 1.0f, 1.0f};
    }
    const Vector3 delta = sub3(to, from);
    const float distance = std::sqrt(dot3(delta, delta));
    if (distance < 1e-5f) {
        return false;
    }
    const auto hit = raycastWithAlphaOcclusion(
        bvh,
        from,
        delta,
        distance * 0.999f,
        ignoreFaceA,
        ignoreFaceB,
        faces,
        materialCache,
        faceTransparent,
        tintOut);
    if (!hit) {
        return false;
    }
    return !shouldIgnoreFace(hit->faceIndex, ignoreFaceA, ignoreFaceB);
}

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
    const std::unordered_map<std::string, MaterialBakeInfo>& materialCache,
    Vector3* tintOut) {
    if (tintOut != nullptr) {
        *tintOut = {1.0f, 1.0f, 1.0f};
    }
    const float kSunRayDistance = sunParams.maxRayDistance;
    if (sunParams.rayCount <= 1 || sunParams.angularSpreadRad <= 0.0f) {
        Vector3 rayTint{1.0f, 1.0f, 1.0f};
        const auto hit = raycastSunWithRefraction(
            occlusionBvh,
            luxelPos,
            toLight,
            kSunRayDistance,
            luxelFaceIndex,
            -1,
            faces,
            materialCache,
            faceTransparent,
            &rayTint);
        if (hit && faceIsSky(faceSky, hit->faceIndex)) {
            if (tintOut != nullptr) {
                *tintOut = rayTint;
            }
            return 1.0f;
        }
        return 0.0f;
    }

    float hits = 0.0f;
    Vector3 tintSum{0.0f, 0.0f, 0.0f};
    for (int ray = 0; ray < sunParams.rayCount; ++ray) {
        const Vector3 rayDir = sampleSunRayDirection(
            toLight,
            tangent,
            bitangent,
            sunParams.angularSpreadRad,
            ray,
            sunParams.rayCount);
        Vector3 rayTint{1.0f, 1.0f, 1.0f};
        const auto hit = raycastSunWithRefraction(
            occlusionBvh,
            luxelPos,
            rayDir,
            kSunRayDistance,
            luxelFaceIndex,
            -1,
            faces,
            materialCache,
            faceTransparent,
            &rayTint);
        if (hit && faceIsSky(faceSky, hit->faceIndex)) {
            hits += 1.0f;
            tintSum = add3(tintSum, rayTint);
        }
    }
    const float visibility = hits / static_cast<float>(sunParams.rayCount);
    if (tintOut != nullptr && hits > 0.0f) {
        *tintOut = scale3(tintSum, 1.0f / hits);
    }
    if (visibility <= sunParams.leakThreshold) {
        return 0.0f;
    }
    const float leakThreshold = std::clamp(sunParams.leakThreshold, 0.0f, 0.999f);
    return (visibility - leakThreshold) / (1.0f - leakThreshold);
}

bool buildRadGpuOcclusionResources(
    const std::vector<LightmapFace>& faces,
    const std::unordered_map<std::string, MaterialBakeInfo>& materialCache,
    const std::vector<char>& faceTransparent,
    RadGpuOcclusionResources& out) {
    unloadRadGpuOcclusionResources(out);
    out = {};

    out.faceOcclusion.resize(faces.size());
    out.materialRects.push_back(
        RadGpuMaterialRect{.u0 = 0.0f,
            .v0 = 0.0f,
            .u1 = 1.0f,
            .v1 = 1.0f,
            .baseColorAlpha = 1.0f,
            .textureWidth = 1.0f,
            .textureHeight = 1.0f});
    out.materialPaths.emplace_back();

    std::unordered_map<std::string, int> materialIndexByPath;
    materialIndexByPath.emplace(std::string{}, 0);

    int atlasWidth = 1;
    int atlasHeight = 1;
    std::vector<std::pair<int, Image>> atlasLayers;
    atlasLayers.push_back({0, GenImageColor(1, 1, WHITE)});

    auto ensureMaterialIndex = [&](const std::string& materialPath) -> int {
        const auto existing = materialIndexByPath.find(materialPath);
        if (existing != materialIndexByPath.end()) {
            return existing->second;
        }
        const auto matIt = materialCache.find(materialPath);
        if (matIt == materialCache.end() || !matIt->second.hasAlbedoImage) {
            materialIndexByPath.emplace(materialPath, 0);
            return 0;
        }
        const MaterialBakeInfo& material = matIt->second;

        Image image = ImageCopy(material.albedoImage);
        ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        atlasWidth = std::max(atlasWidth, image.width);
        atlasHeight += image.height;
        const int index = static_cast<int>(out.materialRects.size());
        RadGpuMaterialRect rect{};
        rect.textureWidth = static_cast<float>(image.width);
        rect.textureHeight = static_cast<float>(image.height);
        rect.baseColorAlpha = static_cast<float>(material.asset.baseColor.a) / 255.0f;
        out.materialRects.push_back(rect);
        out.materialPaths.push_back(materialPath);
        atlasLayers.push_back({index, image});
        materialIndexByPath.emplace(materialPath, index);
        return index;
    };

    for (std::size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex) {
        const LightmapFace& face = faces[faceIndex];
        RadGpuFaceOcclusion& gpuFace = out.faceOcclusion[faceIndex];
        Vector3 uAxis{};
        Vector3 vAxis{};
        faceUvAxes(face.uvLock, face.normal, face.uvUAxis, face.uvVAxis, uAxis, vAxis);
        gpuFace.uvUAxisX = uAxis.x;
        gpuFace.uvUAxisY = uAxis.y;
        gpuFace.uvUAxisZ = uAxis.z;
        gpuFace.uvVAxisX = vAxis.x;
        gpuFace.uvVAxisY = vAxis.y;
        gpuFace.uvVAxisZ = vAxis.z;
        gpuFace.uvShiftX = face.uvShiftPixels.x;
        gpuFace.uvShiftY = face.uvShiftPixels.y;
        gpuFace.uvScaleX = face.uvScale.x;
        gpuFace.uvScaleY = face.uvScale.y;
        gpuFace.isTransparent =
            faceIsTransparentRole(faceTransparent, static_cast<std::int32_t>(faceIndex)) ? 1.0f
                                                                                         : 0.0f;

        const MaterialBakeInfo* material = materialForFace(face, materialCache);
        gpuFace.baseColorAlpha = material != nullptr
            ? static_cast<float>(material->asset.baseColor.a) / 255.0f
            : 1.0f;
        gpuFace.baseColorR = material != nullptr
            ? static_cast<float>(material->asset.baseColor.r) / 255.0f
            : 1.0f;
        gpuFace.baseColorG = material != nullptr
            ? static_cast<float>(material->asset.baseColor.g) / 255.0f
            : 1.0f;
        gpuFace.baseColorB = material != nullptr
            ? static_cast<float>(material->asset.baseColor.b) / 255.0f
            : 1.0f;
        gpuFace.ior = material != nullptr ? material->asset.ior : 1.0f;
        gpuFace.pixelsPerMeter =
            material != nullptr && material->asset.pixelsPerMeter > 0.0f
            ? material->asset.pixelsPerMeter
            : 64.0f;

        if (gpuFace.isTransparent > 0.0f && material != nullptr && material->hasAlbedoImage) {
            gpuFace.materialIndex =
                static_cast<float>(ensureMaterialIndex(face.material));
        } else {
            gpuFace.materialIndex = 0.0f;
        }
    }

    if (atlasHeight > 8192) {
        TraceLog(
            LOG_WARNING,
            "sloprad: alpha occlusion atlas too tall (%d px); GPU alpha occlusion disabled",
            atlasHeight);
        for (auto& layer : atlasLayers) {
            if (layer.first != 0) {
                UnloadImage(layer.second);
            }
        }
        UnloadImage(atlasLayers[0].second);
        out = {};
        return false;
    }

    out.alphaAtlasImage = GenImageColor(atlasWidth, atlasHeight, BLANK);
    int yOffset = 0;
    for (const auto& layer : atlasLayers) {
        const int index = layer.first;
        const Image& src = layer.second;
        ImageDrawImage(
            &out.alphaAtlasImage,
            src,
            0,
            yOffset,
            WHITE);
        RadGpuMaterialRect& rect = out.materialRects[static_cast<std::size_t>(index)];
        rect.u0 = 0.0f;
        rect.v0 = static_cast<float>(yOffset) / static_cast<float>(atlasHeight);
        rect.u1 = static_cast<float>(src.width) / static_cast<float>(atlasWidth);
        rect.v1 = static_cast<float>(yOffset + src.height) / static_cast<float>(atlasHeight);
        rect.yPixelOffset = yOffset;
        yOffset += src.height;
        if (index != 0) {
            UnloadImage(const_cast<Image&>(src));
        }
    }
    UnloadImage(atlasLayers[0].second);

    out.alphaAtlas = LoadTextureFromImage(out.alphaAtlasImage);
    if (out.alphaAtlas.id != 0) {
        SetTextureFilter(out.alphaAtlas, TEXTURE_FILTER_POINT);
        SetTextureWrap(out.alphaAtlas, TEXTURE_WRAP_CLAMP);
    }
    out.atlasWidth = atlasWidth;
    out.atlasHeight = atlasHeight;
    out.valid = out.alphaAtlas.id != 0;
    return out.valid;
}

void unloadRadGpuOcclusionResources(RadGpuOcclusionResources& resources) {
    if (resources.alphaAtlas.id != 0) {
        UnloadTexture(resources.alphaAtlas);
        resources.alphaAtlas = {};
    }
    if (resources.alphaAtlasImage.data != nullptr) {
        UnloadImage(resources.alphaAtlasImage);
        resources.alphaAtlasImage = {};
    }
    resources.faceOcclusion.clear();
    resources.materialRects.clear();
    resources.materialPaths.clear();
    resources.valid = false;
}

float sampleRadGpuAtlasAlpha(
    const RadGpuOcclusionResources& resources,
    std::int32_t materialIndex,
    float u,
    float v) {
    if (materialIndex < 0
        || materialIndex >= static_cast<std::int32_t>(resources.materialRects.size())
        || resources.alphaAtlasImage.data == nullptr) {
        return 1.0f;
    }
    const RadGpuMaterialRect& rect = resources.materialRects[static_cast<std::size_t>(materialIndex)];
    const int width = std::max(1, static_cast<int>(rect.textureWidth));
    const int height = std::max(1, static_cast<int>(rect.textureHeight));
    u = u - std::floor(u);
    v = v - std::floor(v);
    const int px = std::clamp(static_cast<int>(u * static_cast<float>(width)), 0, width - 1);
    const int py = std::clamp(static_cast<int>(v * static_cast<float>(height)), 0, height - 1);
    const int atlasX = px;
    const int atlasY = rect.yPixelOffset + py;
    if (atlasX < 0 || atlasY < 0 || atlasX >= resources.atlasWidth || atlasY >= resources.atlasHeight) {
        return 1.0f;
    }
    return static_cast<float>(GetImageColor(resources.alphaAtlasImage, atlasX, atlasY).a) / 255.0f;
}

bool verifyRadGpuOcclusionAtlas(
    const RadGpuOcclusionResources& resources,
    const std::unordered_map<std::string, MaterialBakeInfo>& materialCache) {
    if (!resources.valid || resources.alphaAtlasImage.data == nullptr) {
        return false;
    }
    if (resources.materialRects.size() != resources.materialPaths.size()) {
        return false;
    }

    constexpr int kGrid = 4;
    std::size_t mismatches = 0;
    std::size_t samples = 0;
    for (std::size_t matIndex = 1; matIndex < resources.materialRects.size(); ++matIndex) {
        const std::string& materialPath = resources.materialPaths[matIndex];
        const auto matIt = materialCache.find(materialPath);
        if (matIt == materialCache.end() || !matIt->second.hasAlbedoImage) {
            continue;
        }
        const MaterialBakeInfo& material = matIt->second;
        for (int sy = 0; sy < kGrid; ++sy) {
            for (int sx = 0; sx < kGrid; ++sx) {
                const float u = (static_cast<float>(sx) + 0.5f) / static_cast<float>(kGrid);
                const float v = (static_cast<float>(sy) + 0.5f) / static_cast<float>(kGrid);
                const float cpuAlpha = sampleImageAlpha(material.albedoImage, u, v);
                const float atlasAlpha =
                    sampleRadGpuAtlasAlpha(resources, static_cast<std::int32_t>(matIndex), u, v);
                ++samples;
                if (std::fabs(cpuAlpha - atlasAlpha) > 1.0f / 255.0f) {
                    ++mismatches;
                }
            }
        }
    }

    if (samples == 0) {
        return true;
    }
    if (mismatches > 0) {
        TraceLog(
            LOG_WARNING,
            "sloprad: GPU alpha atlas mismatch samples=%zu mismatches=%zu",
            samples,
            mismatches);
    }
    return mismatches == 0;
}

}
