#include "preview.hpp"

#include "assets/geo_loader.hpp"
#include "assets/material_loader.hpp"
#include "map/csg_compile.hpp"
#include "map/fac_io.hpp"
#include "map/lightmap.hpp"
#include "map/mover_brushes.hpp"
#include "render/skybox.hpp"
#include "render/skybox_render.hpp"

#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace slopmap {

namespace {

std::vector<slopengine::Brush> staticBrushesForPreview(
    const std::vector<slopengine::Brush>& brushes,
    const std::unordered_set<std::string>& moverBrushIds) {
    if (moverBrushIds.empty()) {
        return brushes;
    }
    std::vector<slopengine::Brush> out;
    out.reserve(brushes.size());
    for (const slopengine::Brush& brush : brushes) {
        if (moverBrushIds.count(brush.id) == 0) {
            out.push_back(brush);
        }
    }
    return out;
}

Vector3 normalizeOrDefault(Vector3 v, Vector3 fallback) {
    const float lenSq = v.x * v.x + v.y * v.y + v.z * v.z;
    if (lenSq < 1e-8f) {
        return fallback;
    }
    const float inv = 1.0f / std::sqrt(lenSq);
    return {v.x * inv, v.y * inv, v.z * inv};
}

float viewDepthAlongAxis(Vector3 point, Vector3 cameraPos, Vector3 cameraForward) {
    return Vector3DotProduct(
        Vector3Subtract(point, cameraPos),
        cameraForward);
}

float meshMinViewDepth(
    const Mesh& mesh,
    Vector3 cameraPos,
    Vector3 cameraForward) {
    const BoundingBox bounds = GetMeshBoundingBox(mesh);
    const Vector3 corners[8] = {
        {bounds.min.x, bounds.min.y, bounds.min.z},
        {bounds.max.x, bounds.min.y, bounds.min.z},
        {bounds.min.x, bounds.max.y, bounds.min.z},
        {bounds.max.x, bounds.max.y, bounds.min.z},
        {bounds.min.x, bounds.min.y, bounds.max.z},
        {bounds.max.x, bounds.min.y, bounds.max.z},
        {bounds.min.x, bounds.max.y, bounds.max.z},
        {bounds.max.x, bounds.max.y, bounds.max.z},
    };
    float minDepth = std::numeric_limits<float>::max();
    for (const Vector3& corner : corners) {
        minDepth = std::min(minDepth, viewDepthAlongAxis(corner, cameraPos, cameraForward));
    }
    return minDepth;
}

slopengine::MaterialUvInfo resolveMaterialUv(slopengine::AssetStore& assets, std::string_view materialPath) {
    slopengine::MaterialUvInfo info{};
    const slopengine::MaterialAsset* asset = assets.getMaterialAsset(materialPath);
    if (asset != nullptr) {
        info.pixelsPerMeter = asset->pixelsPerMeter;
        if (!asset->albedoTexture.empty()) {
            const Texture2D texture = assets.getTexture(asset->albedoTexture);
            if (texture.id != 0 && texture.width > 0 && texture.height > 0) {
                info.textureWidth = static_cast<float>(texture.width);
                info.textureHeight = static_cast<float>(texture.height);
            }
        }
    }
    return info;
}

std::uint32_t hashString(const std::string& value) {
    std::uint32_t hash = 2166136261u;
    for (unsigned char c : value) {
        hash ^= c;
        hash *= 16777619u;
    }
    return hash;
}

void clearMoverOverlay(Model& model, bool& valid) {
    if (valid && model.meshCount > 0) {
        UnloadModel(model);
    }
    model = {};
    valid = false;
}

void rebuildMoverOverlay(
    slopengine::AssetStore& assets,
    const std::vector<slopengine::Brush>& brushes,
    const std::unordered_set<std::string>& moverBrushIds,
    Model& model,
    bool& valid) {
    clearMoverOverlay(model, valid);
    if (moverBrushIds.empty()) {
        return;
    }
    std::vector<slopengine::Brush> movers;
    movers.reserve(moverBrushIds.size());
    for (const slopengine::Brush& brush : brushes) {
        if (moverBrushIds.count(brush.id) > 0) {
            movers.push_back(brush);
        }
    }
    if (movers.empty()) {
        return;
    }
    const slopengine::CsgCompileResult compiled = slopengine::compileBrushesToGeo(
        movers,
        [&assets](std::string_view materialPath) { return resolveMaterialUv(assets, materialPath); },
        nullptr);
    model = slopengine::buildModelFromGeo(
        compiled.asset,
        compiled.buffer,
        [&assets](std::string_view path) { return assets.resolveMaterial(path); });
    valid = model.meshCount > 0;
    if (!valid) {
        clearMoverOverlay(model, valid);
    }
}

unsigned char mixChannel(unsigned char base, std::uint32_t hash, int shift, int spread) {
    const int delta = static_cast<int>((hash >> shift) & 0xFFu) % (spread * 2 + 1) - spread;
    const int value = static_cast<int>(base) + delta;
    if (value < 40) {
        return 40;
    }
    if (value > 255) {
        return 255;
    }
    return static_cast<unsigned char>(value);
}

void collectTransparentMeshIndices(
    const slopengine::GeoAsset& asset,
    std::vector<int>& out) {
    out.clear();
    for (std::size_t meshIndex = 0; meshIndex < asset.primitives.size(); ++meshIndex) {
        if (asset.primitives[meshIndex].transparent) {
            out.push_back(static_cast<int>(meshIndex));
        }
    }
}

void collectSkyMeshIndices(
    slopengine::AssetStore& assets,
    const slopengine::GeoAsset& asset,
    std::vector<int>& out) {
    out.clear();
    for (std::size_t meshIndex = 0; meshIndex < asset.primitives.size(); ++meshIndex) {
        const slopengine::MaterialAsset* materialAsset =
            assets.getMaterialAsset(asset.primitives[meshIndex].material);
        if (materialAsset != nullptr && materialAsset->sky) {
            out.push_back(static_cast<int>(meshIndex));
        }
    }
}

void drawModelMeshesSplit(
    const Model& model,
    const std::vector<int>& transparentMeshIndices,
    const std::vector<int>& skyMeshIndices,
    bool transparentPass) {
    if (model.meshCount <= 0) {
        return;
    }
    std::unordered_set<int> transparentSet(
        transparentMeshIndices.begin(),
        transparentMeshIndices.end());
    std::unordered_set<int> skySet(skyMeshIndices.begin(), skyMeshIndices.end());
    for (int meshIndex = 0; meshIndex < model.meshCount; ++meshIndex) {
        const bool isTransparent = transparentSet.count(meshIndex) > 0;
        if (isTransparent != transparentPass) {
            continue;
        }
        if (!transparentPass && skySet.count(meshIndex) > 0) {
            continue;
        }
        DrawMesh(model.meshes[meshIndex], model.materials[meshIndex], MatrixIdentity());
    }
}

void drawPreviewModelTextured(
    const Model& model,
    const std::vector<int>& transparentMeshIndices,
    const std::vector<int>& skyMeshIndices,
    Vector3 cameraPos,
    Vector3 cameraForward) {
    drawModelMeshesSplit(model, transparentMeshIndices, skyMeshIndices, false);
    if (transparentMeshIndices.empty()) {
        return;
    }
    const Vector3 camForward = normalizeOrDefault(cameraForward, {0.0f, 0.0f, 1.0f});
    struct SortItem {
        int meshIndex = 0;
        float viewDepth = 0.0f;
    };
    std::vector<SortItem> sorted;
    sorted.reserve(transparentMeshIndices.size());
    for (int meshIndex : transparentMeshIndices) {
        if (meshIndex < 0 || meshIndex >= model.meshCount) {
            continue;
        }
        sorted.push_back(SortItem{
            meshIndex,
            meshMinViewDepth(model.meshes[meshIndex], cameraPos, camForward),
        });
    }
    std::sort(sorted.begin(), sorted.end(), [](const SortItem& a, const SortItem& b) {
        return a.viewDepth > b.viewDepth;
    });
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);
    for (const SortItem& item : sorted) {
        DrawMesh(
            model.meshes[item.meshIndex],
            model.materials[item.meshIndex],
            MatrixIdentity());
    }
    EndBlendMode();
    rlEnableDepthMask();
}

