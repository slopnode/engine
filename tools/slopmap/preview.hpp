#pragma once

#include "assets/asset_store.hpp"
#include "map/brush.hpp"
#include "map/lightmap.hpp"

#include <raylib.h>

#include <string>
#include <vector>

namespace slopmap {

enum class PreviewFill {
    Wireframe,
    Solid,
    Textures,
    Unlit,
    Lit,
};

enum class WireframeOverlay {
    Off,
    Visible,
    All,
};

enum class GridPlane {
    XZ,
    XY,
    YZ,
};

struct MapPreview {
    Model model{};
    bool valid = false;
    std::vector<std::string> editFaceIds;

    Model visModel{};
    bool visValid = false;

    Model litModel{};
    bool litValid = false;
    slopengine::RadFile rad{};
    Shader lightmapShader{};
    int useLightmapLoc = -1;
    std::vector<Texture2D> lightmapAtlases;

    void clear();
    void clearVis();
    void clearLit();
    void rebuild(slopengine::AssetStore& assets, const std::vector<slopengine::Brush>& brushes);
    bool reloadVisPreview(
        slopengine::AssetStore& assets,
        const std::string& mapName,
        const std::vector<slopengine::Brush>& brushes);
    bool reloadBake(
        slopengine::AssetStore& assets,
        const std::string& mapName,
        const std::vector<slopengine::Brush>& brushes);
    void draw(
        PreviewFill fill,
        WireframeOverlay wireframe,
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
void drawGrid(
    GridPlane plane,
    float halfExtent,
    float step,
    Color color,
    Vector3 eye,
    float lineWidth);

}
