#pragma once

#include "assets/asset_store.hpp"
#include "camera.hpp"
#include "compile.hpp"
#include "editor_types.hpp"
#include "history.hpp"
#include "map/brush.hpp"
#include "map/nav_graph.hpp"
#include "map/thing.hpp"
#include "map/prefab.hpp"
#include "preview.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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

enum class TranslateSnapMode {
    Offset,
    Absolute,
};

enum class TransformSpace {
    Global,
    Relative,
};

enum class CreatePrimitive {
    Box,
    Cylinder,
    Stairs,
    SpiralStairs,
};

enum class PlaceTarget {
    PrefabInstance,
    Thing,
};

enum class PlacePresentation {
    None,
    Sprite,
    Geo,
};

struct CompileDirty {
    bool bsp = false;
    bool vis = false;
    bool rad = false;
    bool nav = false;
};

struct ViewportCamera {
    ViewPlane plane = ViewPlane::PerspectiveY0;
    FlyCamera camera;
};

struct Editor {
    EditorDocument levelDoc;
    EditorDocument prefabDoc;
    DocumentHistory levelHistory;
    DocumentHistory prefabHistory;
    EditorScene scene = EditorScene::Level;
    EditorMode mode = EditorMode::Select;
    ViewportLayout viewportLayout = ViewportLayout::Single;
    int activeViewport = 0;
    ViewportCamera viewports[kViewportCount]{};
    ViewPlane viewPlane = ViewPlane::PerspectiveY0;
    FlyCamera camera;
    Vector3 orthoFocus{0.0f, 1.0f, 0.0f};
    MapPreview preview;
    PreviewFill fill = PreviewFill::Textures;
    WireframeOverlay wireframe = WireframeOverlay::Off;
    bool ignoreBackfaces = true;
    float gridSize = 0.1f;
    bool showGrid = true;
    bool showGizmos = true;
    bool showNodraw = false;
    bool showNavMesh = false;
    slopengine::MapNavigation bakedNav{};
    bool bakedNavValid = false;
    GridPlane gridPlane = GridPlane::XZ;
    TranslateSnapMode translateSnapMode = TranslateSnapMode::Offset;
    TransformSpace transformSpace = TransformSpace::Relative;
    float rotateSnapDegrees = 15.0f;
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
    bool showValidateBrushesWindow = false;
    bool showLeakWindow = false;
    bool leakChecked = false;
    bool leakFound = false;
    bool leakTotal = false;
    std::vector<Vector3> leakPathPoints;
    std::vector<LeakBrushHit> leakOffendingBrushes;
    float hollowThickness = 0.1f;
    bool hollowOutward = false;
    int createCylinderSides = 16;
    int createStairsSteps = 8;
    int createSpiralSides = 12;
    float createSpiralInnerRadius = 0.5f;
    float createSpiralStepHeight = 0.2f;
    EditorScene pendingScene = EditorScene::Level;
    std::string modalMapName;
    std::string modalPrefabPath;
    std::string statusMessage;
    std::string numericBuffer;
    PlaceTarget placeTarget = PlaceTarget::PrefabInstance;
    std::string placePrefabPath;
    std::optional<slopengine::ThingKind> placeThingKind;
    std::string placeThingType;
    PlacePresentation placePresentation = PlacePresentation::None;
    std::unordered_map<std::string, PlacePresentation> propChannelLock;
    bool particlePreviewEnabled = true;
    bool particlePreviewRestartRequest = false;
    std::filesystem::path writePackageRoot;
    std::string writePackageId = "slopengine.base";
    s7_scheme* scheme = nullptr;
    std::vector<slopengine::Brush> expandedInstanceBrushes;
    std::vector<int> expandedInstanceOwners;
    CompileDirty compileDirty{};
    bool previewDirty = false;

    struct ToolMouseCapture {
        bool active = false;
    };
    ToolMouseCapture toolMouseCapture;

    EditorDocument& doc();
    const EditorDocument& doc() const;
    DocumentHistory& history();
    const DocumentHistory& history() const;

