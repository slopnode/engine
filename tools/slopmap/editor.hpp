#pragma once

#include "assets/asset_store.hpp"
#include "camera.hpp"
#include "compile.hpp"
#include "map/brush.hpp"
#include "map/thing.hpp"
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

enum class SelectionMode {
    Brush,
    Face,
    Entity,
};

enum class TranslateSnapMode {
    Offset,
    Absolute,
};

enum class CreatePrimitive {
    Box,
    Cylinder,
    Stairs,
};

enum class PlaceTarget {
    PrefabInstance,
    Thing,
};

struct FaceRef {
    int brush = -1;
    int face = -1;

    bool valid() const { return brush >= 0 && face >= 0; }
    bool operator==(const FaceRef& other) const {
        return brush == other.brush && face == other.face;
    }
};

struct EntityRef {
    enum class Kind { Thing, Instance } kind = Kind::Thing;
    int index = -1;

    bool valid() const { return index >= 0; }
    bool operator==(const EntityRef& other) const {
        return kind == other.kind && index == other.index;
    }
};

struct CompileDirty {
    bool bsp = false;
    bool fac = false;
    bool vis = false;
    bool rad = false;
};

struct EditorDocument {
    std::string assetPath;
    std::vector<slopengine::Brush> brushes;
    std::vector<slopengine::PrefabInstance> instances;
    std::vector<slopengine::Thing> things;
    bool dirty = false;
    SelectionMode selectionMode = SelectionMode::Brush;
    std::vector<int> selectedBrushes;
    std::vector<FaceRef> selectedFaces;
    std::vector<EntityRef> selectedEntities;
    int activeBrush = -1;
    FaceRef activeFace{};
    EntityRef activeEntity{};
    std::string defaultMaterial = "default/cube";
    int nextBrushSerial = 1;
    int nextPrefabSerial = 1;
    int nextThingSerial = 1;

    bool hasSelection() const;
    bool isBrushSelected(int index) const;
    bool isFaceSelected(FaceRef ref) const;
    bool isEntitySelected(EntityRef ref) const;
};

struct Editor {
    EditorDocument levelDoc;
    EditorDocument prefabDoc;
    EditorScene scene = EditorScene::Level;
    EditorMode mode = EditorMode::Select;
    ViewPlane viewPlane = ViewPlane::PerspectiveY0;
    FlyCamera camera;
    MapPreview preview;
    PreviewFill fill = PreviewFill::Textures;
    WireframeOverlay wireframe = WireframeOverlay::Off;
    bool ignoreBackfaces = true;
    float gridSize = 0.1f;
    bool showGrid = true;
    GridPlane gridPlane = GridPlane::XZ;
    TranslateSnapMode translateSnapMode = TranslateSnapMode::Offset;
    slopengine::BrushRole createBrushRole = slopengine::BrushRole::Hull;
    CreatePrimitive createPrimitive = CreatePrimitive::Box;
    Rectangle contentViewport{0.0f, 0.0f, 1.0f, 1.0f};
    bool showQuitModal = false;
    bool quitConfirmed = false;
    bool showLoadModal = false;
    bool showSaveAsModal = false;
    bool showNewModal = false;
    bool showOpenPrefabModal = false;
    bool showSavePrefabAsModal = false;
    bool showSwitchSceneModal = false;
    bool showHollowModal = false;
    bool showPrimitiveParamsModal = false;
    float hollowThickness = 0.1f;
    int createCylinderSides = 16;
    int createStairsSteps = 8;
    EditorScene pendingScene = EditorScene::Level;
    std::string modalMapName;
    std::string modalPrefabPath;
    std::string statusMessage;
    std::string numericBuffer;
    PlaceTarget placeTarget = PlaceTarget::PrefabInstance;
    std::string placePrefabPath;
    std::optional<slopengine::ThingKind> placeThingKind;
    std::string placeSpritePath;
    std::string placeGeoPath;
    std::filesystem::path writePackageRoot;
    std::string writePackageId = "slopengine.base";
    s7_scheme* scheme = nullptr;
    std::vector<slopengine::Brush> expandedInstanceBrushes;
    std::vector<int> expandedInstanceOwners;
    CompileDirty compileDirty{};

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
    void markBspDirty();
    void markFacDirty();
    void markVisDirty();
    void markRadDirty();
    void markBrushCompileDirty(slopengine::BrushRole role);
    void markThingCompileDirty(slopengine::ThingKind kind);
    void clearCompileStage(CompileStage stage);
    bool cleanCompileData(
        slopengine::AssetStore& assets,
        const std::vector<CompileStage>& stages);
    void rebuildPreview(slopengine::AssetStore& assets);
    bool reloadVisPreview(slopengine::AssetStore& assets);
    bool reloadLitBake(slopengine::AssetStore& assets);
    void cycleGrid(int direction);
    const char* gridSizeLabel() const;
    void cycleGridPlane();
    const char* gridPlaneLabel() const;
    void setViewPlane(ViewPlane plane);
    void toggleOrthoTop();
    std::string allocateBrushId();
    std::string allocatePrefabId();
    std::string allocateThingId(const char* prefix);
    void clearSelection();
    void setSelectionMode(SelectionMode mode);
    void selectBrush(int index, bool additive);
    void selectFace(FaceRef ref, bool additive);
    void selectEntity(EntityRef ref, bool additive);
    void selectBrushes(const std::vector<int>& indices, int active);
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

ConstructionPlane constructionPlaneForView(ViewPlane view, GridPlane gridPlane = GridPlane::XZ);
ConstructionPlane constructionPlaneForGrid(GridPlane gridPlane);
ConstructionPlane constructionPlaneFromFace(const slopengine::BrushFace& face, Vector3 origin);

}
