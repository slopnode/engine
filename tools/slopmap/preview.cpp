#include "preview.hpp"

#include "assets/geo_loader.hpp"
#include "assets/material_loader.hpp"
#include "map/csg_compile.hpp"
#include "map/fac_io.hpp"

#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <unordered_map>

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
}

void MapPreview::clearVis() {
    if (visValid && visModel.meshCount > 0) {
        UnloadModel(visModel);
    }
    visModel = {};
    visValid = false;
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
    const std::vector<slopengine::Brush>& brushes) {
    (void)brushes;
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
    const slopengine::CsgCompileResult compiled =
        slopengine::compileVisibleFacesToGeo(*loadedFac, resolveUv, nullptr);

    visModel = slopengine::buildModelFromGeo(
        compiled.asset,
        compiled.buffer,
        [&assets](std::string_view path) { return assets.resolveMaterial(path); });
    visValid = visModel.meshCount > 0;
    if (!visValid) {
        clearVis();
    }
    return visValid;
}

bool MapPreview::reloadBake(
    slopengine::AssetStore& assets,
    const std::string& mapName,
    const std::vector<slopengine::Brush>& brushes) {
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

void drawThickLine3D(Vector3 a, Vector3 b, Color color, float width, Vector3 eye) {
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
    Vector3 toEye{eye.x - mid.x, eye.y - mid.y, eye.z - mid.z};
    float toEyeLen = std::sqrt(toEye.x * toEye.x + toEye.y * toEye.y + toEye.z * toEye.z);
    if (toEyeLen < 1e-6f) {
        toEye = {0.0f, 1.0f, 0.0f};
        toEyeLen = 1.0f;
    } else {
        toEye = {toEye.x / toEyeLen, toEye.y / toEyeLen, toEye.z / toEyeLen};
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
            DrawModel(litModel, {0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
            break;
        }
        [[fallthrough]];
    case PreviewFill::Unlit:
        if (visValid) {
            DrawModel(visModel, {0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
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
    float lineWidth) {
    switch (plane) {
    case GridPlane::XY:
        for (float x = -halfExtent; x <= halfExtent + 0.001f; x += step) {
            drawThickLine3D({x, -halfExtent, 0.0f}, {x, halfExtent, 0.0f}, color, lineWidth, eye);
        }
        for (float y = -halfExtent; y <= halfExtent + 0.001f; y += step) {
            drawThickLine3D({-halfExtent, y, 0.0f}, {halfExtent, y, 0.0f}, color, lineWidth, eye);
        }
        break;
    case GridPlane::YZ:
        for (float y = -halfExtent; y <= halfExtent + 0.001f; y += step) {
            drawThickLine3D({0.0f, y, -halfExtent}, {0.0f, y, halfExtent}, color, lineWidth, eye);
        }
        for (float z = -halfExtent; z <= halfExtent + 0.001f; z += step) {
            drawThickLine3D({0.0f, -halfExtent, z}, {0.0f, halfExtent, z}, color, lineWidth, eye);
        }
        break;
    case GridPlane::XZ:
    default:
        for (float x = -halfExtent; x <= halfExtent + 0.001f; x += step) {
            drawThickLine3D({x, 0.0f, -halfExtent}, {x, 0.0f, halfExtent}, color, lineWidth, eye);
        }
        for (float z = -halfExtent; z <= halfExtent + 0.001f; z += step) {
            drawThickLine3D({-halfExtent, 0.0f, z}, {halfExtent, 0.0f, z}, color, lineWidth, eye);
        }
        break;
    }
}

}
