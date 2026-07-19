#pragma once

#include "assets/asset_store.hpp"
#include "camera.hpp"
#include "map/brush.hpp"
#include "map/placement.hpp"
#include "map/prefab.hpp"
#include "preview.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct s7_scheme;

namespace slopmap {

enum class EditorMode {
    Select,
    Create,
    Place,
};

enum class EditorScene {
    Level,
    Prefab,
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

enum class SelectionTarget {
    None,
    Brush,
    Instance,
    Placement,
};

enum class PlaceTarget {
    PrefabInstance,
    Placement,
};

struct EditorDocument {
    std::string assetPath;
    std::vector<slopengine::Brush> brushes;
    std::vector<slopengine::PrefabInstance> instances;
    std::vector<slopengine::Placement> placements;
    bool dirty = false;
    SelectionTarget selection = SelectionTarget::None;
    int selectedBrush = -1;
    int selectedFace = -1;
    int selectedInstance = -1;
    int selectedPlacement = -1;
    SelectionScope scope = SelectionScope::Brush;
    std::string defaultMaterial = "default/cube";
    int nextBrushSerial = 1;
    int nextPrefabSerial = 1;
    int nextPlacementSerial = 1;
};

struct Editor {
    EditorDocument levelDoc;
    EditorDocument prefabDoc;
    EditorScene scene = EditorScene::Level;
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
    bool showOpenPrefabModal = false;
    bool showSavePrefabAsModal = false;
    bool showSwitchSceneModal = false;
    EditorScene pendingScene = EditorScene::Level;
    std::string modalMapName;
    std::string modalPrefabPath;
    std::string statusMessage;
    std::string numericBuffer;
    PlaceTarget placeTarget = PlaceTarget::PrefabInstance;
    std::string placePrefabPath;
    std::optional<slopengine::PlacementKind> placePlacementKind;
    std::string placeSpritePath;
    std::string placeGeoPath;
    std::filesystem::path baseGamePath;
    std::string packageId = "slopengine.base";
    s7_scheme* scheme = nullptr;
    std::vector<slopengine::Brush> expandedInstanceBrushes;
    std::vector<int> expandedInstanceOwners;

    EditorDocument& doc();
    const EditorDocument& doc() const;

    void newMap(const std::string& mapName);
    void newPrefab();
    bool load(slopengine::AssetStore& assets, s7_scheme* scheme, const std::string& mapName);
    bool loadPrefab(slopengine::AssetStore& assets, s7_scheme* scheme, const std::string& prefabPath);
    bool save(slopengine::AssetStore& assets);
    bool saveAs(slopengine::AssetStore& assets, const std::string& mapName);
    bool savePrefab(slopengine::AssetStore& assets);
    bool savePrefabAs(slopengine::AssetStore& assets, const std::string& prefabPath);
    bool switchScene(EditorScene next, bool force = false);
    void markDirty();
    void rebuildPreview(slopengine::AssetStore& assets);
    void cycleGrid(int direction);
    void setViewPlane(ViewPlane plane);
    void toggleOrthoTop();
    std::string allocateBrushId();
    std::string allocatePrefabId();
    std::string allocatePlacementId(const char* prefix);
    void clearSelection();
    void frameSelection();
    Vector3 selectionCenter() const;
    void toggleSelectedBrushRole();
};

float snapToGrid(float value, float grid);
Vector3 snapToGrid(Vector3 value, float grid);

bool rayPlaneIntersection(Ray ray, Vector3 planePoint, Vector3 planeNormal, Vector3& outHit);
Ray mouseRay(const Camera3D& camera, Rectangle viewport);
Vector2 worldToViewportScreen(Vector3 world, const Camera3D& camera, Rectangle viewport);

float length3(Vector3 v);
float dot3(Vector3 a, Vector3 b);
Vector3 add3(Vector3 a, Vector3 b);
Vector3 sub3(Vector3 a, Vector3 b);
Vector3 scale3(Vector3 a, float s);
Vector3 cross3(Vector3 a, Vector3 b);
Vector3 normalize3(Vector3 v);
Vector3 cameraForward(const Camera3D& camera);
Vector3 dragPlaneNormalForAxis(Vector3 axis, Vector3 viewForward);

struct ConstructionPlane {
    Vector3 origin{};
    Vector3 normal{};
    Vector3 axisU{};
    Vector3 axisV{};
};

ConstructionPlane constructionPlaneForView(ViewPlane view);
ConstructionPlane constructionPlaneFromFace(const slopengine::BrushFace& face, Vector3 origin);

}