struct FaceSolidInfo {
    slopengine::BrushRole role = slopengine::BrushRole::Hull;
    std::string brushId;
};

Color solidBaseColor(const FaceSolidInfo& info) {
    const std::uint32_t hash = hashString(info.brushId);
    if (info.role == slopengine::BrushRole::Hull) {
        return Color{
            mixChannel(70, hash, 0, 35),
            mixChannel(190, hash, 8, 45),
            mixChannel(90, hash, 16, 35),
            255,
        };
    }
    if (info.role == slopengine::BrushRole::Window) {
        return Color{
            mixChannel(140, hash, 0, 35),
            mixChannel(200, hash, 8, 40),
            mixChannel(220, hash, 16, 35),
            255,
        };
    }
    if (info.role == slopengine::BrushRole::Transparent) {
        return Color{
            mixChannel(200, hash, 0, 35),
            mixChannel(90, hash, 8, 40),
            mixChannel(210, hash, 16, 35),
            180,
        };
    }
    return Color{
        mixChannel(70, hash, 0, 35),
        mixChannel(120, hash, 8, 40),
        mixChannel(210, hash, 16, 35),
        255,
    };
}

Color applyFauxShade(Color base, Vector3 normal) {
    const Vector3 lightDir = Vector3Normalize(Vector3{0.45f, 0.85f, 0.35f});
    const float nLen = Vector3Length(normal);
    Vector3 n = nLen > 1e-6f ? Vector3Scale(normal, 1.0f / nLen) : Vector3{0.0f, 1.0f, 0.0f};
    float ndotl = Vector3DotProduct(n, lightDir);
    if (ndotl < 0.2f) {
        ndotl = 0.2f;
    }
    if (ndotl > 1.0f) {
        ndotl = 1.0f;
    }
    return Color{
        static_cast<unsigned char>(std::lround(static_cast<float>(base.r) * ndotl)),
        static_cast<unsigned char>(std::lround(static_cast<float>(base.g) * ndotl)),
        static_cast<unsigned char>(std::lround(static_cast<float>(base.b) * ndotl)),
        255,
    };
}

void drawEditModelSolid(
    const Model& model,
    const std::vector<std::string>& faceIds,
    const std::vector<slopengine::Brush>& brushes,
    const std::vector<slopengine::Brush>& instanceBrushes) {
    if (model.meshCount <= 0) {
        return;
    }

    std::unordered_map<std::string, FaceSolidInfo> faceInfo;
    auto indexBrush = [&](const slopengine::Brush& brush) {
        for (const slopengine::BrushFace& face : brush.faces) {
            faceInfo[face.id] = FaceSolidInfo{brush.role, brush.id};
        }
    };
    for (const slopengine::Brush& brush : brushes) {
        indexBrush(brush);
    }
    for (const slopengine::Brush& brush : instanceBrushes) {
        indexBrush(brush);
    }

    Material flat = LoadMaterialDefault();
    for (int meshIndex = 0; meshIndex < model.meshCount; ++meshIndex) {
        FaceSolidInfo info{};
        if (meshIndex < static_cast<int>(faceIds.size())) {
            const auto it = faceInfo.find(faceIds[static_cast<std::size_t>(meshIndex)]);
            if (it != faceInfo.end()) {
                info = it->second;
            }
        }
        Color tint = solidBaseColor(info);
        const Mesh& mesh = model.meshes[meshIndex];
        if (mesh.normals != nullptr && mesh.vertexCount > 0) {
            const Vector3 normal{
                mesh.normals[0],
                mesh.normals[1],
                mesh.normals[2],
            };
            tint = applyFauxShade(tint, normal);
        }
        flat.maps[MATERIAL_MAP_ALBEDO].color = tint;
        DrawMesh(mesh, flat, MatrixIdentity());
    }
    UnloadMaterial(flat);
}

} // namespace

