#pragma once

#include "assets/asset_store.hpp"
#include "map/brush.hpp"
#include "map/fac.hpp"
#include "map/lightmap.hpp"

#include <raylib.h>

#include <string>
#include <unordered_set>
#include <vector>

namespace slopmap {

enum class PreviewFill {
    Wireframe,
    Solid,
    Textures,
    Unlit,
    Lit,
    SolidLit,
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
    slopengine::FacFile pickFac{};

    Model moverOverlayModel{};
    bool moverOverlayValid = false;

    Model litModel{};
    bool litValid = false;
    slopengine::RadFile rad{};
    Shader lightmapShader{};
    int useLightmapLoc = -1;
    int solidLitLoc = -1;
    std::vector<Texture2D> lightmapAtlases;
    std::vector<int> transparentMeshIndices;

    void clear();
    void clearVis();
    void clearLit();
    void rebuild(slopengine::AssetStore& assets, const std::vector<slopengine::Brush>& brushes);
    bool reloadVisPreview(
        slopengine::AssetStore& assets,
        const std::string& mapName,
        const std::vector<slopengine::Brush>& brushes,
        const std::unordered_set<std::string>& moverBrushIds = {});
    bool reloadBake(
        slopengine::AssetStore& assets,
        const std::string& mapName,
        const std::vector<slopengine::Brush>& brushes,
        const std::unordered_set<std::string>& moverBrushIds = {});
    void draw(
        PreviewFill fill,
        WireframeOverlay wireframe,
        const std::vector<slopengine::Brush>& brushes,
        const std::vector<slopengine::Brush>& instanceBrushes,
        const std::vector<int>& selectedBrushes,
        Vector3 eye,
        Vector3 cameraForward,
        float lineWidth) const;
};

struct InfiniteGrid {
    Shader shader{};
    int cameraPosLoc = -1;
    int gridSizeLoc = -1;
    int planeAxisLoc = -1;
    int fadeRadiusLoc = -1;
    int minorColorLoc = -1;
    int majorColorLoc = -1;

    bool load(slopengine::AssetStore& assets);
    void unload();
    bool ready() const;
    void draw(GridPlane plane, Vector3 eye, float gridSize, float fadeRadius) const;
};

Color brushOutlineColor(const slopengine::Brush& brush, bool selected);
void drawThickLine3D(
    Vector3 a,
    Vector3 b,
    Color color,
    float width,
    Vector3 eye,
    Vector3 viewDir = {});
void drawBrushFaceOutlines(
    const slopengine::Brush& brush,
    Color color,
    Vector3 eye,
    float lineWidth);
void drawBrushAabbWires(const slopengine::Brush& brush, Color color);
void drawAabbWires(Vector3 mins, Vector3 maxs, Color color);
void drawAabbSolid(Vector3 mins, Vector3 maxs, Color color);
float gridMetersPerPixel(
    bool orthographic,
    float orthoHalfHeight,
    float fovyDegrees,
    float viewportHeight,
    GridPlane plane,
    Vector3 eye,
    Vector3 viewDir = {});
void drawOrientationWidget(const Camera3D& camera, float width, float height);

}
