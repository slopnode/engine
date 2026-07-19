#pragma once

#include "assets/asset_store.hpp"
#include "camera.hpp"
#include "map/brush.hpp"
#include "preview.hpp"

#include <filesystem>
#include <string>
#include <vector>

struct s7_scheme;

namespace slopmap {

enum class EditorMode {
    Select,
    Create,
};

enum class ViewPlane {
    PerspectiveY0,
    Top,
    Front,
    Side,
};

enum class SelectionScope {
    Brush,
    Face,
};

struct EditorDocument {
    std::string mapName;
    std::vector<slopengine::Brush> brushes;
    bool dirty = false;
    int selectedBrush = -1;
    int selectedFace = -1;
    SelectionScope scope = SelectionScope::Brush;
    std::string defaultMaterial = "default/cube";
    int nextBrushSerial = 1;
};

struct Editor {
    EditorDocument doc;
    EditorMode mode = EditorMode::Select;
    ViewPlane viewPlane = ViewPlane::PerspectiveY0;
    FlyCamera camera;
    MapPreview preview;
    bool wireframe = false;
    float gridSize = 0.25f;
    Rectangle contentViewport{0.0f, 0.0f, 1.0f, 1.0f};
    bool showQuitModal = false;
    bool quitConfirmed = false;
    bool showLoadModal = false;
    bool showSaveAsModal = false;
    bool showNewModal = false;
    std::string modalMapName;
    std::string statusMessage;
    std::string numericBuffer;
    std::filesystem::path baseGamePath;
    std::string packageId = "slopengine.base";

    void newMap(const std::string& mapName);
    bool load(slopengine::AssetStore& assets, s7_scheme* scheme, const std::string& mapName);
    bool save(slopengine::AssetStore& assets);
    bool saveAs(slopengine::AssetStore& assets, const std::string& mapName);
    void markDirty();
    void rebuildPreview(slopengine::AssetStore& assets);
    void cycleGrid(int direction);
    void setViewPlane(ViewPlane plane);
    void toggleOrthoTop();
    std::string allocateBrushId();
    void frameSelection();
    Vector3 selectionCenter() const;
};

float snapToGrid(float value, float grid);
Vector3 snapToGrid(Vector3 value, float grid);

bool rayPlaneIntersection(Ray ray, Vector3 planePoint, Vector3 planeNormal, Vector3& outHit);
Ray mouseRay(const Camera3D& camera, Rectangle viewport);
Vector2 worldToViewportScreen(Vector3 world, const Camera3D& camera, Rectangle viewport);

struct ConstructionPlane {
    Vector3 origin{};
    Vector3 normal{};
    Vector3 axisU{};
    Vector3 axisV{};
};

ConstructionPlane constructionPlaneForView(ViewPlane view);

}