void MapPreview::clearLit() {
    if (litValid && litModel.meshCount > 0) {
        for (int materialIndex = 0; materialIndex < litModel.materialCount; ++materialIndex) {
            Material& material = litModel.materials[materialIndex];
            if (material.maps != nullptr) {
                material.maps[MATERIAL_MAP_METALNESS].texture = {};
            }
            material.shader = {};
        }
        UnloadModel(litModel);
    }
    litModel = {};
    litValid = false;

    for (Texture2D& atlas : lightmapAtlases) {
        if (atlas.id != 0) {
            UnloadTexture(atlas);
        }
    }
    lightmapAtlases.clear();

    if (lightmapShader.id != 0) {
        UnloadShader(lightmapShader);
        lightmapShader = {};
    }
    useLightmapLoc = -1;
    solidLitLoc = -1;
    transparentMeshIndices.clear();
    skyMeshIndices.clear();
    if (skyShader.id != 0) {
        UnloadShader(skyShader);
        skyShader = {};
    }
    rad = {};
    if (!visValid) {
        pickFac = {};
    }
}

void MapPreview::clearVis() {
    if (visValid && visModel.meshCount > 0) {
        UnloadModel(visModel);
    }
    visModel = {};
    visValid = false;
    transparentMeshIndices.clear();
    if (!litValid) {
        pickFac = {};
    }
}

void MapPreview::clear() {
    if (valid && model.meshCount > 0) {
        UnloadModel(model);
    }
    model = {};
    valid = false;
    editFaceIds.clear();
    clearVis();
    clearLit();
    clearMoverOverlay(moverOverlayModel, moverOverlayValid);
}

void MapPreview::rebuild(slopengine::AssetStore& assets, const std::vector<slopengine::Brush>& brushes) {
    if (valid && model.meshCount > 0) {
        UnloadModel(model);
    }
    model = {};
    valid = false;
    editFaceIds.clear();
    if (brushes.empty()) {
        return;
    }

    const slopengine::CsgCompileResult compiled = slopengine::compileBrushesToGeo(
        brushes,
        [&assets](std::string_view materialPath) { return resolveMaterialUv(assets, materialPath); });

    editFaceIds.reserve(compiled.asset.primitives.size());
    for (const auto& primitive : compiled.asset.primitives) {
        editFaceIds.push_back(primitive.name);
    }

    model = slopengine::buildModelFromGeo(
        compiled.asset,
        compiled.buffer,
        [&assets](std::string_view path) { return assets.resolveMaterial(path); });

    collectTransparentMeshIndices(compiled.asset, transparentMeshIndices);
    collectSkyMeshIndices(assets, compiled.asset, skyMeshIndices);
    valid = model.meshCount > 0;
}

bool MapPreview::reloadVisPreview(
    slopengine::AssetStore& assets,
    const std::string& mapName,
    const std::vector<slopengine::Brush>& brushes,
    const std::unordered_set<std::string>& moverBrushIds) {
    clearVis();
    if (mapName.empty() || mapName == "untitled") {
        return false;
    }

    const auto resolveUv =
        [&assets](std::string_view materialPath) { return resolveMaterialUv(assets, materialPath); };

    const std::string visVirtualPath = mapName + "/static";
    slopengine::CsgCompileResult compiled{};
    if (assets.hasMapFac(visVirtualPath)) {
        if (const auto visPath = assets.resolvePath(slopengine::AssetKind::MapFac, visVirtualPath)) {
            if (auto loadedFac = slopengine::readFacFile(*visPath)) {
                if (!loadedFac->faces.empty()) {
                    pickFac = *loadedFac;
                    slopengine::eraseFacFacesForMoverBrushes(pickFac, moverBrushIds);
                    compiled = slopengine::compileVisibleFacesToGeo(pickFac, resolveUv, nullptr);
                }
            }
        }
    }
    if (compiled.asset.primitives.empty()) {
        pickFac = {};
        compiled = slopengine::compileBrushesToGeo(
            staticBrushesForPreview(brushes, moverBrushIds),
            resolveUv,
            nullptr);
    }

    visModel = slopengine::buildModelFromGeo(
        compiled.asset,
        compiled.buffer,
        [&assets](std::string_view path) { return assets.resolveMaterial(path); });
    collectTransparentMeshIndices(compiled.asset, transparentMeshIndices);
    collectSkyMeshIndices(assets, compiled.asset, skyMeshIndices);
    visValid = visModel.meshCount > 0;
    if (!visValid) {
        clearVis();
        return false;
    }
    rebuildMoverOverlay(assets, brushes, moverBrushIds, moverOverlayModel, moverOverlayValid);
    return true;
}

