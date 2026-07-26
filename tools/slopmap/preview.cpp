#include "preview.hpp"

#include "assets/geo_loader.hpp"
#include "assets/material_loader.hpp"
#include "map/csg_compile.hpp"
#include "map/fac_io.hpp"
#include "map/lightmap.hpp"

#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace slopmap {

namespace {

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

void drawEditModelTextured(const Model& model) {
    if (model.meshCount <= 0) {
        return;
    }
    DrawModel(model, {0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
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

    const std::string visVirtualPath = mapName + "/static";
    if (!assets.hasMapFac(visVirtualPath)) {
        return false;
    }
    const auto visPath = assets.resolvePath(slopengine::AssetKind::MapFac, visVirtualPath);
    if (!visPath) {
        return false;
    }
    auto loadedFac = slopengine::readFacFile(*visPath);
    if (!loadedFac || loadedFac->faces.empty()) {
        return false;
    }

    const auto resolveUv =
        [&assets](std::string_view materialPath) { return resolveMaterialUv(assets, materialPath); };
    pickFac = std::move(*loadedFac);
    const slopengine::CsgCompileResult compiled =
        slopengine::compileVisibleFacesToGeo(pickFac, resolveUv, nullptr);

    visModel = slopengine::buildModelFromGeo(
        compiled.asset,
        compiled.buffer,
        [&assets](std::string_view path) { return assets.resolveMaterial(path); });
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
                SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
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
        : slopengine::compileBrushesToGeo(brushes, resolveUv, &rad);

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
    if (brush.role != slopengine::BrushRole::Hull && brush.role != slopengine::BrushRole::Window) {
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
    float lineWidth) const {
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
            DrawModel(litModel, {0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
            if (moverOverlayValid) {
                DrawModel(moverOverlayModel, {0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
            }
            break;
        }
        [[fallthrough]];
    case PreviewFill::Unlit:
        if (visValid) {
            DrawModel(visModel, {0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
            if (moverOverlayValid) {
                DrawModel(moverOverlayModel, {0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
            }
            break;
        }
        [[fallthrough]];
    case PreviewFill::Textures:
        if (valid) {
            drawEditModelTextured(model);
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

void drawGrid(
    GridPlane plane,
    float halfExtent,
    float step,
    Color color,
    Vector3 eye,
    float lineWidth,
    Vector3 viewDir,
    Vector3 origin) {
    const float ox = origin.x;
    const float oy = origin.y;
    const float oz = origin.z;
    switch (plane) {
    case GridPlane::XY:
        for (float x = -halfExtent; x <= halfExtent + 0.001f; x += step) {
            drawThickLine3D(
                {ox + x, oy - halfExtent, oz},
                {ox + x, oy + halfExtent, oz},
                color,
                lineWidth,
                eye,
                viewDir);
        }
        for (float y = -halfExtent; y <= halfExtent + 0.001f; y += step) {
            drawThickLine3D(
                {ox - halfExtent, oy + y, oz},
                {ox + halfExtent, oy + y, oz},
                color,
                lineWidth,
                eye,
                viewDir);
        }
        break;
    case GridPlane::YZ:
        for (float y = -halfExtent; y <= halfExtent + 0.001f; y += step) {
            drawThickLine3D(
                {ox, oy + y, oz - halfExtent},
                {ox, oy + y, oz + halfExtent},
                color,
                lineWidth,
                eye,
                viewDir);
        }
        for (float z = -halfExtent; z <= halfExtent + 0.001f; z += step) {
            drawThickLine3D(
                {ox, oy - halfExtent, oz + z},
                {ox, oy + halfExtent, oz + z},
                color,
                lineWidth,
                eye,
                viewDir);
        }
        break;
    case GridPlane::XZ:
    default:
        for (float x = -halfExtent; x <= halfExtent + 0.001f; x += step) {
            drawThickLine3D(
                {ox + x, oy, oz - halfExtent},
                {ox + x, oy, oz + halfExtent},
                color,
                lineWidth,
                eye,
                viewDir);
        }
        for (float z = -halfExtent; z <= halfExtent + 0.001f; z += step) {
            drawThickLine3D(
                {ox - halfExtent, oy, oz + z},
                {ox + halfExtent, oy, oz + z},
                color,
                lineWidth,
                eye,
                viewDir);
        }
        break;
    }
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
