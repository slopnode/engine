#pragma once

#include "assets/asset_store.hpp"
#include "map/brush.hpp"
#include "map/lightmap.hpp"

#include <raylib.h>

#include <string>
#include <vector>

namespace slopmap {

enum class PreviewShading {
    Wireframe,
    Solid,
    Textured,
    Lit,
};

struct MapPreview {
    Model model{};
    bool valid = false;
    std::vector<std::string> editFaceIds;

    Model litModel{};
    bool litValid = false;
    slopengine::RadFile rad{};
    Shader lightmapShader{};
    int useLightmapLoc = -1;
    std::vector<Texture2D> lightmapAtlases;

    void clear();
    void clearLit();
    void rebuild(slopengine::AssetStore& assets, const std::vector<slopengine::Brush>& brushes);
    bool reloadBake(
        slopengine::AssetStore& assets,
        const std::string& mapName,
        const std::vector<slopengine::Brush>& brushes);
    void draw(
        PreviewShading shading,
        const std::vector<slopengine::Brush>& brushes,
        const std::vector<slopengine::Brush>& instanceBrushes,
        const std::vector<int>& selectedBrushes,
        Vector3 eye,
        float lineWidth) const;
};

Color brushOutlineColor(const slopengine::Brush& brush, bool selected);
void drawThickLine3D(Vector3 a, Vector3 b, Color color, float width, Vector3 eye);
void drawBrushFaceOutlines(
    const slopengine::Brush& brush,
    Color color,
    Vector3 eye,
    float lineWidth);
void drawBrushAabbWires(const slopengine::Brush& brush, Color color);
void drawAabbWires(Vector3 mins, Vector3 maxs, Color color);
void drawAabbSolid(Vector3 mins, Vector3 maxs, Color color);
void drawGridY0(float halfExtent, float step, Color color, Vector3 eye, float lineWidth);

}