bool MapPreview::reloadBake(
    slopengine::AssetStore& assets,
    const std::string& mapName,
    const std::vector<slopengine::Brush>& brushes,
    const std::unordered_set<std::string>& moverBrushIds) {
    clearLit();
    if (mapName.empty() || mapName == "untitled") {
        return false;
    }

    const std::string radVirtualPath = mapName + "/rad/static";
    if (!assets.hasMapRad(radVirtualPath)) {
        return false;
    }
    const auto radPath = assets.resolvePath(slopengine::AssetKind::MapRad, radVirtualPath);
    if (!radPath) {
        return false;
    }
    auto loadedRad = slopengine::readRadFile(*radPath);
    if (!loadedRad || loadedRad->charts.empty() || loadedRad->atlases.empty()) {
        return false;
    }
    rad = std::move(*loadedRad);

    lightmapShader = slopengine::loadLightmapShader(assets, useLightmapLoc);
    if (lightmapShader.id == 0) {
        rad = {};
        return false;
    }
    slopengine::applyLightmapEncoding(lightmapShader, slopengine::primaryLightmapEncoding(rad));
    skyShader = slopengine::loadSkyFaceShader(assets);
    solidLitLoc = GetShaderLocation(lightmapShader, "solidLit");
    if (solidLitLoc >= 0) {
        const int solidLit = 0;
        SetShaderValue(lightmapShader, solidLitLoc, &solidLit, SHADER_UNIFORM_INT);
    }

    lightmapAtlases.reserve(rad.atlases.size());
    for (const slopengine::LightmapAtlasInfo& atlas : rad.atlases) {
        const std::string atlasPath = mapName + "/rad/" + atlas.texturePath;
        const auto resolved = assets.resolvePath(slopengine::AssetKind::MapLightmap, atlasPath);
        Texture2D texture{};
        if (resolved) {
            texture = LoadTexture(resolved->string().c_str());
            if (texture.id != 0) {
                const int filter = atlas.encoding == slopengine::LightmapEncoding::Rgbe
                    ? TEXTURE_FILTER_POINT
                    : TEXTURE_FILTER_BILINEAR;
                SetTextureFilter(texture, filter);
                SetTextureWrap(texture, TEXTURE_WRAP_CLAMP);
            }
        }
        lightmapAtlases.push_back(texture);
    }

    const auto resolveUv =
        [&assets](std::string_view materialPath) { return resolveMaterialUv(assets, materialPath); };

    slopengine::FacFile vis{};
    bool haveVis = false;
    const std::string visVirtualPath = mapName + "/static";
    if (assets.hasMapFac(visVirtualPath)) {
        if (const auto visPath = assets.resolvePath(slopengine::AssetKind::MapFac, visVirtualPath)) {
            if (auto loadedFac = slopengine::readFacFile(*visPath)) {
                vis = std::move(*loadedFac);
                haveVis = true;
            }
        }
    }

    if (haveVis) {
        pickFac = vis;
    } else if (!visValid) {
        pickFac = {};
    }
    const slopengine::CsgCompileResult compiled = haveVis
        ? slopengine::compileVisibleFacesToGeo(vis, resolveUv, &rad)
        : slopengine::compileBrushesToGeo(
              staticBrushesForPreview(brushes, moverBrushIds),
              resolveUv,
              &rad);

    std::unordered_map<std::string, std::int32_t> faceAtlasById;
    for (const slopengine::LightmapChart& chart : rad.charts) {
        faceAtlasById[chart.faceId] = chart.atlasIndex;
    }

    litModel = slopengine::buildModelFromGeo(
        compiled.asset,
        compiled.buffer,
        [&](std::string_view path) {
            Material material = assets.resolveMaterial(path);
            material.shader = lightmapShader;
            if (!lightmapAtlases.empty() && lightmapAtlases[0].id != 0) {
                SetMaterialTexture(&material, MATERIAL_MAP_METALNESS, lightmapAtlases[0]);
            }
            return material;
        });

    if (litModel.meshCount > 0) {
        for (int meshIndex = 0; meshIndex < litModel.meshCount; ++meshIndex) {
            const std::string& faceId =
                compiled.asset.primitives[static_cast<std::size_t>(meshIndex)].name;
            std::int32_t atlasIndex = 0;
            const auto atlasIt = faceAtlasById.find(faceId);
            if (atlasIt != faceAtlasById.end()) {
                atlasIndex = atlasIt->second;
            }
            if (atlasIndex >= 0 &&
                atlasIndex < static_cast<std::int32_t>(lightmapAtlases.size())) {
                const Texture2D lightmap = lightmapAtlases[static_cast<std::size_t>(atlasIndex)];
                if (lightmap.id != 0) {
                    SetMaterialTexture(&litModel.materials[meshIndex], MATERIAL_MAP_METALNESS, lightmap);
                }
            }
            litModel.materials[meshIndex].shader = lightmapShader;
        }
        collectTransparentMeshIndices(compiled.asset, transparentMeshIndices);
        collectSkyMeshIndices(assets, compiled.asset, skyMeshIndices);
        for (int meshIndex : skyMeshIndices) {
            if (meshIndex >= 0 && meshIndex < litModel.meshCount && skyShader.id != 0) {
                litModel.materials[meshIndex].shader = skyShader;
            }
        }
        litValid = true;
    } else {
        clearLit();
        return false;
    }

    if (useLightmapLoc >= 0) {
        const int useLightmap = 1;
        SetShaderValue(lightmapShader, useLightmapLoc, &useLightmap, SHADER_UNIFORM_INT);
    }
    rebuildMoverOverlay(assets, brushes, moverBrushIds, moverOverlayModel, moverOverlayValid);
    return litValid;
}

Color brushOutlineColor(const slopengine::Brush& brush, bool selected) {
    if (selected) {
        return Color{255, 140, 40, 255};
    }

    const std::uint32_t hash = hashString(brush.id);
    if (brush.role != slopengine::BrushRole::Hull &&
        brush.role != slopengine::BrushRole::Window &&
        brush.role != slopengine::BrushRole::Transparent) {
        return Color{
            mixChannel(70, hash, 0, 35),
            mixChannel(120, hash, 8, 40),
            mixChannel(210, hash, 16, 35),
            255,
        };
    }

    if (brush.role == slopengine::BrushRole::Window) {
        return Color{
            mixChannel(140, hash, 0, 35),
            mixChannel(200, hash, 8, 40),
            mixChannel(220, hash, 16, 35),
            255,
        };
    }

    if (brush.role == slopengine::BrushRole::Transparent) {
        return Color{
            mixChannel(220, hash, 0, 35),
            mixChannel(100, hash, 8, 40),
            mixChannel(230, hash, 16, 35),
            255,
        };
    }

    return Color{
        mixChannel(70, hash, 0, 35),
        mixChannel(190, hash, 8, 45),
        mixChannel(90, hash, 16, 35),
        255,
    };
}

