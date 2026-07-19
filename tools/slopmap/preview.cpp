#include "preview.hpp"

#include "assets/material_loader.hpp"
#include "map/csg_compile.hpp"

#include <cmath>
#include <cstdint>
#include <string_view>

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

} // namespace

void MapPreview::clear() {
    if (valid && model.meshCount > 0) {
        UnloadModel(model);
    }
    model = {};
    valid = false;
}

void MapPreview::rebuild(slopengine::AssetStore& assets, const std::vector<slopengine::Brush>& brushes) {
    clear();
    if (brushes.empty()) {
        return;
    }

    const slopengine::CsgCompileResult compiled = slopengine::compileBrushesToGeo(
        brushes,
        [&assets](std::string_view materialPath) { return resolveMaterialUv(assets, materialPath); });

    model = slopengine::buildModelFromGeo(
        compiled.asset,
        compiled.buffer,
        [&assets](std::string_view path) { return assets.resolveMaterial(path); });

    valid = model.meshCount > 0;
}

Color brushOutlineColor(const slopengine::Brush& brush, bool selected) {
    if (selected) {
        return Color{255, 140, 40, 255};
    }

    const std::uint32_t hash = hashString(brush.id);
    if (brush.role == slopengine::BrushRole::Detail) {
        return Color{
            mixChannel(70, hash, 0, 35),
            mixChannel(120, hash, 8, 40),
            mixChannel(210, hash, 16, 35),
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

void drawBrushFaceOutlines(const slopengine::Brush& brush, Color color) {
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
    bool wireframe,
    const std::vector<slopengine::Brush>& brushes,
    int selectedBrush) const {
    if (wireframe) {
        for (std::size_t i = 0; i < brushes.size(); ++i) {
            const bool selected = static_cast<int>(i) == selectedBrush;
            drawBrushFaceOutlines(brushes[i], brushOutlineColor(brushes[i], selected));
        }
        return;
    }

    if (valid) {
        DrawModel(model, {0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
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

void drawGridY0(float halfExtent, float step, Color color) {
    for (float x = -halfExtent; x <= halfExtent + 0.001f; x += step) {
        DrawLine3D({x, 0.0f, -halfExtent}, {x, 0.0f, halfExtent}, color);
    }
    for (float z = -halfExtent; z <= halfExtent + 0.001f; z += step) {
        DrawLine3D({-halfExtent, 0.0f, z}, {halfExtent, 0.0f, z}, color);
    }
}

}
