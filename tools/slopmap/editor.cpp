#include "editor.hpp"

#include "core/vfs.hpp"
#include "map/csg_script.hpp"
#include "map/csg_write.hpp"
#include "map/mover_brushes.hpp"
#include "map/things_script.hpp"
#include "map/things_write.hpp"
#include "select_tool.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <system_error>
#include <unordered_set>

namespace slopmap {

namespace {

constexpr float kTouchWeldEpsilon = 1e-3f;

bool vertsTouch(Vector3 a, Vector3 b) {
    return std::fabs(a.x - b.x) <= kTouchWeldEpsilon && std::fabs(a.y - b.y) <= kTouchWeldEpsilon &&
        std::fabs(a.z - b.z) <= kTouchWeldEpsilon;
}

/** True if the faces share a full edge (two consecutive vertices in common, in either
 *  winding order) -- distinct brushes butted against each other will have matching
 *  edges even though they don't share vertex storage. */
bool facesShareEdge(const slopengine::BrushFace& a, const slopengine::BrushFace& b) {
    if (a.vertices.size() < 2 || b.vertices.size() < 2) {
        return false;
    }
    for (std::size_t ai = 0; ai < a.vertices.size(); ++ai) {
        const Vector3 a0 = a.vertices[ai];
        const Vector3 a1 = a.vertices[(ai + 1) % a.vertices.size()];
        for (std::size_t bi = 0; bi < b.vertices.size(); ++bi) {
            const Vector3 b0 = b.vertices[bi];
            const Vector3 b1 = b.vertices[(bi + 1) % b.vertices.size()];
            if ((vertsTouch(a0, b0) && vertsTouch(a1, b1)) ||
                (vertsTouch(a0, b1) && vertsTouch(a1, b0))) {
                return true;
            }
        }
    }
    return false;
}

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
        meta << "  (author \"\")\n";
        meta << "  (description \"\")\n";
        meta << "  (depends))\n";
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
    doc.selectedEdges.clear();
    doc.selectedVerts.clear();
    doc.selectedEntities.clear();
    doc.activeBrush = -1;
    doc.activeFace = {};
    doc.activeEdge = {};
    doc.activeVert = {};
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

constexpr float kOrthoPullback = 512.0f;

void applyOrthoCameraPose(FlyCamera& camera, ViewPlane plane, Vector3 orthoFocus) {
    switch (plane) {
    case ViewPlane::Top:
        camera.orthographic = true;
        camera.viewPlane = ViewPlane::Top;
        camera.yaw = 0.0f;
        camera.pitch = -1.57079632679f;
        camera.position = {
            orthoFocus.x,
            orthoFocus.y + kOrthoPullback,
            orthoFocus.z,
        };
        break;
    case ViewPlane::Front:
        camera.orthographic = true;
        camera.viewPlane = ViewPlane::Front;
        camera.yaw = 3.14159265f;
        camera.pitch = 0.0f;
        camera.position = {
            orthoFocus.x,
            orthoFocus.y,
            orthoFocus.z + kOrthoPullback,
        };
        break;
    case ViewPlane::Side:
        camera.orthographic = true;
        camera.viewPlane = ViewPlane::Side;
        camera.yaw = -1.57079632679f;
        camera.pitch = 0.0f;
        camera.position = {
            orthoFocus.x + kOrthoPullback,
            orthoFocus.y,
            orthoFocus.z,
        };
        break;
    case ViewPlane::PerspectiveY0:
    default:
        camera.orthographic = false;
        camera.viewPlane = ViewPlane::PerspectiveY0;
        break;
    }
}

void syncGridPlaneForView(Editor& editor, ViewPlane plane) {
    switch (plane) {
    case ViewPlane::Front:
        editor.gridPlane = GridPlane::XY;
        break;
    case ViewPlane::Side:
        editor.gridPlane = GridPlane::YZ;
        break;
    case ViewPlane::Top:
    case ViewPlane::PerspectiveY0:
    default:
        editor.gridPlane = GridPlane::XZ;
        break;
    }
}

void resetCamera(Editor& editor) {
    editor.orthoFocus = {0.0f, 1.0f, 0.0f};
    editor.activeViewport = 0;
    editor.viewportLayout = ViewportLayout::Single;

    editor.viewports[0].plane = ViewPlane::PerspectiveY0;
    editor.viewports[0].camera = {};
    editor.viewports[0].camera.position = {0.0f, 2.5f, 8.0f};
    editor.viewports[0].camera.yaw = 3.14159265f;
    editor.viewports[0].camera.pitch = -0.35f;
    editor.viewports[0].camera.orthographic = false;
    editor.viewports[0].camera.viewPlane = ViewPlane::PerspectiveY0;

    editor.viewports[1].plane = ViewPlane::Top;
    editor.viewports[1].camera = {};
    editor.viewports[1].camera.orthoHalfHeight = 8.0f;

    editor.viewports[2].plane = ViewPlane::Front;
    editor.viewports[2].camera = {};
    editor.viewports[2].camera.orthoHalfHeight = 8.0f;

    editor.viewports[3].plane = ViewPlane::Side;
    editor.viewports[3].camera = {};
    editor.viewports[3].camera.orthoHalfHeight = 8.0f;

    editor.applyOrthoPoses();
    editor.syncActiveCameraFromBank();
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
    return mouseRay(camera, viewport, GetMousePosition());
}

Ray mouseRay(const Camera3D& camera, Rectangle viewport, Vector2 mouseScreen) {
    const Vector2 local{
        mouseScreen.x - viewport.x,
        mouseScreen.y - viewport.y,
    };
    return GetScreenToWorldRayEx(
        local,
        camera,
        static_cast<int>(viewport.width),
        static_cast<int>(viewport.height));
}

Ray toolMouseRay(const Editor& editor, const Camera3D& camera, Rectangle viewport) {
    return mouseRay(camera, viewport, toolMouseScreen(editor));
}

void beginToolMouseCapture(Editor& editor, Vector2 /*anchor*/) {
    if (editor.toolMouseCapture.active) {
        return;
    }
    if (IsCursorHidden()) {
        EnableCursor();
    }
    HideCursor();
    editor.toolMouseCapture.active = true;
}

void beginToolMouseCapture(Editor& editor) {
    beginToolMouseCapture(editor, GetMousePosition());
}

void endToolMouseCapture(Editor& editor) {
    if (!editor.toolMouseCapture.active) {
        return;
    }
    editor.toolMouseCapture.active = false;
    if (IsCursorHidden() && !IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        ShowCursor();
    }
}

Vector2 toolMouseScreen(const Editor& /*editor*/) {
    return GetMousePosition();
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

Vector2 screenAxisForWorldAxisLocal(
    Vector3 origin,
    Vector3 axis,
    const Camera3D& camera,
    Rectangle viewport) {
    const Vector2 s0 = worldToViewportScreen(origin, camera, viewport);
    const Vector2 s1 = worldToViewportScreen(add3(origin, axis), camera, viewport);
    return {s1.x - s0.x, s1.y - s0.y};
}

float screenDeltaAlongAxis(
    Vector3 axis,
    Vector3 origin,
    Vector2 mouseGrabScreen,
    const Editor& editor,
    const Camera3D& camera,
    Rectangle viewport,
    const ConstructionPlane* fallbackPlane) {
    const Vector2 mouse = toolMouseScreen(editor);
    Vector2 axisScreen = screenAxisForWorldAxisLocal(origin, axis, camera, viewport);
    const float lenSq = axisScreen.x * axisScreen.x + axisScreen.y * axisScreen.y;
    const Vector2 mouseDelta{mouse.x - mouseGrabScreen.x, mouse.y - mouseGrabScreen.y};
    if (lenSq >= 4.0f) {
        if (std::fabs(axisScreen.x) >= std::fabs(axisScreen.y)) {
            if (axisScreen.x < 0.0f) {
                axisScreen.x = -axisScreen.x;
                axisScreen.y = -axisScreen.y;
                axis = scale3(axis, -1.0f);
            }
        } else if (axisScreen.y < 0.0f) {
            axisScreen.x = -axisScreen.x;
            axisScreen.y = -axisScreen.y;
            axis = scale3(axis, -1.0f);
        }
        return (mouseDelta.x * axisScreen.x + mouseDelta.y * axisScreen.y) / lenSq;
    }
    if (fallbackPlane != nullptr) {
        const Vector2 uScreen =
            screenAxisForWorldAxisLocal(origin, fallbackPlane->axisU, camera, viewport);
        const Vector2 vScreen =
            screenAxisForWorldAxisLocal(origin, fallbackPlane->axisV, camera, viewport);
        const float dist = std::max(length3(sub3(camera.position, origin)), 0.25f);
        const float scale = dist * 0.0025f;
        float uAmount = 0.0f;
        float vAmount = 0.0f;
        const float uLenSq = uScreen.x * uScreen.x + uScreen.y * uScreen.y;
        const float vLenSq = vScreen.x * vScreen.x + vScreen.y * vScreen.y;
        if (uLenSq >= 1.0f) {
            uAmount = (mouseDelta.x * uScreen.x + mouseDelta.y * uScreen.y) / uLenSq;
        }
        if (vLenSq >= 1.0f) {
            vAmount = (mouseDelta.x * vScreen.x + mouseDelta.y * vScreen.y) / vLenSq;
        }
        const Vector3 planeDelta = add3(
            scale3(fallbackPlane->axisU, uAmount * scale),
            scale3(fallbackPlane->axisV, vAmount * scale));
        return dot3(planeDelta, axis);
    }
    const float dist = std::max(length3(sub3(camera.position, origin)), 0.25f);
    return -mouseDelta.y * dist * 0.0025f;
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

GridPlane gridPlaneForView(ViewPlane view, GridPlane gridPlane) {
    switch (view) {
    case ViewPlane::Front:
        return GridPlane::XY;
    case ViewPlane::Side:
        return GridPlane::YZ;
    case ViewPlane::Top:
        return GridPlane::XZ;
    case ViewPlane::PerspectiveY0:
    default:
        return gridPlane;
    }
}

ConstructionPlane constructionPlaneForView(ViewPlane view, GridPlane gridPlane) {
    return constructionPlaneForGrid(gridPlaneForView(view, gridPlane));
}

ConstructionPlane constructionPlaneFromFace(const slopengine::BrushFace& face, Vector3 origin) {
    ConstructionPlane plane{};
    plane.origin = origin;
    plane.normal = normalize3(face.normal);

    const Vector3 n = plane.normal;
    if (std::fabs(n.x) > 0.99f) {
        plane.axisU = {0.0f, 0.0f, 1.0f};
        plane.axisV = normalize3(cross3(plane.normal, plane.axisU));
        return plane;
    }
    if (std::fabs(n.y) > 0.99f) {
        plane.axisU = {1.0f, 0.0f, 0.0f};
        plane.axisV = normalize3(cross3(plane.normal, plane.axisU));
        return plane;
    }
    if (std::fabs(n.z) > 0.99f) {
        plane.axisU = {1.0f, 0.0f, 0.0f};
        plane.axisV = normalize3(cross3(plane.normal, plane.axisU));
        return plane;
    }

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

Vector3 snapOnConstructionPlane(Vector3 point, const ConstructionPlane& plane, float grid) {
    const Vector3 rel = sub3(point, plane.origin);
    const float u = snapToGrid(dot3(rel, plane.axisU), grid);
    const float v = snapToGrid(dot3(rel, plane.axisV), grid);
    return {
        plane.origin.x + plane.axisU.x * u + plane.axisV.x * v,
        plane.origin.y + plane.axisU.y * u + plane.axisV.y * v,
        plane.origin.z + plane.axisU.z * u + plane.axisV.z * v,
    };
}

void snapPickOnFace(
    const slopengine::BrushFace& face,
    Vector3 hit,
    float grid,
    ConstructionPlane& outPlane,
    Vector3& outHit) {
    const float magnet = std::max(grid, 1e-4f);
    float bestDistSq = magnet * magnet;
    bool foundVert = false;
    Vector3 bestVert = hit;
    for (const Vector3& vert : face.vertices) {
        const Vector3 d = sub3(hit, vert);
        const float distSq = d.x * d.x + d.y * d.y + d.z * d.z;
        if (distSq <= bestDistSq) {
            bestDistSq = distSq;
            bestVert = vert;
            foundVert = true;
        }
    }
    if (foundVert) {
        outHit = bestVert;
        outPlane = constructionPlaneFromFace(face, outHit);
        return;
    }

    const Vector3 normal = normalize3(face.normal);
    Vector3 snapped = snapToGrid(hit, grid);
    const float off = dot3(sub3(snapped, hit), normal);
    snapped = sub3(snapped, scale3(normal, off));

    outPlane = constructionPlaneFromFace(face, snapped);
    const Vector3 gridOrigin = snapToGrid(snapped, grid);
    const float originOff = dot3(sub3(gridOrigin, snapped), normal);
    outPlane.origin = sub3(gridOrigin, scale3(normal, originOff));
    outHit = snapOnConstructionPlane(snapped, outPlane, grid);
    outPlane.origin = outHit;
}

bool pickConstructionPlane(
    const Editor& editor,
    Ray ray,
    ConstructionPlane& outPlane,
    Vector3& outHit) {
    float bestFaceT = std::numeric_limits<float>::max();
    int bestBrush = -1;
    int bestFace = -1;
    const EditorDocument& d = editor.doc();
    for (std::size_t i = 0; i < d.brushes.size(); ++i) {
        float faceT = 0.0f;
        const auto face =
            rayBrushFaceIndex(ray, d.brushes[i], &faceT, editor.ignoreBackfaces);
        if (face && faceT < bestFaceT) {
            bestFaceT = faceT;
            bestBrush = static_cast<int>(i);
            bestFace = *face;
        }
    }

    if (bestBrush >= 0 && bestFace >= 0) {
        const slopengine::BrushFace& face =
            d.brushes[static_cast<std::size_t>(bestBrush)].faces[static_cast<std::size_t>(bestFace)];
        const Vector3 hit{
            ray.position.x + ray.direction.x * bestFaceT,
            ray.position.y + ray.direction.y * bestFaceT,
            ray.position.z + ray.direction.z * bestFaceT,
        };
        snapPickOnFace(face, hit, editor.gridSize, outPlane, outHit);
        return true;
    }

    outPlane = constructionPlaneForView(editor.viewPlane, editor.gridPlane);
    if (!rayPlaneIntersection(ray, outPlane.origin, outPlane.normal, outHit)) {
        return false;
    }
    outHit = snapOnConstructionPlane(outHit, outPlane, editor.gridSize);
    outPlane.origin = outHit;
    return true;
}

EditorDocument& Editor::doc() {
    return scene == EditorScene::Level ? levelDoc : prefabDoc;
}

const EditorDocument& Editor::doc() const {
    return scene == EditorScene::Level ? levelDoc : prefabDoc;
}

DocumentHistory& Editor::history() {
    return scene == EditorScene::Level ? levelHistory : prefabHistory;
}

const DocumentHistory& Editor::history() const {
    return scene == EditorScene::Level ? levelHistory : prefabHistory;
}

bool EditorDocument::hasSelection() const {
    switch (selectionMode) {
    case SelectionMode::Brush:
        return !selectedBrushes.empty();
    case SelectionMode::Face:
        return !selectedFaces.empty();
    case SelectionMode::Edge:
        return !selectedEdges.empty();
    case SelectionMode::Vert:
        return !selectedVerts.empty();
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

bool EditorDocument::isEdgeSelected(EdgeRef ref) const {
    return std::find(selectedEdges.begin(), selectedEdges.end(), ref) != selectedEdges.end();
}

bool EditorDocument::isVertSelected(VertRef ref) const {
    return std::find(selectedVerts.begin(), selectedVerts.end(), ref) != selectedVerts.end();
}

bool EditorDocument::isEntitySelected(EntityRef ref) const {
    return std::find(selectedEntities.begin(), selectedEntities.end(), ref) !=
        selectedEntities.end();
}

void Editor::clearSelection() {
    EditorDocument& d = doc();
    d.selectedBrushes.clear();
    d.selectedFaces.clear();
    d.selectedEdges.clear();
    d.selectedVerts.clear();
    d.selectedEntities.clear();
    d.activeBrush = -1;
    d.activeFace = {};
    d.activeEdge = {};
    d.activeVert = {};
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
    case SelectionMode::Edge:
        statusMessage = "Selection mode: Edge";
        break;
    case SelectionMode::Vert:
        statusMessage = "Selection mode: Vert";
        break;
    case SelectionMode::Entity:
        statusMessage = "Selection mode: Thing";
        break;
    }
}

void Editor::selectBrush(int index, bool additive) {
    EditorDocument& d = doc();
    d.selectionMode = SelectionMode::Brush;
    d.selectedFaces.clear();
    d.selectedEdges.clear();
    d.selectedVerts.clear();
    d.selectedEntities.clear();
    d.activeFace = {};
    d.activeEdge = {};
    d.activeVert = {};
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
    d.selectedEdges.clear();
    d.selectedVerts.clear();
    d.selectedEntities.clear();
    d.activeBrush = -1;
    d.activeEdge = {};
    d.activeVert = {};
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

void Editor::selectEdge(EdgeRef ref, bool additive) {
    EditorDocument& d = doc();
    d.selectionMode = SelectionMode::Edge;
    d.selectedBrushes.clear();
    d.selectedFaces.clear();
    d.selectedVerts.clear();
    d.selectedEntities.clear();
    d.activeBrush = -1;
    d.activeFace = {};
    d.activeVert = {};
    d.activeEntity = {};
    if (!ref.valid() || ref.brush >= static_cast<int>(d.brushes.size())) {
        if (!additive) {
            clearSelection();
        }
        return;
    }
    const slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(ref.brush)];
    if (ref.face >= static_cast<int>(brush.faces.size()) ||
        ref.edge >= static_cast<int>(brush.faces[static_cast<std::size_t>(ref.face)].vertices.size())) {
        if (!additive) {
            clearSelection();
        }
        return;
    }
    if (!additive) {
        d.selectedEdges.clear();
        d.selectedEdges.push_back(ref);
        d.activeEdge = ref;
        return;
    }
    const auto it = std::find(d.selectedEdges.begin(), d.selectedEdges.end(), ref);
    if (it != d.selectedEdges.end()) {
        d.selectedEdges.erase(it);
        if (d.activeEdge == ref) {
            d.activeEdge = d.selectedEdges.empty() ? EdgeRef{} : d.selectedEdges.back();
        }
        return;
    }
    d.selectedEdges.push_back(ref);
    d.activeEdge = ref;
}

void Editor::selectVert(VertRef ref, bool additive) {
    EditorDocument& d = doc();
    d.selectionMode = SelectionMode::Vert;
    d.selectedBrushes.clear();
    d.selectedFaces.clear();
    d.selectedEdges.clear();
    d.selectedEntities.clear();
    d.activeBrush = -1;
    d.activeFace = {};
    d.activeEdge = {};
    d.activeEntity = {};
    if (!ref.valid() || ref.brush >= static_cast<int>(d.brushes.size())) {
        if (!additive) {
            clearSelection();
        }
        return;
    }
    const slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(ref.brush)];
    if (ref.face >= static_cast<int>(brush.faces.size()) ||
        ref.vert >= static_cast<int>(brush.faces[static_cast<std::size_t>(ref.face)].vertices.size())) {
        if (!additive) {
            clearSelection();
        }
        return;
    }
    if (!additive) {
        d.selectedVerts.clear();
        d.selectedVerts.push_back(ref);
        d.activeVert = ref;
        return;
    }
    const auto it = std::find(d.selectedVerts.begin(), d.selectedVerts.end(), ref);
    if (it != d.selectedVerts.end()) {
        d.selectedVerts.erase(it);
        if (d.activeVert == ref) {
            d.activeVert = d.selectedVerts.empty() ? VertRef{} : d.selectedVerts.back();
        }
        return;
    }
    d.selectedVerts.push_back(ref);
    d.activeVert = ref;
}

void Editor::selectEntity(EntityRef ref, bool additive) {
    EditorDocument& d = doc();
    d.selectionMode = SelectionMode::Entity;
    d.selectedBrushes.clear();
    d.selectedFaces.clear();
    d.selectedEdges.clear();
    d.selectedVerts.clear();
    d.activeBrush = -1;
    d.activeFace = {};
    d.activeEdge = {};
    d.activeVert = {};
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

void Editor::selectTouchingFaces() {
    EditorDocument& d = doc();
    if (d.selectionMode != SelectionMode::Face || d.selectedFaces.empty()) {
        return;
    }

    std::vector<FaceRef> selected = d.selectedFaces;
    std::size_t frontier = 0;
    while (frontier < selected.size()) {
        const FaceRef current = selected[frontier++];
        if (current.brush < 0 || current.brush >= static_cast<int>(d.brushes.size())) {
            continue;
        }
        const slopengine::Brush& currentBrush =
            d.brushes[static_cast<std::size_t>(current.brush)];
        if (current.face < 0 || current.face >= static_cast<int>(currentBrush.faces.size())) {
            continue;
        }
        const slopengine::BrushFace& currentFace =
            currentBrush.faces[static_cast<std::size_t>(current.face)];
        for (std::size_t bi = 0; bi < d.brushes.size(); ++bi) {
            const slopengine::Brush& brush = d.brushes[bi];
            for (std::size_t fi = 0; fi < brush.faces.size(); ++fi) {
                const FaceRef candidate{static_cast<int>(bi), static_cast<int>(fi)};
                if (candidate == current) {
                    continue;
                }
                if (std::find(selected.begin(), selected.end(), candidate) != selected.end()) {
                    continue;
                }
                if (brush.faces[fi].material != currentFace.material) {
                    continue;
                }
                if (facesShareEdge(currentFace, brush.faces[fi])) {
                    selected.push_back(candidate);
                }
            }
        }
    }

    if (selected.size() == d.selectedFaces.size()) {
        statusMessage = "No touching faces found";
        return;
    }
    d.selectedFaces = std::move(selected);
    d.activeFace = d.selectedFaces.back();
    statusMessage =
        "Selected touching faces (" + std::to_string(d.selectedFaces.size()) + ")";
}

void Editor::newMap(const std::string& mapName) {
    scene = EditorScene::Level;
    levelDoc.assetPath = mapName.empty() ? "untitled" : mapName;
    levelDoc.brushes.clear();
    levelDoc.instances.clear();
    levelDoc.things.clear();
    levelDoc.dirty = false;
    resetSelectionSerial(levelDoc);
    levelHistory.clear();
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
    prefabHistory.clear();
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
    levelHistory.clear();
    compileDirty = {};
    rebuildPreview(assets);
    reloadVisPreview(assets);
    fill = reloadLitBake(assets) ? PreviewFill::Lit : PreviewFill::Textures;
    resetCamera(*this);
    frameSelection();
    createBrushRole = slopengine::BrushRole::Hull;
    int invalidCount = 0;
    for (const slopengine::Brush& brush : levelDoc.brushes) {
        if (slopengine::validateBrushConvex(brush)) {
            ++invalidCount;
        }
    }
    statusMessage = "Loaded " + mapName + " (" + std::to_string(levelDoc.brushes.size()) +
        " brushes, " + std::to_string(levelDoc.instances.size()) + " prefabs, " +
        std::to_string(levelDoc.things.size()) + " things)";
    if (invalidCount > 0) {
        statusMessage += " — " + std::to_string(invalidCount) +
            " brush(es) failed validation (Edit → Validate Brushes)";
    }
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
    prefabHistory.clear();
    rebuildPreview(assets);
    resetCamera(*this);
    frameSelection();
    createBrushRole = slopengine::BrushRole::Detail;
    int invalidCount = 0;
    for (const slopengine::Brush& brush : prefabDoc.brushes) {
        if (slopengine::validateBrushConvex(brush)) {
            ++invalidCount;
        }
    }
    statusMessage =
        "Loaded prefab " + prefabPath + " (" + std::to_string(prefabDoc.brushes.size()) +
        " brushes, " + std::to_string(prefabDoc.things.size()) + " things)";
    if (invalidCount > 0) {
        statusMessage += " — " + std::to_string(invalidCount) +
            " brush(es) failed validation (Edit → Validate Brushes)";
    }
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
    levelHistory.markClean();
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
    prefabHistory.markClean();
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

void Editor::prepareEdit() {
    history().prepareEdit(doc());
}

void Editor::abortEdit() {
    history().abortEdit(doc());
}

void Editor::endEdit() {
    history().endEdit();
}

bool Editor::canUndo() const {
    return history().canUndo();
}

bool Editor::canRedo() const {
    return history().canRedo();
}

bool Editor::undo(slopengine::AssetStore& assets) {
    if (!history().undo(doc())) {
        return false;
    }
    markBspDirty();
    rebuildPreview(assets);
    statusMessage = "Undo";
    return true;
}

bool Editor::redo(slopengine::AssetStore& assets) {
    if (!history().redo(doc())) {
        return false;
    }
    markBspDirty();
    rebuildPreview(assets);
    statusMessage = "Redo";
    return true;
}

void Editor::markDirty() {
    doc().dirty = true;
    previewDirty = true;
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
        markRadDirty();
    }
}

void Editor::markThingCompileDirty(slopengine::ThingKind kind) {
    if (kind == slopengine::ThingKind::PointLight || kind == slopengine::ThingKind::SpotLight ||
        kind == slopengine::ThingKind::Sun || kind == slopengine::ThingKind::AmbientLight) {
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

bool Editor::cleanCompileData(
    slopengine::AssetStore& assets,
    const std::vector<CompileStage>& stages) {
    if (scene != EditorScene::Level) {
        statusMessage = "Clean compile data is only available in the Level scene";
        return false;
    }
    const std::string& mapName = levelDoc.assetPath;
    if (mapName.empty() || mapName == "untitled") {
        statusMessage = "Save the map before cleaning compile data";
        return false;
    }
    if (stages.empty()) {
        return false;
    }

    std::filesystem::path packageRoot = writePackageRoot;
    auto existing = assets.resolveOwned(slopengine::AssetKind::MapCsg, mapName + "/static");
    if (existing && existing->package != nullptr) {
        packageRoot = existing->package->root();
        writePackageRoot = packageRoot;
        writePackageId = existing->package->meta().id;
    }
    if (packageRoot.empty()) {
        statusMessage = "Clean failed: no package root";
        return false;
    }

    const std::filesystem::path mapDir = packageRoot / "maps" / mapName;
    bool removedAny = false;
    bool cleanBsp = false;
    bool cleanVis = false;
    bool cleanRad = false;
    for (const CompileStage stage : stages) {
        switch (stage) {
        case CompileStage::Bsp:
            cleanBsp = true;
            break;
        case CompileStage::Vis:
            cleanVis = true;
            break;
        case CompileStage::Rad:
            cleanRad = true;
            break;
        }
    }

    std::error_code ec;
    auto removePath = [&](const std::filesystem::path& path, bool recursive) {
        if (!std::filesystem::exists(path, ec)) {
            return;
        }
        if (recursive) {
            std::filesystem::remove_all(path, ec);
        } else {
            std::filesystem::remove(path, ec);
        }
        if (!ec) {
            removedAny = true;
        }
    };

    if (cleanBsp) {
        removePath(mapDir / "static.bsp", false);
        compileDirty.bsp = true;
    }
    if (cleanVis) {
        removePath(mapDir / "static.vis", false);
        preview.clearVis();
        compileDirty.vis = true;
        compileDirty.rad = true;
    }
    if (cleanRad) {
        removePath(mapDir / "rad", true);
        preview.clearLit();
        compileDirty.rad = true;
    }

    if (fill == PreviewFill::Lit || fill == PreviewFill::SolidLit) {
        if (!preview.litValid) {
            fill = preview.visValid ? PreviewFill::Unlit : PreviewFill::Textures;
        }
    } else if (fill == PreviewFill::Unlit && !preview.visValid) {
        fill = PreviewFill::Textures;
    }

    if (!removedAny) {
        statusMessage = "No compile data to clean for " + mapName;
        return false;
    }

    std::string cleaned;
    if (cleanBsp) {
        cleaned += "BSP";
    }
    if (cleanVis) {
        if (!cleaned.empty()) {
            cleaned += "+";
        }
        cleaned += "VIS";
    }
    if (cleanRad) {
        if (!cleaned.empty()) {
            cleaned += "+";
        }
        cleaned += "RAD";
    }
    statusMessage = "Cleaned " + cleaned + " for " + mapName;
    return true;
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
    previewDirty = false;
}

bool Editor::reloadVisPreview(slopengine::AssetStore& assets) {
    if (levelDoc.assetPath.empty() || levelDoc.assetPath == "untitled") {
        return false;
    }
    std::vector<slopengine::Brush> combined = levelDoc.brushes;
    combined.insert(
        combined.end(), expandedInstanceBrushes.begin(), expandedInstanceBrushes.end());
    slopengine::ThingDocument thingsDoc{};
    thingsDoc.things = levelDoc.things;
    const std::unordered_set<std::string> moverBrushIds =
        slopengine::collectClaimedBrushIds(&thingsDoc, combined);
    return preview.reloadVisPreview(assets, levelDoc.assetPath, combined, moverBrushIds);
}

bool Editor::reloadLitBake(slopengine::AssetStore& assets) {
    if (levelDoc.assetPath.empty() || levelDoc.assetPath == "untitled") {
        return false;
    }
    std::vector<slopengine::Brush> combined = levelDoc.brushes;
    combined.insert(
        combined.end(), expandedInstanceBrushes.begin(), expandedInstanceBrushes.end());
    slopengine::ThingDocument thingsDoc{};
    thingsDoc.things = levelDoc.things;
    const std::unordered_set<std::string> moverBrushIds =
        slopengine::collectClaimedBrushIds(&thingsDoc, combined);
    return preview.reloadBake(assets, levelDoc.assetPath, combined, moverBrushIds);
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

int Editor::viewportIndexForPlane(ViewPlane plane) {
    switch (plane) {
    case ViewPlane::Top:
        return 1;
    case ViewPlane::Front:
        return 2;
    case ViewPlane::Side:
        return 3;
    case ViewPlane::PerspectiveY0:
    default:
        return 0;
    }
}

ViewPlane Editor::planeForViewportIndex(int index) {
    switch (index) {
    case 1:
        return ViewPlane::Top;
    case 2:
        return ViewPlane::Front;
    case 3:
        return ViewPlane::Side;
    case 0:
    default:
        return ViewPlane::PerspectiveY0;
    }
}

void Editor::applyOrthoPoseToViewport(int index, Vector3 focus) {
    if (index < 0 || index >= kViewportCount) {
        return;
    }
    ViewportCamera& slot = viewports[static_cast<std::size_t>(index)];
    if (slot.plane == ViewPlane::PerspectiveY0) {
        return;
    }
    const float zoom = slot.camera.orthoHalfHeight;
    applyOrthoCameraPose(slot.camera, slot.plane, focus);
    slot.camera.orthoHalfHeight = zoom;
}

void Editor::applyOrthoPoses() {
    for (int i = 0; i < kViewportCount; ++i) {
        applyOrthoPoseToViewport(i, orthoFocus);
    }
}

void Editor::syncActiveCameraFromBank() {
    if (activeViewport < 0 || activeViewport >= kViewportCount) {
        activeViewport = 0;
    }
    viewPlane = viewports[activeViewport].plane;
    camera = viewports[activeViewport].camera;
    syncGridPlaneForView(*this, viewPlane);
}

void Editor::syncBankFromActiveCamera() {
    if (activeViewport < 0 || activeViewport >= kViewportCount) {
        activeViewport = 0;
    }
    viewports[activeViewport].camera = camera;
    viewports[activeViewport].plane = viewPlane;
}

void Editor::setActiveViewport(int index) {
    if (index < 0 || index >= kViewportCount) {
        return;
    }
    if (index == activeViewport) {
        syncActiveCameraFromBank();
        return;
    }
    syncBankFromActiveCamera();
    activeViewport = index;
    syncActiveCameraFromBank();
}

void Editor::setViewPlane(ViewPlane plane) {
    const int index = viewportIndexForPlane(plane);
    syncBankFromActiveCamera();
    if (viewPlane == ViewPlane::PerspectiveY0 && plane != ViewPlane::PerspectiveY0) {
        const FlyCamera& persp = viewports[0].camera;
        const Vector3 fwd = persp.forward();
        const Vector3 focus{
            persp.position.x + fwd.x * 8.0f,
            persp.position.y + fwd.y * 8.0f,
            persp.position.z + fwd.z * 8.0f,
        };
        orthoFocus = focus;
        applyOrthoPoseToViewport(index, focus);
    }
    setActiveViewport(index);
}

void Editor::toggleViewportLayout() {
    if (viewportLayout == ViewportLayout::Single) {
        viewportLayout = ViewportLayout::Quad;
        statusMessage = "Viewport: Quad";
    } else {
        viewportLayout = ViewportLayout::Single;
        statusMessage = "Viewport: Single";
    }
    syncActiveCameraFromBank();
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

bool Editor::renameBrush(int index, std::string_view newId) {
    EditorDocument& d = doc();
    if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
        return false;
    }

    std::string id(newId);
    while (!id.empty() && std::isspace(static_cast<unsigned char>(id.front()))) {
        id.erase(id.begin());
    }
    while (!id.empty() && std::isspace(static_cast<unsigned char>(id.back()))) {
        id.pop_back();
    }
    if (id.empty() || id.find('/') != std::string::npos) {
        statusMessage = "Brush id must be non-empty and cannot contain '/'";
        return false;
    }

    slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(index)];
    if (id == brush.id) {
        return false;
    }
    for (std::size_t i = 0; i < d.brushes.size(); ++i) {
        if (static_cast<int>(i) != index && d.brushes[i].id == id) {
            statusMessage = "Brush id already in use: " + id;
            return false;
        }
    }

    const std::string oldId = brush.id;
    const std::string oldPrefix = oldId + "/";
    const std::string newPrefix = id + "/";
    prepareEdit();
    brush.id = id;
    for (slopengine::BrushFace& face : brush.faces) {
        if (face.id.rfind(oldPrefix, 0) == 0) {
            face.id = newPrefix + face.id.substr(oldPrefix.size());
        } else if (face.id == oldId) {
            face.id = id;
        }
    }
    for (slopengine::Thing& thing : d.things) {
        if (thing.brush == oldId) {
            thing.brush = id;
        }
    }
    if (id.rfind("brush-", 0) == 0) {
        try {
            const int serial = std::stoi(id.substr(6));
            d.nextBrushSerial = std::max(d.nextBrushSerial, serial + 1);
        } catch (...) {
        }
    }

    markDirty();
    markRadDirty();
    markBrushCompileDirty(brush.role);
    statusMessage = "Renamed brush " + oldId + " -> " + id;
    return true;
}

void Editor::frameSelection() {
    const Vector3 center = selectionCenter();
    syncBankFromActiveCamera();
    orthoFocus = center;
    applyOrthoPoses();
    FlyCamera& persp = viewports[0].camera;
    persp.position = {center.x, center.y + 2.5f, center.z + 8.0f};
    persp.lookAt(center);
    syncActiveCameraFromBank();
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
    if (d.selectionMode == SelectionMode::Face && !d.selectedFaces.empty()) {
        Vector3 sum{};
        int count = 0;
        for (const FaceRef& ref : d.selectedFaces) {
            if (!ref.valid() || ref.brush >= static_cast<int>(d.brushes.size())) {
                continue;
            }
            const slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(ref.brush)];
            if (ref.face >= static_cast<int>(brush.faces.size())) {
                continue;
            }
            const auto& verts = brush.faces[static_cast<std::size_t>(ref.face)].vertices;
            for (const Vector3& v : verts) {
                sum = {sum.x + v.x, sum.y + v.y, sum.z + v.z};
                ++count;
            }
        }
        if (count > 0) {
            const float inv = 1.0f / static_cast<float>(count);
            return {sum.x * inv, sum.y * inv, sum.z * inv};
        }
    }
    if (d.selectionMode == SelectionMode::Vert && !d.selectedVerts.empty()) {
        Vector3 sum{};
        int count = 0;
        for (const VertRef& ref : d.selectedVerts) {
            if (!ref.valid() || ref.brush >= static_cast<int>(d.brushes.size())) {
                continue;
            }
            const slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(ref.brush)];
            if (ref.face >= static_cast<int>(brush.faces.size())) {
                continue;
            }
            const auto& verts = brush.faces[static_cast<std::size_t>(ref.face)].vertices;
            if (ref.vert < 0 || ref.vert >= static_cast<int>(verts.size())) {
                continue;
            }
            const Vector3& v = verts[static_cast<std::size_t>(ref.vert)];
            sum = {sum.x + v.x, sum.y + v.y, sum.z + v.z};
            ++count;
        }
        if (count > 0) {
            const float inv = 1.0f / static_cast<float>(count);
            return {sum.x * inv, sum.y * inv, sum.z * inv};
        }
    }
    if (d.selectionMode == SelectionMode::Edge && !d.selectedEdges.empty()) {
        Vector3 sum{};
        int count = 0;
        for (const EdgeRef& ref : d.selectedEdges) {
            if (!ref.valid() || ref.brush >= static_cast<int>(d.brushes.size())) {
                continue;
            }
            const slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(ref.brush)];
            if (ref.face >= static_cast<int>(brush.faces.size())) {
                continue;
            }
            const auto& verts = brush.faces[static_cast<std::size_t>(ref.face)].vertices;
            if (verts.size() < 2 || ref.edge < 0 ||
                ref.edge >= static_cast<int>(verts.size())) {
                continue;
            }
            const Vector3& a = verts[static_cast<std::size_t>(ref.edge)];
            const Vector3& b = verts[static_cast<std::size_t>((ref.edge + 1) % verts.size())];
            sum = {sum.x + 0.5f * (a.x + b.x), sum.y + 0.5f * (a.y + b.y), sum.z + 0.5f * (a.z + b.z)};
            ++count;
        }
        if (count > 0) {
            const float inv = 1.0f / static_cast<float>(count);
            return {sum.x * inv, sum.y * inv, sum.z * inv};
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

void Editor::convertSelectedBrushesToTriggers() {
    EditorDocument& d = doc();
    if (d.selectionMode != SelectionMode::Brush || d.selectedBrushes.empty()) {
        return;
    }

    std::vector<int> indices = d.selectedBrushes;
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

    prepareEdit();
    std::vector<EntityRef> created;
    for (int index : indices) {
        if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
            continue;
        }
        const slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(index)];

        slopengine::Thing thing{};
        thing.kind = slopengine::ThingKind::Trigger;
        thing.id = allocateThingId("trigger");
        thing.at = {
            0.5f * (brush.mins.x + brush.maxs.x),
            0.5f * (brush.mins.y + brush.maxs.y),
            0.5f * (brush.mins.z + brush.maxs.z),
        };
        thing.haveAt = true;
        thing.triggerSize = {
            brush.maxs.x - brush.mins.x,
            brush.maxs.y - brush.mins.y,
            brush.maxs.z - brush.mins.z,
        };
        if (thing.triggerSize.x < 1e-4f) {
            thing.triggerSize.x = 1.0f;
        }
        if (thing.triggerSize.y < 1e-4f) {
            thing.triggerSize.y = 1.0f;
        }
        if (thing.triggerSize.z < 1e-4f) {
            thing.triggerSize.z = 1.0f;
        }
        thing.haveTriggerSize = true;

        d.things.push_back(std::move(thing));
        created.push_back({EntityRef::Kind::Thing, static_cast<int>(d.things.size()) - 1});
    }

    if (created.empty()) {
        abortEdit();
        return;
    }

    std::sort(indices.begin(), indices.end(), std::greater<int>());
    for (int index : indices) {
        if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
            continue;
        }
        d.brushes.erase(d.brushes.begin() + index);
    }

    clearSelection();
    for (std::size_t i = 0; i < created.size(); ++i) {
        selectEntity(created[i], i > 0);
    }
    markDirty();
    markBspDirty();
    markThingCompileDirty(slopengine::ThingKind::Trigger);
    endEdit();
    statusMessage = created.size() == 1
        ? "Converted brush to trigger — set on-enter in Properties"
        : "Converted " + std::to_string(created.size()) +
            " brushes to triggers — set on-enter in Properties";
}

void Editor::convertSelectedBrushesToMovers() {
    EditorDocument& d = doc();
    if (d.selectionMode != SelectionMode::Brush || d.selectedBrushes.empty()) {
        return;
    }

    std::vector<int> indices = d.selectedBrushes;
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

    prepareEdit();
    std::vector<EntityRef> created;
    int skipped = 0;
    for (int index : indices) {
        if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
            continue;
        }
        slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(index)];
        if (brush.role != slopengine::BrushRole::Detail) {
            ++skipped;
            continue;
        }
        if (brush.id.empty()) {
            ++skipped;
            continue;
        }

        bool alreadyClaimed = false;
        for (const slopengine::Thing& existing : d.things) {
            if (existing.kind == slopengine::ThingKind::Mover && existing.brush == brush.id) {
                alreadyClaimed = true;
                break;
            }
        }
        if (alreadyClaimed) {
            ++skipped;
            continue;
        }

        const float height = brush.maxs.y - brush.mins.y;
        slopengine::Thing thing{};
        thing.kind = slopengine::ThingKind::Mover;
        thing.id = allocateThingId("plat");
        thing.brush = brush.id;
        thing.at = {
            0.5f * (brush.mins.x + brush.maxs.x),
            0.5f * (brush.mins.y + brush.maxs.y),
            0.5f * (brush.mins.z + brush.maxs.z),
        };
        thing.haveAt = true;
        thing.haveMoverOpenOffset = true;
        thing.moverOpenOffset = {0.0f, height > 0.1f ? height + 0.1f : 2.2f, 0.0f};
        thing.haveMoverDuration = true;
        thing.moverDuration = 0.6f;
        thing.moverBlockMode = "shove";
        thing.havePrompt = true;
        thing.prompt = "Open";
        thing.onUse = slopengine::HandlerBinding{"on-use-mover-toggle", {}};

        d.things.push_back(std::move(thing));
        created.push_back({EntityRef::Kind::Thing, static_cast<int>(d.things.size()) - 1});
    }

    if (created.empty()) {
        abortEdit();
        statusMessage = skipped > 0
            ? "Convert to Mover needs unclaimed detail brushes with ids"
            : "No brushes selected";
        return;
    }

    clearSelection();
    for (std::size_t i = 0; i < created.size(); ++i) {
        selectEntity(created[i], i > 0);
    }
    markDirty();
    markRadDirty();
    markThingCompileDirty(slopengine::ThingKind::Mover);
    endEdit();
    statusMessage = created.size() == 1
        ? "Created mover claiming brush leaf — brush stays in CSG"
        : "Created " + std::to_string(created.size()) +
            " movers claiming brush leaves — brushes stay in CSG";
}

void Editor::setSelectedBrushesAsDoors() {
    EditorDocument& d = doc();
    if (d.selectionMode != SelectionMode::Brush || d.selectedBrushes.empty()) {
        return;
    }

    std::vector<int> indices = d.selectedBrushes;
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

    prepareEdit();
    int updated = 0;
    int skipped = 0;
    for (int index : indices) {
        if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
            continue;
        }
        slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(index)];
        if (brush.id.empty()) {
            ++skipped;
            continue;
        }
        bool claimedByMover = false;
        for (const slopengine::Thing& existing : d.things) {
            if (existing.kind == slopengine::ThingKind::Mover && existing.brush == brush.id) {
                claimedByMover = true;
                break;
            }
        }
        if (claimedByMover) {
            ++skipped;
            continue;
        }

        const slopengine::BrushRole previous = brush.role;
        const bool wasDoor = brush.role == slopengine::BrushRole::Door;
        brush.role = slopengine::BrushRole::Door;
        slopengine::setBrushBlocks(brush, slopengine::brushRoleDefaultBlocks(brush.role));
        if (!wasDoor) {
            brush.door = slopengine::BrushDoor{};
            brush.door.motion = slopengine::DoorMotion::Raise;
            brush.door.haveDuration = true;
            brush.door.duration = 0.6f;
        }
        markBrushCompileDirty(previous);
        markBrushCompileDirty(brush.role);
        ++updated;
    }

    if (updated == 0) {
        abortEdit();
        statusMessage = skipped > 0
            ? "Set as Door needs brushes with ids (not mover-claimed)"
            : "No brushes selected";
        return;
    }

    markDirty();
    markRadDirty();
    endEdit();
    statusMessage = updated == 1
        ? "Set 1 brush as door"
        : "Set " + std::to_string(updated) + " brushes as doors";
}

void Editor::toggleSelectedBrushRole() {
    EditorDocument& d = doc();
    if (d.selectionMode != SelectionMode::Brush || d.selectedBrushes.empty()) {
        return;
    }
    prepareEdit();
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
            brush.role = slopengine::BrushRole::Door;
            break;
        case slopengine::BrushRole::Door:
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
            brush.role = slopengine::BrushRole::Transparent;
            break;
        case slopengine::BrushRole::Transparent:
            brush.role = slopengine::BrushRole::Hull;
            break;
        }
        slopengine::setBrushBlocks(brush, slopengine::brushRoleDefaultBlocks(brush.role));
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
        markRadDirty();
    }
    endEdit();
    statusMessage = std::string("Role: ") + slopengine::brushRoleName(lastRole) + " (" + lastId + ")";
}

}