void drawThickLine3D(
    Vector3 a,
    Vector3 b,
    Color color,
    float width,
    Vector3 eye,
    Vector3 viewDir) {
    const Vector3 delta{b.x - a.x, b.y - a.y, b.z - a.z};
    const float lenSq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    if (lenSq < 1e-12f || width <= 0.0f) {
        return;
    }
    const float len = std::sqrt(lenSq);
    const Vector3 dir{delta.x / len, delta.y / len, delta.z / len};
    const Vector3 mid{
        0.5f * (a.x + b.x),
        0.5f * (a.y + b.y),
        0.5f * (a.z + b.z),
    };
    const float viewLenSq =
        viewDir.x * viewDir.x + viewDir.y * viewDir.y + viewDir.z * viewDir.z;
    Vector3 toEye{};
    if (viewLenSq > 1e-12f) {
        const float viewLen = std::sqrt(viewLenSq);
        toEye = {-viewDir.x / viewLen, -viewDir.y / viewLen, -viewDir.z / viewLen};
    } else {
        toEye = {eye.x - mid.x, eye.y - mid.y, eye.z - mid.z};
        float toEyeLen = std::sqrt(toEye.x * toEye.x + toEye.y * toEye.y + toEye.z * toEye.z);
        if (toEyeLen < 1e-6f) {
            toEye = {0.0f, 1.0f, 0.0f};
        } else {
            toEye = {toEye.x / toEyeLen, toEye.y / toEyeLen, toEye.z / toEyeLen};
        }
    }

    Vector3 side{
        dir.y * toEye.z - dir.z * toEye.y,
        dir.z * toEye.x - dir.x * toEye.z,
        dir.x * toEye.y - dir.y * toEye.x,
    };
    float sideLen = std::sqrt(side.x * side.x + side.y * side.y + side.z * side.z);
    if (sideLen < 1e-5f) {
        const Vector3 fallback = std::fabs(dir.y) < 0.9f ? Vector3{0.0f, 1.0f, 0.0f}
                                                         : Vector3{1.0f, 0.0f, 0.0f};
        side = {
            dir.y * fallback.z - dir.z * fallback.y,
            dir.z * fallback.x - dir.x * fallback.z,
            dir.x * fallback.y - dir.y * fallback.x,
        };
        sideLen = std::sqrt(side.x * side.x + side.y * side.y + side.z * side.z);
        if (sideLen < 1e-5f) {
            DrawLine3D(a, b, color);
            return;
        }
    }
    const float half = width * 0.5f / sideLen;
    side = {side.x * half, side.y * half, side.z * half};

    const Vector3 a1{a.x + side.x, a.y + side.y, a.z + side.z};
    const Vector3 a2{a.x - side.x, a.y - side.y, a.z - side.z};
    const Vector3 b1{b.x + side.x, b.y + side.y, b.z + side.z};
    const Vector3 b2{b.x - side.x, b.y - side.y, b.z - side.z};
    DrawTriangle3D(a1, b1, b2, color);
    DrawTriangle3D(a1, b2, a2, color);
    DrawTriangle3D(a1, b2, b1, color);
    DrawTriangle3D(a1, a2, b2, color);
}

void drawBrushFaceOutlines(
    const slopengine::Brush& brush,
    Color color,
    Vector3 eye,
    float lineWidth) {
    for (const slopengine::BrushFace& face : brush.faces) {
        if (face.vertices.size() < 2) {
            continue;
        }
        for (std::size_t i = 0; i < face.vertices.size(); ++i) {
            const Vector3& a = face.vertices[i];
            const Vector3& b = face.vertices[(i + 1) % face.vertices.size()];
            drawThickLine3D(a, b, color, lineWidth, eye);
        }
    }
}

void drawBrushFaceOutlinesXray(const slopengine::Brush& brush, Color color) {
    for (const slopengine::BrushFace& face : brush.faces) {
        if (face.vertices.size() < 2) {
            continue;
        }
        for (std::size_t i = 0; i < face.vertices.size(); ++i) {
            const Vector3& a = face.vertices[i];
            const Vector3& b = face.vertices[(i + 1) % face.vertices.size()];
            DrawLine3D(a, b, color);
        }
    }
}

