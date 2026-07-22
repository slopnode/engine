#include "editor.hpp"

#include "core/vfs.hpp"
#include "map/csg_script.hpp"
#include "map/csg_write.hpp"
#include "map/things_script.hpp"
#include "map/things_write.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <system_error>

namespace slopmap {

namespace {

bool ensureMapFiles(
    const std::filesystem::path& packageRoot,
    const std::string& mapName,
    std::filesystem::path& outCsgPath) {
    if (mapName.empty()) {
        return false;
    }
    const std::filesystem::path mapDir = packageRoot / "maps" / mapName;
    std::error_code ec;
    std::filesystem::create_directories(mapDir, ec);
    if (ec) {
        return false;
    }

    outCsgPath = mapDir / "static.csg";
    const std::filesystem::path metaPath = mapDir / "map.meta";
    if (!std::filesystem::exists(metaPath)) {
        std::ofstream meta(metaPath, std::ios::binary | std::ios::trunc);
        if (!meta) {
            return false;
        }
        meta << "(map\n";
        meta << "  (id \"" << mapName << "\")\n";
        meta << "  (name \"" << mapName << "\")\n";
        meta << "  (depends)\n";
        meta << "  (ambient 0.03 0.03 0.04))\n";
    }
    return true;
}

bool ensurePrefabPath(
    const std::filesystem::path& packageRoot,
    const std::string& prefabPath,
    std::filesystem::path& outCsgPath) {
    if (prefabPath.empty()) {
        return false;
    }
    outCsgPath = packageRoot / "prefabs" / (prefabPath + ".csg");
    std::error_code ec;
    std::filesystem::create_directories(outCsgPath.parent_path(), ec);
    return !ec;
}

void resetSelectionSerial(EditorDocument& doc) {
    doc.selectionMode = SelectionMode::Brush;
    doc.selectedBrushes.clear();
    doc.selectedFaces.clear();
    doc.selectedEntities.clear();
    doc.activeBrush = -1;
    doc.activeFace = {};
    doc.activeEntity = {};
    doc.nextBrushSerial = 1;
    doc.nextPrefabSerial = 1;
    doc.nextThingSerial = 1;
    for (const slopengine::Brush& brush : doc.brushes) {
        if (brush.id.rfind("brush-", 0) == 0) {
            try {
                const int serial = std::stoi(brush.id.substr(6));
                doc.nextBrushSerial = std::max(doc.nextBrushSerial, serial + 1);
            } catch (...) {
            }
        }
    }
    for (const slopengine::PrefabInstance& instance : doc.instances) {
        if (instance.id.rfind("prefab-", 0) == 0) {
            try {
                const int serial = std::stoi(instance.id.substr(7));
                doc.nextPrefabSerial = std::max(doc.nextPrefabSerial, serial + 1);
            } catch (...) {
            }
        }
    }
    for (const slopengine::Thing& thing : doc.things) {
        const auto dash = thing.id.rfind('-');
        if (dash == std::string::npos || dash + 1 >= thing.id.size()) {
            continue;
        }
        try {
            const int serial = std::stoi(thing.id.substr(dash + 1));
            doc.nextThingSerial = std::max(doc.nextThingSerial, serial + 1);
        } catch (...) {
        }
    }
}

std::filesystem::path thingsPathForMap(
    const std::filesystem::path& packageRoot,
    const std::string& mapName) {
    return packageRoot / "maps" / mapName / "things.s7";
}

std::filesystem::path thingsPathForPrefab(
    const std::filesystem::path& packageRoot,
    const std::string& prefabPath) {
    return packageRoot / "prefabs" / (prefabPath + ".s7");
}

void resetCamera(Editor& editor) {
    editor.camera.position = {0.0f, 2.5f, 8.0f};
    editor.camera.yaw = 3.14159265f;
    editor.camera.pitch = -0.35f;
    editor.camera.orthographic = false;
    editor.viewPlane = ViewPlane::PerspectiveY0;
}

} // namespace

float snapToGrid(float value, float grid) {
    if (grid <= 0.0f) {
        return value;
    }
    return std::round(value / grid) * grid;
}

Vector3 snapToGrid(Vector3 value, float grid) {
    return {
        snapToGrid(value.x, grid),
        snapToGrid(value.y, grid),
        snapToGrid(value.z, grid),
    };
}

bool rayPlaneIntersection(Ray ray, Vector3 planePoint, Vector3 planeNormal, Vector3& outHit) {
    const float denom =
        ray.direction.x * planeNormal.x + ray.direction.y * planeNormal.y + ray.direction.z * planeNormal.z;
    if (std::fabs(denom) < 1e-6f) {
        return false;
    }
    const Vector3 toPlane{
        planePoint.x - ray.position.x,
        planePoint.y - ray.position.y,
        planePoint.z - ray.position.z,
    };
    const float t =
        (toPlane.x * planeNormal.x + toPlane.y * planeNormal.y + toPlane.z * planeNormal.z) / denom;
    if (t < 0.0f) {
        return false;
    }
    outHit = {
        ray.position.x + ray.direction.x * t,
        ray.position.y + ray.direction.y * t,
        ray.position.z + ray.direction.z * t,
    };
    return true;
}

Ray mouseRay(const Camera3D& camera, Rectangle viewport) {
    const Vector2 mouse = GetMousePosition();
    const Vector2 local{
        mouse.x - viewport.x,
        mouse.y - viewport.y,
    };
    return GetScreenToWorldRayEx(
        local,
        camera,
        static_cast<int>(viewport.width),
        static_cast<int>(viewport.height));
}

Vector2 worldToViewportScreen(Vector3 world, const Camera3D& camera, Rectangle viewport) {
    const Vector2 local = GetWorldToScreenEx(
        world,
        camera,
        static_cast<int>(viewport.width),
        static_cast<int>(viewport.height));
    return {local.x + viewport.x, local.y + viewport.y};
}

float length3(Vector3 v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

float dot3(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 add3(Vector3 a, Vector3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 sub3(Vector3 a, Vector3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vector3 scale3(Vector3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}

Vector3 cross3(Vector3 a, Vector3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

Vector3 normalize3(Vector3 v) {
    const float len = length3(v);
    if (len < 1e-8f) {
        return {0.0f, 1.0f, 0.0f};
    }
    return {v.x / len, v.y / len, v.z / len};
}

Vector3 cameraForward(const Camera3D& camera) {
    return normalize3({
        camera.target.x - camera.position.x,
        camera.target.y - camera.position.y,
        camera.target.z - camera.position.z,
    });
}

Vector3 dragPlaneNormalForAxis(Vector3 axis, Vector3 viewForward) {
    Vector3 n = cross3(axis, viewForward);
    if (length3(n) < 1e-5f) {
        const Vector3 fallback = std::fabs(axis.y) < 0.9f ? Vector3{0.0f, 1.0f, 0.0f}
                                                         : Vector3{1.0f, 0.0f, 0.0f};
        n = cross3(axis, fallback);
    }
    return normalize3(n);
}

ConstructionPlane constructionPlaneForGrid(GridPlane gridPlane) {
    ConstructionPlane plane{};
    plane.origin = {0.0f, 0.0f, 0.0f};
    switch (gridPlane) {
    case GridPlane::XY:
        plane.normal = {0.0f, 0.0f, 1.0f};
        plane.axisU = {1.0f, 0.0f, 0.0f};
        plane.axisV = {0.0f, 1.0f, 0.0f};
        break;
    case GridPlane::YZ:
        plane.normal = {1.0f, 0.0f, 0.0f};
        plane.axisU = {0.0f, 0.0f, 1.0f};
        plane.axisV = {0.0f, 1.0f, 0.0f};
        break;
    case GridPlane::XZ:
    default:
        plane.normal = {0.0f, 1.0f, 0.0f};
        plane.axisU = {1.0f, 0.0f, 0.0f};
        plane.axisV = {0.0f, 0.0f, 1.0f};
        break;
    }
    return plane;
}

ConstructionPlane constructionPlaneForView(ViewPlane view, GridPlane gridPlane) {
    switch (view) {
    case ViewPlane::Front:
        return constructionPlaneForGrid(GridPlane::XY);
    case ViewPlane::Side:
        return constructionPlaneForGrid(GridPlane::YZ);
    case ViewPlane::Top:
        return constructionPlaneForGrid(GridPlane::XZ);
    case ViewPlane::PerspectiveY0:
    default:
        return constructionPlaneForGrid(gridPlane);
    }
}

ConstructionPlane constructionPlaneFromFace(const slopengine::BrushFace& face, Vector3 origin) {
    ConstructionPlane plane{};
    plane.origin = origin;
    plane.normal = normalize3(face.normal);

    Vector3 edge{};
    if (face.vertices.size() >= 2) {
        edge = {
            face.vertices[1].x - face.vertices[0].x,
            face.vertices[1].y - face.vertices[0].y,
            face.vertices[1].z - face.vertices[0].z,
        };
        const float along = dot3(edge, plane.normal);
        edge = {
            edge.x - plane.normal.x * along,
            edge.y - plane.normal.y * along,
            edge.z - plane.normal.z * along,
        };
    }
    if (length3(edge) < 1e-5f && face.vertices.size() >= 3) {
        edge = {
            face.vertices[2].x - face.vertices[0].x,
            face.vertices[2].y - face.vertices[0].y,
            face.vertices[2].z - face.vertices[0].z,
        };
        const float along = dot3(edge, plane.normal);
        edge = {
            edge.x - plane.normal.x * along,
            edge.y - plane.normal.y * along,
            edge.z - plane.normal.z * along,
        };
    }
    if (length3(edge) < 1e-5f) {
        const Vector3 fallback = std::fabs(plane.normal.y) < 0.9f ? Vector3{0.0f, 1.0f, 0.0f}
                                                                 : Vector3{1.0f, 0.0f, 0.0f};
        edge = cross3(fallback, plane.normal);
    }
    plane.axisU = normalize3(edge);
    plane.axisV = normalize3(cross3(plane.normal, plane.axisU));
    return plane;
}

EditorDocument& Editor::doc() {
    return scene == EditorScene::Level ? levelDoc : prefabDoc;
}

const EditorDocument& Editor::doc() const {
    return scene == EditorScene::Level ? levelDoc : prefabDoc;
}

bool EditorDocument::hasSelection() const {
    switch (selectionMode) {
    case SelectionMode::Brush:
        return !selectedBrushes.empty();
    case SelectionMode::Face:
        return !selectedFaces.empty();
    case SelectionMode::Entity:
        return !selectedEntities.empty();
    }
    return false;
}

bool EditorDocument::isBrushSelected(int index) const {
    return std::find(selectedBrushes.begin(), selectedBrushes.end(), index) != selectedBrushes.end();
}

bool EditorDocument::isFaceSelected(FaceRef ref) const {
    return std::find(selectedFaces.begin(), selectedFaces.end(), ref) != selectedFaces.end();
}

bool EditorDocument::isEntitySelected(EntityRef ref) const {
    return std::find(selectedEntities.begin(), selectedEntities.end(), ref) !=
        selectedEntities.end();
}

void Editor::clearSelection() {
    EditorDocument& d = doc();
    d.selectedBrushes.clear();
    d.selectedFaces.clear();
    d.selectedEntities.clear();
    d.activeBrush = -1;
    d.activeFace = {};
    d.activeEntity = {};
}

void Editor::setSelectionMode(SelectionMode mode) {
    EditorDocument& d = doc();
    if (d.selectionMode == mode) {
        return;
    }
    clearSelection();
    d.selectionMode = mode;
    switch (mode) {
    case SelectionMode::Brush:
        statusMessage = "Selection mode: Brush";
        break;
    case SelectionMode::Face:
        statusMessage = "Selection mode: Face";
        break;
    case SelectionMode::Entity:
        statusMessage = "Selection mode: Entity";
        break;
    }
}

void Editor::selectBrush(int index, bool additive) {
    EditorDocument& d = doc();
    d.selectionMode = SelectionMode::Brush;
    d.selectedFaces.clear();
    d.selectedEntities.clear();
    d.activeFace = {};
    d.activeEntity = {};
    if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
        if (!additive) {
            clearSelection();
        }
        return;
    }
    if (!additive) {
        d.selectedBrushes.clear();
        d.selectedBrushes.push_back(index);
        d.activeBrush = index;
        return;
    }
    const auto it = std::find(d.selectedBrushes.begin(), d.selectedBrushes.end(), index);
    if (it != d.selectedBrushes.end()) {
        d.selectedBrushes.erase(it);
        if (d.activeBrush == index) {
            d.activeBrush = d.selectedBrushes.empty() ? -1 : d.selectedBrushes.back();
        }
        return;
    }
    d.selectedBrushes.push_back(index);
    d.activeBrush = index;
}

void Editor::selectFace(FaceRef ref, bool additive) {
    EditorDocument& d = doc();
    d.selectionMode = SelectionMode::Face;
    d.selectedBrushes.clear();
    d.selectedEntities.clear();
    d.activeBrush = -1;
    d.activeEntity = {};
    if (!ref.valid() || ref.brush >= static_cast<int>(d.brushes.size()) ||
        ref.face >= static_cast<int>(d.brushes[static_cast<std::size_t>(ref.brush)].faces.size())) {
        if (!additive) {
            clearSelection();
        }
        return;
    }
    if (!additive) {
        d.selectedFaces.clear();
        d.selectedFaces.push_back(ref);
        d.activeFace = ref;
        return;
    }
    const auto it = std::find(d.selectedFaces.begin(), d.selectedFaces.end(), ref);
    if (it != d.selectedFaces.end()) {
        d.selectedFaces.erase(it);
        if (d.activeFace == ref) {
            d.activeFace = d.selectedFaces.empty() ? FaceRef{} : d.selectedFaces.back();
        }
        return;
    }
    d.selectedFaces.push_back(ref);
    d.activeFace = ref;
}

void Editor::selectEntity(EntityRef ref, bool additive) {
    EditorDocument& d = doc();
    d.selectionMode = SelectionMode::Entity;
    d.selectedBrushes.clear();
    d.selectedFaces.clear();
    d.activeBrush = -1;
    d.activeFace = {};
    if (!ref.valid()) {
        if (!additive) {
            clearSelection();
        }
        return;
    }
    if (ref.kind == EntityRef::Kind::Thing &&
        ref.index >= static_cast<int>(d.things.size())) {
        if (!additive) {
            clearSelection();
        }
        return;
    }
    if (ref.kind == EntityRef::Kind::Instance &&
        ref.index >= static_cast<int>(d.instances.size())) {
        if (!additive) {
            clearSelection();
        }
        return;
    }
    if (!additive) {
        d.selectedEntities.clear();
        d.selectedEntities.push_back(ref);
        d.activeEntity = ref;
        return;
    }
    const auto it = std::find(d.selectedEntities.begin(), d.selectedEntities.end(), ref);
    if (it != d.selectedEntities.end()) {
        d.selectedEntities.erase(it);
        if (d.activeEntity == ref) {
            d.activeEntity = d.selectedEntities.empty() ? EntityRef{} : d.selectedEntities.back();
        }
        return;
    }
    d.selectedEntities.push_back(ref);
    d.activeEntity = ref;
}

void Editor::selectBrushes(const std::vector<int>& indices, int active) {
    EditorDocument& d = doc();
    clearSelection();
    d.selectionMode = SelectionMode::Brush;
    d.selectedBrushes = indices;
    d.activeBrush = active;
    if (d.activeBrush < 0 && !d.selectedBrushes.empty()) {
        d.activeBrush = d.selectedBrushes.back();
    }
}

void Editor::newMap(const std::string& mapName) {
    scene = EditorScene::Level;
    levelDoc.assetPath = mapName.empty() ? "untitled" : mapName;
    levelDoc.brushes.clear();
    levelDoc.instances.clear();
    levelDoc.things.clear();
    levelDoc.dirty = false;
    resetSelectionSerial(levelDoc);
    expandedInstanceBrushes.clear();
    expandedInstanceOwners.clear();
    preview.clear();
    fill = PreviewFill::Textures;
    wireframe = WireframeOverlay::Off;
    compileDirty = {};
    resetCamera(*this);
    createBrushRole = slopengine::BrushRole::Hull;
    statusMessage = "New map '" + levelDoc.assetPath + "'";
}

void Editor::newPrefab() {
    scene = EditorScene::Prefab;
    prefabDoc.assetPath.clear();
    prefabDoc.brushes.clear();
    prefabDoc.instances.clear();
    prefabDoc.things.clear();
    prefabDoc.dirty = false;
    resetSelectionSerial(prefabDoc);
    expandedInstanceBrushes.clear();
    expandedInstanceOwners.clear();
    preview.clear();
    resetCamera(*this);
    mode = EditorMode::Create;
    createBrushRole = slopengine::BrushRole::Detail;
    statusMessage = "New prefab";
}

bool Editor::load(slopengine::AssetStore& assets, s7_scheme* schemeIn, const std::string& mapName) {
    scheme = schemeIn;
    auto loaded = slopengine::loadMapCsgDocument(scheme, assets, mapName);
    if (!loaded) {
        statusMessage = "Load failed: " + mapName;
        return false;
    }

    auto things = slopengine::loadMapThings(scheme, assets, mapName);
    if (!things) {
        statusMessage = "Load failed things: " + mapName;
        return false;
    }

    scene = EditorScene::Level;
    levelDoc.assetPath = mapName;
    levelDoc.brushes = std::move(loaded->brushes);
    levelDoc.instances = std::move(loaded->instances);
    levelDoc.things = std::move(things->things);
    levelDoc.dirty = false;
    if (auto owned = assets.resolveOwned(slopengine::AssetKind::MapMeta, mapName + "/map");
        owned && owned->package != nullptr) {
        writePackageRoot = owned->package->root();
        writePackageId = owned->package->meta().id;
    }
    resetSelectionSerial(levelDoc);
    compileDirty = {};
    rebuildPreview(assets);
    reloadVisPreview(assets);
    fill = reloadLitBake(assets) ? PreviewFill::Lit : PreviewFill::Textures;
    frameSelection();
    createBrushRole = slopengine::BrushRole::Hull;
    statusMessage = "Loaded " + mapName + " (" + std::to_string(levelDoc.brushes.size()) +
        " brushes, " + std::to_string(levelDoc.instances.size()) + " prefabs, " +
        std::to_string(levelDoc.things.size()) + " things)";
    return true;
}

bool Editor::loadPrefab(
    slopengine::AssetStore& assets,
    s7_scheme* schemeIn,
    const std::string& prefabPath) {
    scheme = schemeIn;
    auto brushes = slopengine::loadPrefabBrushes(scheme, assets, prefabPath);
    if (!brushes) {
        statusMessage = "Load prefab failed: " + prefabPath;
        return false;
    }

    auto things = slopengine::loadPrefabThings(scheme, assets, prefabPath);
    if (!things) {
        statusMessage = "Load prefab things failed: " + prefabPath;
        return false;
    }

    scene = EditorScene::Prefab;
    prefabDoc.assetPath = prefabPath;
    prefabDoc.brushes = std::move(*brushes);
    prefabDoc.instances.clear();
    prefabDoc.things = std::move(things->things);
    prefabDoc.dirty = false;
    if (auto owned = assets.resolveOwned(slopengine::AssetKind::PrefabCsg, prefabPath);
        owned && owned->package != nullptr) {
        writePackageRoot = owned->package->root();
        writePackageId = owned->package->meta().id;
    }
    resetSelectionSerial(prefabDoc);
    rebuildPreview(assets);
    frameSelection();
    createBrushRole = slopengine::BrushRole::Detail;
    statusMessage =
        "Loaded prefab " + prefabPath + " (" + std::to_string(prefabDoc.brushes.size()) +
        " brushes, " + std::to_string(prefabDoc.things.size()) + " things)";
    return true;
}

bool Editor::save(slopengine::AssetStore& assets) {
    if (scene == EditorScene::Prefab) {
        return savePrefab(assets);
    }
    if (levelDoc.assetPath.empty() || levelDoc.assetPath == "untitled") {
        showSaveAsModal = true;
        modalMapName = levelDoc.assetPath == "untitled" ? "" : levelDoc.assetPath;
        return false;
    }
    return saveAs(assets, levelDoc.assetPath);
}

bool Editor::saveAs(slopengine::AssetStore& assets, const std::string& mapName) {
    if (mapName.empty()) {
        statusMessage = "Save failed: empty map name";
        return false;
    }

    std::filesystem::path csgPath;
    std::filesystem::path packageRoot = writePackageRoot;
    auto existing = assets.resolveOwned(slopengine::AssetKind::MapCsg, mapName + "/static");
    if (existing && existing->package != nullptr) {
        csgPath = existing->path;
        packageRoot = existing->package->root();
        writePackageRoot = packageRoot;
        writePackageId = existing->package->meta().id;
    } else {
        if (!ensureMapFiles(writePackageRoot, mapName, csgPath)) {
            statusMessage = "Save failed: could not create map folder";
            return false;
        }
        packageRoot = writePackageRoot;
    }

    if (!slopengine::writeMapCsgDocument(csgPath, levelDoc.brushes, levelDoc.instances)) {
        statusMessage = "Save failed: write error";
        return false;
    }

    const std::filesystem::path thingsPath = thingsPathForMap(packageRoot, mapName);
    slopengine::ThingDocument thingDoc{};
    thingDoc.things = levelDoc.things;
    if (!slopengine::writeMapThings(thingsPath, thingDoc)) {
        statusMessage = "Save failed: things write error";
        return false;
    }

    levelDoc.assetPath = mapName;
    levelDoc.dirty = false;
    statusMessage = "Saved " + csgPath.string();
    return true;
}

bool Editor::savePrefab(slopengine::AssetStore& assets) {
    if (prefabDoc.assetPath.empty()) {
        showSavePrefabAsModal = true;
        modalPrefabPath.clear();
        return false;
    }
    return savePrefabAs(assets, prefabDoc.assetPath);
}

bool Editor::savePrefabAs(slopengine::AssetStore& assets, const std::string& prefabPath) {
    if (prefabPath.empty()) {
        statusMessage = "Save prefab failed: empty path";
        return false;
    }

    std::filesystem::path csgPath;
    std::filesystem::path packageRoot = writePackageRoot;
    auto existing = assets.resolveOwned(slopengine::AssetKind::PrefabCsg, prefabPath);
    if (existing && existing->package != nullptr) {
        csgPath = existing->path;
        packageRoot = existing->package->root();
        writePackageRoot = packageRoot;
        writePackageId = existing->package->meta().id;
    } else if (!ensurePrefabPath(writePackageRoot, prefabPath, csgPath)) {
        statusMessage = "Save prefab failed: could not create folders";
        return false;
    } else {
        packageRoot = writePackageRoot;
    }

    if (!slopengine::writeMapBrushes(csgPath, prefabDoc.brushes)) {
        statusMessage = "Save prefab failed: write error";
        return false;
    }

    const std::filesystem::path thingsPath = thingsPathForPrefab(packageRoot, prefabPath);
    if (prefabDoc.things.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(thingsPath)) {
            std::filesystem::remove(thingsPath, ec);
        }
    } else {
        slopengine::ThingDocument thingDoc{};
        thingDoc.things = prefabDoc.things;
        if (!slopengine::writeMapThings(thingsPath, thingDoc)) {
            statusMessage = "Save prefab failed: things write error";
            return false;
        }
    }

    prefabDoc.assetPath = prefabPath;
    prefabDoc.dirty = false;
    statusMessage = "Saved prefab " + csgPath.string();
    return true;
}

bool Editor::switchScene(EditorScene next, bool force) {
    if (scene == next) {
        return true;
    }
    if (!force && doc().dirty) {
        pendingScene = next;
        showSwitchSceneModal = true;
        return false;
    }
    scene = next;
    if (mode == EditorMode::Place && scene != EditorScene::Level &&
        placeTarget == PlaceTarget::PrefabInstance) {
        mode = EditorMode::Select;
    }
    createBrushRole = scene == EditorScene::Prefab ? slopengine::BrushRole::Detail
                                                   : slopengine::BrushRole::Hull;
    clearSelection();
    statusMessage = scene == EditorScene::Level ? "Level scene" : "Prefab scene";
    return true;
}

void Editor::markDirty() {
    doc().dirty = true;
}

void Editor::markBspDirty() {
    if (scene != EditorScene::Level) {
        return;
    }
    compileDirty.bsp = true;
    compileDirty.vis = true;
    compileDirty.rad = true;
}

void Editor::markVisDirty() {
    if (scene != EditorScene::Level) {
        return;
    }
    compileDirty.vis = true;
    compileDirty.rad = true;
}

void Editor::markRadDirty() {
    if (scene != EditorScene::Level) {
        return;
    }
    compileDirty.rad = true;
}

void Editor::markBrushCompileDirty(slopengine::BrushRole role) {
    if (slopengine::brushRoleContributesSplits(role)) {
        markBspDirty();
    } else {
        markVisDirty();
    }
}

void Editor::markThingCompileDirty(slopengine::ThingKind kind) {
    if (kind == slopengine::ThingKind::PointLight || kind == slopengine::ThingKind::SpotLight) {
        markRadDirty();
    }
}

void Editor::clearCompileStage(CompileStage stage) {
    switch (stage) {
    case CompileStage::Bsp:
        compileDirty.bsp = false;
        break;
    case CompileStage::Vis:
        compileDirty.vis = false;
        break;
    case CompileStage::Rad:
        compileDirty.rad = false;
        break;
    }
}

void Editor::rebuildPreview(slopengine::AssetStore& assets) {
    EditorDocument& d = doc();
    expandedInstanceBrushes.clear();
    expandedInstanceOwners.clear();

    std::vector<slopengine::Brush> combined = d.brushes;
    if (scheme != nullptr && !d.instances.empty()) {
        for (std::size_t i = 0; i < d.instances.size(); ++i) {
            auto expanded = slopengine::expandPrefabInstance(scheme, assets, d.instances[i]);
            if (!expanded) {
                continue;
            }
            for (slopengine::Brush& brush : *expanded) {
                expandedInstanceOwners.push_back(static_cast<int>(i));
                expandedInstanceBrushes.push_back(brush);
                combined.push_back(brush);
            }
        }
    }
    preview.rebuild(assets, combined);
}

bool Editor::reloadVisPreview(slopengine::AssetStore& assets) {
    if (levelDoc.assetPath.empty() || levelDoc.assetPath == "untitled") {
        return false;
    }
    std::vector<slopengine::Brush> combined = levelDoc.brushes;
    combined.insert(
        combined.end(), expandedInstanceBrushes.begin(), expandedInstanceBrushes.end());
    return preview.reloadVisPreview(assets, levelDoc.assetPath, combined);
}

bool Editor::reloadLitBake(slopengine::AssetStore& assets) {
    if (levelDoc.assetPath.empty() || levelDoc.assetPath == "untitled") {
        return false;
    }
    std::vector<slopengine::Brush> combined = levelDoc.brushes;
    combined.insert(
        combined.end(), expandedInstanceBrushes.begin(), expandedInstanceBrushes.end());
    return preview.reloadBake(assets, levelDoc.assetPath, combined);
}

namespace {

struct GridStep {
    float meters;
    const char* label;
};

constexpr GridStep kGridSteps[] = {
    {0.001f, "1mm"},
    {0.005f, "5mm"},
    {0.01f, "1cm"},
    {0.02f, "20mm"},
    {0.05f, "5cm"},
    {0.1f, "10cm"},
    {0.2f, "20cm"},
    {0.5f, "50cm"},
    {1.0f, "1m"},
    {5.0f, "5m"},
    {10.0f, "10m"},
    {20.0f, "20m"},
    {50.0f, "50m"},
    {1000.0f, "1km"},
    {5000.0f, "5km"},
    {10000.0f, "10km"},
    {20000.0f, "20km"},
    {50000.0f, "50km"},
};
constexpr int kGridStepCount = static_cast<int>(sizeof(kGridSteps) / sizeof(kGridSteps[0]));

int nearestGridStepIndex(float meters) {
    int best = 0;
    float bestDist = std::fabs(meters - kGridSteps[0].meters);
    for (int i = 1; i < kGridStepCount; ++i) {
        const float dist = std::fabs(meters - kGridSteps[i].meters);
        if (dist < bestDist) {
            bestDist = dist;
            best = i;
        }
    }
    return best;
}

} // namespace

void Editor::cycleGrid(int direction) {
    int index = nearestGridStepIndex(gridSize);
    index = (index - direction) % kGridStepCount;
    if (index < 0) {
        index += kGridStepCount;
    }
    gridSize = kGridSteps[index].meters;
    statusMessage = std::string("Grid: ") + kGridSteps[index].label;
}

const char* Editor::gridSizeLabel() const {
    return kGridSteps[nearestGridStepIndex(gridSize)].label;
}

void Editor::cycleGridPlane() {
    switch (gridPlane) {
    case GridPlane::XZ:
        gridPlane = GridPlane::XY;
        break;
    case GridPlane::XY:
        gridPlane = GridPlane::YZ;
        break;
    case GridPlane::YZ:
        gridPlane = GridPlane::XZ;
        break;
    }
    statusMessage = std::string("Grid plane: ") + gridPlaneLabel();
}

const char* Editor::gridPlaneLabel() const {
    switch (gridPlane) {
    case GridPlane::XY:
        return "XY";
    case GridPlane::YZ:
        return "YZ";
    case GridPlane::XZ:
    default:
        return "XZ";
    }
}

void Editor::setViewPlane(ViewPlane plane) {
    viewPlane = plane;
    switch (plane) {
    case ViewPlane::Top:
        camera.orthographic = true;
        camera.yaw = 0.0f;
        camera.pitch = -1.45f;
        break;
    case ViewPlane::Front:
        camera.orthographic = true;
        camera.yaw = 0.0f;
        camera.pitch = 0.0f;
        break;
    case ViewPlane::Side:
        camera.orthographic = true;
        camera.yaw = 1.5708f;
        camera.pitch = 0.0f;
        break;
    case ViewPlane::PerspectiveY0:
    default:
        camera.orthographic = false;
        break;
    }
}

void Editor::toggleOrthoTop() {
    if (viewPlane == ViewPlane::Top) {
        setViewPlane(ViewPlane::PerspectiveY0);
    } else {
        setViewPlane(ViewPlane::Top);
    }
}

std::string Editor::allocateBrushId() {
    return "brush-" + std::to_string(doc().nextBrushSerial++);
}

std::string Editor::allocatePrefabId() {
    return "prefab-" + std::to_string(doc().nextPrefabSerial++);
}

std::string Editor::allocateThingId(const char* prefix) {
    return std::string(prefix) + "-" + std::to_string(doc().nextThingSerial++);
}

void Editor::frameSelection() {
    const Vector3 center = selectionCenter();
    camera.position = {center.x, center.y + 2.5f, center.z + 8.0f};
    camera.lookAt(center);
}

Vector3 Editor::selectionCenter() const {
    const EditorDocument& d = doc();
    if (d.selectionMode == SelectionMode::Entity && d.activeEntity.valid()) {
        if (d.activeEntity.kind == EntityRef::Kind::Thing &&
            d.activeEntity.index < static_cast<int>(d.things.size())) {
            const slopengine::Thing& thing =
                d.things[static_cast<std::size_t>(d.activeEntity.index)];
            return thing.haveAt ? thing.at : Vector3{0.0f, 1.0f, 0.0f};
        }
        if (d.activeEntity.kind == EntityRef::Kind::Instance &&
            d.activeEntity.index < static_cast<int>(d.instances.size())) {
            return d.instances[static_cast<std::size_t>(d.activeEntity.index)].at;
        }
    }
    if (d.selectionMode == SelectionMode::Face && d.activeFace.valid() &&
        d.activeFace.brush < static_cast<int>(d.brushes.size())) {
        const slopengine::Brush& brush =
            d.brushes[static_cast<std::size_t>(d.activeFace.brush)];
        if (d.activeFace.face < static_cast<int>(brush.faces.size())) {
            const auto& verts = brush.faces[static_cast<std::size_t>(d.activeFace.face)].vertices;
            if (!verts.empty()) {
                Vector3 sum{};
                for (const Vector3& v : verts) {
                    sum = {sum.x + v.x, sum.y + v.y, sum.z + v.z};
                }
                const float inv = 1.0f / static_cast<float>(verts.size());
                return {sum.x * inv, sum.y * inv, sum.z * inv};
            }
        }
    }
    if (d.selectionMode == SelectionMode::Brush && !d.selectedBrushes.empty()) {
        Vector3 mins{
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
        };
        Vector3 maxs{
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
        };
        bool any = false;
        for (int index : d.selectedBrushes) {
            if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
                continue;
            }
            const slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(index)];
            any = true;
            mins.x = std::min(mins.x, brush.mins.x);
            mins.y = std::min(mins.y, brush.mins.y);
            mins.z = std::min(mins.z, brush.mins.z);
            maxs.x = std::max(maxs.x, brush.maxs.x);
            maxs.y = std::max(maxs.y, brush.maxs.y);
            maxs.z = std::max(maxs.z, brush.maxs.z);
        }
        if (any) {
            return {
                0.5f * (mins.x + maxs.x),
                0.5f * (mins.y + maxs.y),
                0.5f * (mins.z + maxs.z),
            };
        }
    }
    if (!d.brushes.empty()) {
        Vector3 mins = d.brushes[0].mins;
        Vector3 maxs = d.brushes[0].maxs;
        for (const slopengine::Brush& brush : d.brushes) {
            mins.x = std::min(mins.x, brush.mins.x);
            mins.y = std::min(mins.y, brush.mins.y);
            mins.z = std::min(mins.z, brush.mins.z);
            maxs.x = std::max(maxs.x, brush.maxs.x);
            maxs.y = std::max(maxs.y, brush.maxs.y);
            maxs.z = std::max(maxs.z, brush.maxs.z);
        }
        return {
            0.5f * (mins.x + maxs.x),
            0.5f * (mins.y + maxs.y),
            0.5f * (mins.z + maxs.z),
        };
    }
    if (!d.things.empty() && d.things.front().haveAt) {
        return d.things.front().at;
    }
    if (!d.instances.empty()) {
        return d.instances.front().at;
    }
    return {0.0f, 1.0f, 0.0f};
}

void Editor::toggleSelectedBrushRole() {
    EditorDocument& d = doc();
    if (d.selectionMode != SelectionMode::Brush || d.selectedBrushes.empty()) {
        return;
    }
    bool anySplit = false;
    slopengine::BrushRole lastRole = slopengine::BrushRole::Hull;
    std::string lastId;
    for (int index : d.selectedBrushes) {
        if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
            continue;
        }
        slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(index)];
        const slopengine::BrushRole previous = brush.role;
        switch (brush.role) {
        case slopengine::BrushRole::Hull:
            brush.role = slopengine::BrushRole::Detail;
            break;
        case slopengine::BrushRole::Detail:
            brush.role = slopengine::BrushRole::Hint;
            break;
        case slopengine::BrushRole::Hint:
            brush.role = slopengine::BrushRole::Trigger;
            break;
        case slopengine::BrushRole::Trigger:
            brush.role = slopengine::BrushRole::Water;
            break;
        case slopengine::BrushRole::Water:
            brush.role = slopengine::BrushRole::Window;
            break;
        case slopengine::BrushRole::Window:
            brush.role = slopengine::BrushRole::Hull;
            break;
        }
        brush.nocollide = slopengine::brushRoleDefaultNocollide(brush.role);
        if (slopengine::brushRoleContributesSplits(previous) ||
            slopengine::brushRoleContributesSplits(brush.role)) {
            anySplit = true;
        }
        lastRole = brush.role;
        lastId = brush.id;
    }
    markDirty();
    if (anySplit) {
        markBspDirty();
    } else {
        markVisDirty();
    }
    statusMessage = std::string("Role: ") + slopengine::brushRoleName(lastRole) + " (" + lastId + ")";
}

}