    void newMap(const std::string& mapName);
    void newPrefab();
    bool load(slopengine::AssetStore& assets, s7_scheme* scheme, const std::string& mapName);
    bool loadPrefab(slopengine::AssetStore& assets, s7_scheme* scheme, const std::string& prefabPath);
    bool save(slopengine::AssetStore& assets);
    bool saveAs(slopengine::AssetStore& assets, const std::string& mapName);
    bool savePrefab(slopengine::AssetStore& assets);
    bool savePrefabAs(slopengine::AssetStore& assets, const std::string& prefabPath);
    bool switchScene(EditorScene next, bool force = false);
    void prepareEdit();
    void abortEdit();
    void endEdit();
    bool canUndo() const;
    bool canRedo() const;
    bool undo(slopengine::AssetStore& assets);
    bool redo(slopengine::AssetStore& assets);
    void markDirty();
    void markBspDirty();
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
    /** Loads maps/<name>/static.nav (baked by slopnav) into bakedNav, if present.
     *  Clears bakedNav/bakedNavValid and returns false when there's no baked navmesh
     *  yet -- that's the common case for a map nobody has run NAV on. */
    bool reloadBakedNav(slopengine::AssetStore& assets);
    /**
     * Builds a BSP from the current doc().brushes in-process (no disk I/O)
     * and runs the hull-seal analysis. Stores the result leak path (if any,
     * total or partial) into leakPathPoints for drawLeakPath to render, and
     * a summary into leakSealed/leakDetailWarnings for showLeakWindow.
     */
    void detectLeak();
    void cycleGrid(int direction);
    const char* gridSizeLabel() const;
    void cycleGridPlane();
    const char* gridPlaneLabel() const;
    void setViewPlane(ViewPlane plane);
    void setActiveViewport(int index);
    void toggleViewportLayout();
    void applyOrthoPoseToViewport(int index, Vector3 focus);
    void applyOrthoPoses();
    void syncActiveCameraFromBank();
    void syncBankFromActiveCamera();
    void toggleOrthoTop();
    static int viewportIndexForPlane(ViewPlane plane);
    static ViewPlane planeForViewportIndex(int index);
    std::string allocateBrushId();
    std::string allocatePrefabId();
    std::string allocateThingId(const char* prefix);
    bool renameBrush(int index, std::string_view newId);
    void clearSelection();
    void setSelectionMode(SelectionMode mode);
    void selectBrush(int index, bool additive);
    void selectFace(FaceRef ref, bool additive);
    void selectEdge(EdgeRef ref, bool additive);
    void selectVert(VertRef ref, bool additive);
    void selectEntity(EntityRef ref, bool additive);
    void selectBrushes(const std::vector<int>& indices, int active);
    void selectTouchingFaces();
    void frameSelection();
    void frameWorldPoint(Vector3 center);
    Vector3 selectionCenter() const;
    void toggleSelectedBrushRole();
    void convertSelectedBrushesToTriggers();
    void convertSelectedBrushesToMovers();
    void setSelectedBrushesAsDoors();
};

void beginToolMouseCapture(Editor& editor, Vector2 anchor);
void beginToolMouseCapture(Editor& editor);
void endToolMouseCapture(Editor& editor);
Vector2 toolMouseScreen(const Editor& editor);

float snapToGrid(float value, float grid);
Vector3 snapToGrid(Vector3 value, float grid);

bool rayPlaneIntersection(Ray ray, Vector3 planePoint, Vector3 planeNormal, Vector3& outHit);
Ray mouseRay(const Camera3D& camera, Rectangle viewport);
Ray mouseRay(const Camera3D& camera, Rectangle viewport, Vector2 mouseScreen);
Ray toolMouseRay(const Editor& editor, const Camera3D& camera, Rectangle viewport);
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

float screenDeltaAlongAxis(
    Vector3 axis,
    Vector3 origin,
    Vector2 mouseGrabScreen,
    const Editor& editor,
    const Camera3D& camera,
    Rectangle viewport,
    const ConstructionPlane* fallbackPlane = nullptr);

ConstructionPlane constructionPlaneForView(ViewPlane view, GridPlane gridPlane = GridPlane::XZ);
ConstructionPlane constructionPlaneForGrid(GridPlane gridPlane);
GridPlane gridPlaneForView(ViewPlane view, GridPlane gridPlane);
ConstructionPlane constructionPlaneFromFace(const slopengine::BrushFace& face, Vector3 origin);
void snapPickOnFace(
    const slopengine::BrushFace& face,
    Vector3 hit,
    float grid,
    ConstructionPlane& outPlane,
    Vector3& outHit);
Vector3 snapOnConstructionPlane(Vector3 point, const ConstructionPlane& plane, float grid);
bool pickConstructionPlane(
    const Editor& editor,
    Ray ray,
    ConstructionPlane& outPlane,
    Vector3& outHit);

}