void MapPreview::draw(
    PreviewFill fill,
    WireframeOverlay wireframe,
    const std::vector<slopengine::Brush>& brushes,
    const std::vector<slopengine::Brush>& instanceBrushes,
    const std::vector<int>& selectedBrushes,
    Vector3 eye,
    Vector3 cameraForward,
    float lineWidth,
    const Camera3D* camera,
    slopengine::AssetStore* assets,
    const std::vector<slopengine::Thing>* things) const {
    auto outlineBrush = [&](const slopengine::Brush& brush, bool selected) {
        drawBrushFaceOutlines(brush, brushOutlineColor(brush, selected), eye, lineWidth);
    };

    switch (fill) {
    case PreviewFill::Wireframe:
        for (std::size_t i = 0; i < brushes.size(); ++i) {
            const bool selected =
                std::find(selectedBrushes.begin(), selectedBrushes.end(), static_cast<int>(i)) !=
                selectedBrushes.end();
            outlineBrush(brushes[i], selected);
        }
        break;
    case PreviewFill::Lit:
    case PreviewFill::SolidLit:
        if (litValid) {
            if (solidLitLoc >= 0) {
                const int solidLit = fill == PreviewFill::SolidLit ? 1 : 0;
                SetShaderValue(lightmapShader, solidLitLoc, &solidLit, SHADER_UNIFORM_INT);
            }
            slopengine::bindLightmapDummyShadowMaps(lightmapShader);
            drawModelMeshesSplit(litModel, transparentMeshIndices, skyMeshIndices, false);
            drawPreviewModelTextured(litModel, transparentMeshIndices, skyMeshIndices, eye, cameraForward);
            if (camera != nullptr && assets != nullptr && things != nullptr && skyShader.id != 0) {
                const slopengine::SkyboxSettings* skySettings = nullptr;
                slopengine::SkyboxSettings localSettings{};
                for (const slopengine::Thing& thing : *things) {
                    if (thing.kind == slopengine::ThingKind::Skybox) {
                        localSettings = slopengine::skyboxSettingsFromThing(thing, assets);
                        skySettings = &localSettings;
                        break;
                    }
                }
                if (skySettings != nullptr && !skyMeshIndices.empty()) {
                    slopengine::SkyboxShaderState& shaderState =
                        slopengine::ensureSkyboxShaders(*assets);
                    slopengine::applySkyShaderUniforms(
                        skyShader, *assets, shaderState, *skySettings);
                    const Matrix viewRot = [&]() {
                        Matrix view = MatrixLookAt(camera->position, camera->target, camera->up);
                        view.m12 = 0.0f;
                        view.m13 = 0.0f;
                        view.m14 = 0.0f;
                        return view;
                    }();
                    const float cameraPos[3] = {
                        camera->position.x,
                        camera->position.y,
                        camera->position.z,
                    };
                    const int cameraPosLoc = GetShaderLocation(skyShader, "cameraPos");
                    const int matViewRotLoc = GetShaderLocation(skyShader, "matViewRot");
                    SetShaderValue(skyShader, cameraPosLoc, cameraPos, SHADER_UNIFORM_VEC3);
                    if (matViewRotLoc >= 0) {
                        SetShaderValueMatrix(skyShader, matViewRotLoc, viewRot);
                    }
                    for (int meshIndex : skyMeshIndices) {
                        if (meshIndex < 0 || meshIndex >= litModel.meshCount) {
                            continue;
                        }
                        Material& material = litModel.materials[meshIndex];
                        material.shader = skyShader;
                        DrawMesh(litModel.meshes[meshIndex], material, MatrixIdentity());
                    }
                }
            }
            if (moverOverlayValid) {
                DrawModel(moverOverlayModel, {0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
            }
            break;
        }
        [[fallthrough]];
    case PreviewFill::Unlit:
        if (visValid) {
            drawModelMeshesSplit(visModel, transparentMeshIndices, skyMeshIndices, false);
            drawPreviewModelTextured(visModel, transparentMeshIndices, skyMeshIndices, eye, cameraForward);
            if (moverOverlayValid) {
                DrawModel(moverOverlayModel, {0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
            }
            break;
        }
        [[fallthrough]];
    case PreviewFill::Textures:
        if (valid) {
            drawModelMeshesSplit(model, transparentMeshIndices, skyMeshIndices, false);
            drawPreviewModelTextured(model, transparentMeshIndices, skyMeshIndices, eye, cameraForward);
        }
        break;
    case PreviewFill::Solid:
        if (valid) {
            drawEditModelSolid(model, editFaceIds, brushes, instanceBrushes);
        }
        break;
    }

    if (fill == PreviewFill::Wireframe || wireframe == WireframeOverlay::Off) {
        return;
    }

    rlDrawRenderBatchActive();
    if (wireframe == WireframeOverlay::All) {
        rlDisableDepthTest();
        rlDisableDepthMask();
        rlDisableBackfaceCulling();
        for (std::size_t i = 0; i < brushes.size(); ++i) {
            const bool selected =
                std::find(selectedBrushes.begin(), selectedBrushes.end(), static_cast<int>(i)) !=
                selectedBrushes.end();
            drawBrushFaceOutlinesXray(
                brushes[i],
                selected ? Color{255, 180, 60, 255} : Color{220, 220, 230, 255});
        }
        for (const slopengine::Brush& brush : instanceBrushes) {
            drawBrushFaceOutlinesXray(brush, Color{180, 200, 220, 255});
        }
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        rlEnableDepthMask();
        rlEnableDepthTest();
    } else if (wireframe == WireframeOverlay::Visible) {
        for (std::size_t i = 0; i < brushes.size(); ++i) {
            const bool selected =
                std::find(selectedBrushes.begin(), selectedBrushes.end(), static_cast<int>(i)) !=
                selectedBrushes.end();
            outlineBrush(brushes[i], selected);
        }
        for (const slopengine::Brush& brush : instanceBrushes) {
            outlineBrush(brush, false);
        }
        rlDrawRenderBatchActive();
    }
}

void drawPlaneCrosshair(
    Vector3 center,
    Vector3 axisU,
    Vector3 axisV,
    float halfLength,
    Color uColor,
    Color vColor,
    Vector3 eye,
    float lineWidth) {
    if (halfLength <= 0.0f || lineWidth <= 0.0f) {
        return;
    }
    const Vector3 u = Vector3Scale(axisU, halfLength);
    const Vector3 v = Vector3Scale(axisV, halfLength);
    drawThickLine3D(Vector3Subtract(center, u), Vector3Add(center, u), uColor, lineWidth, eye);
    drawThickLine3D(Vector3Subtract(center, v), Vector3Add(center, v), vColor, lineWidth, eye);
}

void drawPlaneQuad(
    Vector3 center,
    Vector3 axisU,
    Vector3 axisV,
    float halfExtent,
    Color fill) {
    if (halfExtent <= 0.0f) {
        return;
    }
    const Vector3 u = Vector3Scale(axisU, halfExtent);
    const Vector3 v = Vector3Scale(axisV, halfExtent);
    const Vector3 c0 = Vector3Subtract(Vector3Subtract(center, u), v);
    const Vector3 c1 = Vector3Add(Vector3Subtract(center, u), v);
    const Vector3 c2 = Vector3Add(Vector3Add(center, u), v);
    const Vector3 c3 = Vector3Add(Vector3Subtract(center, u), v);
    DrawTriangle3D(c0, c1, c2, fill);
    DrawTriangle3D(c0, c2, c3, fill);
    DrawTriangle3D(c0, c2, c1, fill);
    DrawTriangle3D(c0, c3, c2, fill);
}

void drawDirectionArrow(
    Vector3 origin,
    Vector3 direction,
    float length,
    Color color,
    Vector3 eye,
    float lineWidth) {
    const float dirLen = Vector3Length(direction);
    if (dirLen < 1e-6f || length <= 0.0f || lineWidth <= 0.0f) {
        return;
    }
    const Vector3 dir = Vector3Scale(direction, 1.0f / dirLen);
    const Vector3 tip = Vector3Add(origin, Vector3Scale(dir, length));
    const float shaftWidth = lineWidth * 1.35f;
    drawThickLine3D(origin, tip, color, shaftWidth, eye);

    const float headLen = std::clamp(length * 0.18f, lineWidth * 4.0f, length * 0.45f);
    const Vector3 back = Vector3Scale(dir, -headLen);
    Vector3 side = Vector3CrossProduct(
        dir,
        std::fabs(dir.y) < 0.9f ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f});
    const float sideLen = Vector3Length(side);
    if (sideLen < 1e-6f) {
        return;
    }
    side = Vector3Scale(side, headLen * 0.45f / sideLen);
    const Vector3 wingA = Vector3Add(tip, Vector3Add(back, side));
    const Vector3 wingB = Vector3Add(tip, Vector3Subtract(back, side));
    drawThickLine3D(tip, wingA, color, shaftWidth * 0.85f, eye);
    drawThickLine3D(tip, wingB, color, shaftWidth * 0.85f, eye);
}

void drawConstructionPlaneGizmo(
    Vector3 point,
    Vector3 axisU,
    Vector3 axisV,
    Vector3 normal,
    Vector3 eye,
    float lineWidth,
    float normalArrowLength) {
    constexpr Color kUCross{255, 90, 90, 255};
    constexpr Color kVCross{90, 255, 90, 255};
    constexpr Color kNormalArrow{80, 160, 255, 255};
    const float crossHalf = std::max(0.12f, lineWidth * 10.0f);
    const float planeHalf = std::max(crossHalf * 1.8f, 0.18f);
    drawPlaneQuad(point, axisU, axisV, planeHalf, Color{255, 255, 255, 22});
    drawPlaneCrosshair(point, axisU, axisV, crossHalf, kUCross, kVCross, eye, lineWidth);
    if (normalArrowLength > 0.0f) {
        drawDirectionArrow(point, normal, normalArrowLength, kNormalArrow, eye, lineWidth);
    }
}

void drawAabbWires(Vector3 mins, Vector3 maxs, Color color) {
    const Vector3 center{
        0.5f * (mins.x + maxs.x),
        0.5f * (mins.y + maxs.y),
        0.5f * (mins.z + maxs.z),
    };
    const Vector3 size{
        std::fabs(maxs.x - mins.x),
        std::fabs(maxs.y - mins.y),
        std::fabs(maxs.z - mins.z),
    };
    DrawCubeWires(center, size.x, size.y, size.z, color);
}

void drawAabbSolid(Vector3 mins, Vector3 maxs, Color color) {
    const Vector3 center{
        0.5f * (mins.x + maxs.x),
        0.5f * (mins.y + maxs.y),
        0.5f * (mins.z + maxs.z),
    };
    const Vector3 size{
        std::fabs(maxs.x - mins.x),
        std::fabs(maxs.y - mins.y),
        std::fabs(maxs.z - mins.z),
    };
    DrawCube(center, size.x, size.y, size.z, color);
}

void drawBrushAabbWires(const slopengine::Brush& brush, Color color) {
    drawAabbWires(brush.mins, brush.maxs, color);
}

bool InfiniteGrid::load(slopengine::AssetStore& assets) {
    unload();
    const std::string vert = assets.getShaderSource("tools/grid_vert");
    const std::string frag = assets.getShaderSource("tools/grid_frag");
    if (vert.empty() || frag.empty()) {
        TraceLog(LOG_WARNING, "slopmap: missing tools/grid shaders");
        return false;
    }
    shader = LoadShaderFromMemory(vert.c_str(), frag.c_str());
    if (shader.id == 0) {
        TraceLog(LOG_WARNING, "slopmap: failed to compile tools/grid shaders");
        return false;
    }
    shader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(shader, "mvp");
    shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shader, "matModel");
    shader.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(shader, "vertexPosition");
    cameraPosLoc = GetShaderLocation(shader, "cameraPos");
    gridSizeLoc = GetShaderLocation(shader, "gridSize");
    planeAxisLoc = GetShaderLocation(shader, "planeAxis");
    fadeRadiusLoc = GetShaderLocation(shader, "fadeRadius");
    minorColorLoc = GetShaderLocation(shader, "minorColor");
    majorColorLoc = GetShaderLocation(shader, "majorColor");
    return true;
}

void InfiniteGrid::unload() {
    if (shader.id != 0) {
        UnloadShader(shader);
        shader = {};
    }
    cameraPosLoc = -1;
    gridSizeLoc = -1;
    planeAxisLoc = -1;
    fadeRadiusLoc = -1;
    minorColorLoc = -1;
    majorColorLoc = -1;
}

bool InfiniteGrid::ready() const {
    return shader.id != 0;
}

void InfiniteGrid::draw(GridPlane plane, Vector3 eye, float gridSize, float fadeRadius) const {
    if (!ready() || fadeRadius <= 1e-4f || gridSize <= 0.0f) {
        return;
    }

    Vector3 center{};
    int axis = 0;
    switch (plane) {
    case GridPlane::XY:
        center = {eye.x, eye.y, 0.0f};
        axis = 1;
        break;
    case GridPlane::YZ:
        center = {0.0f, eye.y, eye.z};
        axis = 2;
        break;
    case GridPlane::XZ:
    default:
        center = {eye.x, 0.0f, eye.z};
        axis = 0;
        break;
    }

    const float ext = fadeRadius;
    Vector3 p0{};
    Vector3 p1{};
    Vector3 p2{};
    Vector3 p3{};
    switch (plane) {
    case GridPlane::XY:
        p0 = {center.x - ext, center.y - ext, center.z};
        p1 = {center.x + ext, center.y - ext, center.z};
        p2 = {center.x + ext, center.y + ext, center.z};
        p3 = {center.x - ext, center.y + ext, center.z};
        break;
    case GridPlane::YZ:
        p0 = {center.x, center.y - ext, center.z - ext};
        p1 = {center.x, center.y - ext, center.z + ext};
        p2 = {center.x, center.y + ext, center.z + ext};
        p3 = {center.x, center.y + ext, center.z - ext};
        break;
    case GridPlane::XZ:
    default:
        p0 = {center.x - ext, center.y, center.z - ext};
        p1 = {center.x + ext, center.y, center.z - ext};
        p2 = {center.x + ext, center.y, center.z + ext};
        p3 = {center.x - ext, center.y, center.z + ext};
        break;
    }

    const float cam[3] = {eye.x, eye.y, eye.z};
    const float size = std::max(gridSize, 0.001f);
    const float fade = fadeRadius;
    const float minor[4] = {70.0f / 255.0f, 74.0f / 255.0f, 80.0f / 255.0f, 0.45f};
    const float major[4] = {96.0f / 255.0f, 102.0f / 255.0f, 112.0f / 255.0f, 0.75f};

    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();
    BeginShaderMode(shader);
    if (shader.locs[SHADER_LOC_MATRIX_MODEL] >= 0) {
        const Matrix identity = MatrixIdentity();
        SetShaderValueMatrix(shader, shader.locs[SHADER_LOC_MATRIX_MODEL], identity);
    }
    if (cameraPosLoc >= 0) {
        SetShaderValue(shader, cameraPosLoc, cam, SHADER_UNIFORM_VEC3);
    }
    if (gridSizeLoc >= 0) {
        SetShaderValue(shader, gridSizeLoc, &size, SHADER_UNIFORM_FLOAT);
    }
    if (planeAxisLoc >= 0) {
        SetShaderValue(shader, planeAxisLoc, &axis, SHADER_UNIFORM_INT);
    }
    if (fadeRadiusLoc >= 0) {
        SetShaderValue(shader, fadeRadiusLoc, &fade, SHADER_UNIFORM_FLOAT);
    }
    if (minorColorLoc >= 0) {
        SetShaderValue(shader, minorColorLoc, minor, SHADER_UNIFORM_VEC4);
    }
    if (majorColorLoc >= 0) {
        SetShaderValue(shader, majorColorLoc, major, SHADER_UNIFORM_VEC4);
    }

    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    rlVertex3f(p0.x, p0.y, p0.z);
    rlVertex3f(p1.x, p1.y, p1.z);
    rlVertex3f(p2.x, p2.y, p2.z);
    rlVertex3f(p3.x, p3.y, p3.z);
    rlVertex3f(p0.x, p0.y, p0.z);
    rlVertex3f(p3.x, p3.y, p3.z);
    rlVertex3f(p2.x, p2.y, p2.z);
    rlVertex3f(p1.x, p1.y, p1.z);
    rlEnd();

    EndShaderMode();
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    EndBlendMode();
}

float gridMetersPerPixel(
    bool orthographic,
    float orthoHalfHeight,
    float fovyDegrees,
    float viewportHeight,
    GridPlane plane,
    Vector3 eye,
    Vector3 viewDir) {
    (void)viewDir;
    const float height = std::max(viewportHeight, 1.0f);
    if (orthographic) {
        return (std::max(orthoHalfHeight, 1e-4f) * 2.0f) / height;
    }
    float planeDist = std::fabs(eye.y);
    switch (plane) {
    case GridPlane::XY:
        planeDist = std::fabs(eye.z);
        break;
    case GridPlane::YZ:
        planeDist = std::fabs(eye.x);
        break;
    case GridPlane::XZ:
    default:
        planeDist = std::fabs(eye.y);
        break;
    }
    planeDist = std::clamp(planeDist, 0.25f, 128.0f);
    const float fovy = std::max(fovyDegrees, 1.0f) * (3.14159265358979323846f / 180.0f);
    const float halfHeight = planeDist * std::tan(fovy * 0.5f);
    return (halfHeight * 2.0f) / height;
}

void drawOrientationWidget(const Camera3D& camera, float width, float height) {
    if (width < 8.0f || height < 8.0f) {
        return;
    }

    Vector3 forward{
        camera.target.x - camera.position.x,
        camera.target.y - camera.position.y,
        camera.target.z - camera.position.z,
    };
    const float forwardLen =
        std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
    if (forwardLen <= 1e-8f) {
        return;
    }
    forward = {forward.x / forwardLen, forward.y / forwardLen, forward.z / forwardLen};

    Vector3 right{
        forward.y * camera.up.z - forward.z * camera.up.y,
        forward.z * camera.up.x - forward.x * camera.up.z,
        forward.x * camera.up.y - forward.y * camera.up.x,
    };
    float rightLen = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
    if (rightLen <= 1e-6f) {
        const Vector3 fallback =
            std::fabs(forward.y) < 0.9f ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f};
        right = {
            forward.y * fallback.z - forward.z * fallback.y,
            forward.z * fallback.x - forward.x * fallback.z,
            forward.x * fallback.y - forward.y * fallback.x,
        };
        rightLen = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
        if (rightLen <= 1e-6f) {
            return;
        }
    }
    right = {right.x / rightLen, right.y / rightLen, right.z / rightLen};
    const Vector3 up{
        right.y * forward.z - right.z * forward.y,
        right.z * forward.x - right.x * forward.z,
        right.x * forward.y - right.y * forward.x,
    };

    const float cx = width - 54.0f;
    const float cy = 54.0f;
    const float len = 34.0f;
    const Vector2 origin{cx, cy};

    struct AxisArm {
        Vector3 dir{};
        Color color{};
        const char* label = nullptr;
        float depth = 0.0f;
    };

    AxisArm arms[] = {
        {{1.0f, 0.0f, 0.0f}, Color{220, 70, 70, 255}, "X", 0.0f},
        {{-1.0f, 0.0f, 0.0f}, Color{70, 190, 190, 255}, nullptr, 0.0f},
        {{0.0f, 1.0f, 0.0f}, Color{70, 200, 90, 255}, "Y", 0.0f},
        {{0.0f, -1.0f, 0.0f}, Color{200, 70, 190, 255}, nullptr, 0.0f},
        {{0.0f, 0.0f, 1.0f}, Color{70, 120, 230, 255}, "Z", 0.0f},
        {{0.0f, 0.0f, -1.0f}, Color{230, 200, 70, 255}, nullptr, 0.0f},
    };
    for (AxisArm& arm : arms) {
        arm.depth = -(arm.dir.x * forward.x + arm.dir.y * forward.y + arm.dir.z * forward.z);
    }
    std::sort(std::begin(arms), std::end(arms), [](const AxisArm& a, const AxisArm& b) {
        return a.depth < b.depth;
    });

    DrawCircleV(origin, 3.0f, Color{200, 200, 205, 220});
    for (const AxisArm& arm : arms) {
        const Vector2 tip{
            cx + (arm.dir.x * right.x + arm.dir.y * right.y + arm.dir.z * right.z) * len,
            cy - (arm.dir.x * up.x + arm.dir.y * up.y + arm.dir.z * up.z) * len,
        };
        const float thickness = arm.label != nullptr ? 3.0f : 2.0f;
        DrawLineEx(origin, tip, thickness, arm.color);
        DrawCircleV(tip, arm.label != nullptr ? 4.0f : 3.0f, arm.color);
        if (arm.label != nullptr) {
            DrawText(
                arm.label,
                static_cast<int>(tip.x + 5.0f),
                static_cast<int>(tip.y - 6.0f),
                12,
                arm.color);
        }
    }
}

}
