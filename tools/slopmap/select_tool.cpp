#include "select_tool.hpp"

#include "assets/material_loader.hpp"
#include "map/brush.hpp"
#include "map/csg_compile.hpp"
#include "map/fac.hpp"
#include "map/mover_brushes.hpp"
#include "map/uv_math.hpp"

#include <raymath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_set>

namespace slopmap {

namespace {

constexpr float kWeldEpsilon = 1e-3f;
constexpr float kVertPickPixels = 10.0f;
constexpr float kEdgePickPixels = 8.0f;

bool vertsNear(Vector3 a, Vector3 b, float epsilon = kWeldEpsilon) {
    return std::fabs(a.x - b.x) <= epsilon && std::fabs(a.y - b.y) <= epsilon &&
        std::fabs(a.z - b.z) <= epsilon;
}

Vector3 vertRefPosition(const EditorDocument& d, VertRef ref) {
    if (!ref.valid() || ref.brush >= static_cast<int>(d.brushes.size())) {
        return {};
    }
    const slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(ref.brush)];
    if (ref.face >= static_cast<int>(brush.faces.size())) {
        return {};
    }
    const auto& verts = brush.faces[static_cast<std::size_t>(ref.face)].vertices;
    if (ref.vert < 0 || ref.vert >= static_cast<int>(verts.size())) {
        return {};
    }
    return verts[static_cast<std::size_t>(ref.vert)];
}

// Finds the vertex of the selected brushes nearest (in screen space) to the mouse, so a
// translate/rotate can anchor on an actual corner instead of the selection's bounding-box
// center -- otherwise irregular brushes have no way to land their vertices on the grid.
std::optional<VertRef> nearestSelectedVertexToScreen(
    const EditorDocument& d,
    const Camera3D& camera,
    Rectangle viewport,
    Vector2 screenPoint) {
    std::optional<VertRef> best;
    float bestDistSq = std::numeric_limits<float>::max();
    for (int index : d.selectedBrushes) {
        if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
            continue;
        }
        const slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(index)];
        for (std::size_t f = 0; f < brush.faces.size(); ++f) {
            const auto& verts = brush.faces[f].vertices;
            for (std::size_t v = 0; v < verts.size(); ++v) {
                const Vector2 screen = worldToViewportScreen(verts[v], camera, viewport);
                const float dx = screen.x - screenPoint.x;
                const float dy = screen.y - screenPoint.y;
                const float distSq = dx * dx + dy * dy;
                if (distSq < bestDistSq) {
                    bestDistSq = distSq;
                    best = VertRef{index, static_cast<int>(f), static_cast<int>(v)};
                }
            }
        }
    }
    return best;
}

float facePixelsPerMeter(slopengine::AssetStore& assets, std::string_view materialPath) {
    const slopengine::MaterialAsset* asset = assets.getMaterialAsset(materialPath);
    if (asset != nullptr && asset->pixelsPerMeter > 0.0f) {
        return asset->pixelsPerMeter;
    }
    return 64.0f;
}

void compensateUvLocks(
    const slopengine::Brush& src,
    slopengine::Brush& dst,
    slopengine::AssetStore& assets) {
    for (std::size_t i = 0; i < dst.faces.size(); ++i) {
        slopengine::BrushFace& face = dst.faces[i];
        if (!face.uvLock) {
            continue;
        }
        const slopengine::BrushFace* oldFace = nullptr;
        if (i < src.faces.size() && src.faces[i].id == face.id) {
            oldFace = &src.faces[i];
        } else {
            for (const slopengine::BrushFace& candidate : src.faces) {
                if (candidate.id == face.id) {
                    oldFace = &candidate;
                    break;
                }
            }
        }
        if (oldFace == nullptr || oldFace->vertices.empty()) {
            continue;
        }
        Vector3 oldU = oldFace->uvUAxis;
        Vector3 oldV = oldFace->uvVAxis;
        if (oldU.x == 0.0f && oldU.y == 0.0f && oldU.z == 0.0f) {
            slopengine::axialUvAxes(oldFace->normal, oldU, oldV);
        }
        face.uvUAxis = oldU;
        face.uvVAxis = oldV;
        slopengine::ensureFaceUvAxes(face);
        slopengine::lockFaceUvShift(
            face,
            oldFace->vertices[0],
            oldU,
            oldV,
            facePixelsPerMeter(assets, face.material));
    }
}

Vector3 currentTranslateDelta(const Editor& editor, const SelectTool& tool) {
    const EditorDocument& d = editor.doc();
    if (d.selectionMode == SelectionMode::Entity && !tool.entitySnapshotRefs.empty() &&
        !tool.entityAtSnapshots.empty()) {
        const EntityRef& ref = tool.entitySnapshotRefs[0];
        if (ref.kind == EntityRef::Kind::Instance &&
            ref.index >= 0 && ref.index < static_cast<int>(d.instances.size())) {
            return sub3(
                d.instances[static_cast<std::size_t>(ref.index)].at,
                tool.entityAtSnapshots[0]);
        }
        if (ref.kind == EntityRef::Kind::Thing &&
            ref.index >= 0 && ref.index < static_cast<int>(d.things.size())) {
            const auto& thing = d.things[static_cast<std::size_t>(ref.index)];
            return sub3(thing.haveAt ? thing.at : Vector3{}, tool.entityAtSnapshots[0]);
        }
    }
    if (d.selectionMode == SelectionMode::Face && d.activeFace.valid() &&
        !tool.brushSnapshot.empty() && !tool.brushSnapshotIndices.empty()) {
        for (std::size_t i = 0; i < tool.brushSnapshotIndices.size(); ++i) {
            if (tool.brushSnapshotIndices[i] != d.activeFace.brush) {
                continue;
            }
            const slopengine::Brush& src = tool.brushSnapshot[i];
            const int faceIndex = d.activeFace.face;
            if (faceIndex < 0 || faceIndex >= static_cast<int>(src.faces.size())) {
                break;
            }
            const auto& oldFace = src.faces[static_cast<std::size_t>(faceIndex)];
            const int brushIndex = tool.brushSnapshotIndices[i];
            if (brushIndex >= 0 && brushIndex < static_cast<int>(d.brushes.size()) &&
                faceIndex < static_cast<int>(
                    d.brushes[static_cast<std::size_t>(brushIndex)].faces.size()) &&
                !oldFace.vertices.empty()) {
                const auto& newFace =
                    d.brushes[static_cast<std::size_t>(brushIndex)]
                        .faces[static_cast<std::size_t>(faceIndex)];
                if (!newFace.vertices.empty()) {
                    return sub3(newFace.vertices[0], oldFace.vertices[0]);
                }
            }
            break;
        }
    }
    if ((d.selectionMode == SelectionMode::Vert || d.selectionMode == SelectionMode::Edge) &&
        !tool.brushSnapshot.empty() && !tool.brushSnapshotIndices.empty()) {
        auto resolvePos = [&](int brushIndex, int faceIndex, int vertIndex) -> std::optional<Vector3> {
            if (brushIndex < 0 || brushIndex >= static_cast<int>(d.brushes.size()) ||
                faceIndex < 0 || vertIndex < 0) {
                return std::nullopt;
            }
            const slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(brushIndex)];
            if (faceIndex >= static_cast<int>(brush.faces.size())) {
                return std::nullopt;
            }
            const auto& verts = brush.faces[static_cast<std::size_t>(faceIndex)].vertices;
            if (vertIndex >= static_cast<int>(verts.size())) {
                return std::nullopt;
            }
            return verts[static_cast<std::size_t>(vertIndex)];
        };
        auto resolveSnapPos =
            [&](const slopengine::Brush& snap, int faceIndex, int vertIndex) -> std::optional<Vector3> {
            if (faceIndex < 0 || vertIndex < 0 ||
                faceIndex >= static_cast<int>(snap.faces.size())) {
                return std::nullopt;
            }
            const auto& verts = snap.faces[static_cast<std::size_t>(faceIndex)].vertices;
            if (vertIndex >= static_cast<int>(verts.size())) {
                return std::nullopt;
            }
            return verts[static_cast<std::size_t>(vertIndex)];
        };
        if (d.selectionMode == SelectionMode::Vert && d.activeVert.valid()) {
            for (std::size_t i = 0; i < tool.brushSnapshotIndices.size(); ++i) {
                if (tool.brushSnapshotIndices[i] != d.activeVert.brush) {
                    continue;
                }
                const auto oldPos = resolveSnapPos(
                    tool.brushSnapshot[i], d.activeVert.face, d.activeVert.vert);
                const auto newPos =
                    resolvePos(d.activeVert.brush, d.activeVert.face, d.activeVert.vert);
                if (oldPos && newPos) {
                    return sub3(*newPos, *oldPos);
                }
            }
        }
        if (d.selectionMode == SelectionMode::Edge && d.activeEdge.valid()) {
            for (std::size_t i = 0; i < tool.brushSnapshotIndices.size(); ++i) {
                if (tool.brushSnapshotIndices[i] != d.activeEdge.brush) {
                    continue;
                }
                const auto oldPos = resolveSnapPos(
                    tool.brushSnapshot[i], d.activeEdge.face, d.activeEdge.edge);
                const auto newPos =
                    resolvePos(d.activeEdge.brush, d.activeEdge.face, d.activeEdge.edge);
                if (oldPos && newPos) {
                    return sub3(*newPos, *oldPos);
                }
            }
        }
    }
    if (!tool.brushSnapshot.empty() && !tool.brushSnapshotIndices.empty()) {
        const int index = tool.brushSnapshotIndices[0];
        if (index >= 0 && index < static_cast<int>(d.brushes.size())) {
            return sub3(
                d.brushes[static_cast<std::size_t>(index)].mins,
                tool.brushSnapshot[0].mins);
        }
    }
    return {};
}

std::optional<Vector3> constrainedTranslateAxis(
    const Editor& editor,
    const SelectTool& tool) {
    if (editor.doc().selectionMode == SelectionMode::Face &&
        editor.transformSpace == TransformSpace::Relative && editor.doc().activeFace.valid() &&
        !tool.brushSnapshot.empty()) {
        for (std::size_t i = 0; i < tool.brushSnapshotIndices.size(); ++i) {
            if (tool.brushSnapshotIndices[i] != editor.doc().activeFace.brush) {
                continue;
            }
            const int faceIndex = editor.doc().activeFace.face;
            const slopengine::Brush& src = tool.brushSnapshot[i];
            if (faceIndex >= 0 && faceIndex < static_cast<int>(src.faces.size())) {
                return normalize3(src.faces[static_cast<std::size_t>(faceIndex)].normal);
            }
            break;
        }
    }
    switch (tool.axisLock) {
    case TranslateAxis::X:
        return Vector3{1.0f, 0.0f, 0.0f};
    case TranslateAxis::Y:
        return Vector3{0.0f, 1.0f, 0.0f};
    case TranslateAxis::Z:
        return Vector3{0.0f, 0.0f, 1.0f};
    case TranslateAxis::None:
        break;
    }
    return std::nullopt;
}

Vector2 screenAxisForWorldAxis(
    Vector3 origin,
    Vector3 axis,
    const Camera3D& camera,
    Rectangle viewport) {
    const Vector2 s0 = worldToViewportScreen(origin, camera, viewport);
    const Vector2 s1 = worldToViewportScreen(add3(origin, axis), camera, viewport);
    return {s1.x - s0.x, s1.y - s0.y};
}

Vector3 orientAxisForScreen(
    Vector3 axis,
    Vector3 origin,
    const Camera3D& camera,
    Rectangle viewport) {
    const Vector2 axisScreen = screenAxisForWorldAxis(origin, axis, camera, viewport);
    const float lenSq = axisScreen.x * axisScreen.x + axisScreen.y * axisScreen.y;
    if (lenSq < 4.0f) {
        return axis;
    }
    if (std::fabs(axisScreen.x) >= std::fabs(axisScreen.y)) {
        if (axisScreen.x < 0.0f) {
            return scale3(axis, -1.0f);
        }
    } else if (axisScreen.y < 0.0f) {
        return scale3(axis, -1.0f);
    }
    return axis;
}

Vector3 computePlaneTranslateDelta(
    Vector3 origin,
    Vector2 mouseGrabScreen,
    const Editor& editor,
    const Camera3D& camera,
    Rectangle viewport,
    const ConstructionPlane& plane) {
    const Vector2 mouse = toolMouseScreen(editor);
    const Vector2 mouseDelta{mouse.x - mouseGrabScreen.x, mouse.y - mouseGrabScreen.y};
    const float dist = std::max(length3(sub3(camera.position, origin)), 0.25f);
    const float scale = dist * 0.0025f;

    const Vector2 uScreen = screenAxisForWorldAxis(origin, plane.axisU, camera, viewport);
    const Vector2 vScreen = screenAxisForWorldAxis(origin, plane.axisV, camera, viewport);
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
    return add3(scale3(plane.axisU, uAmount * scale), scale3(plane.axisV, vAmount * scale));
}

Vector3 computeAxisTranslateDelta(
    Vector3 axis,
    Vector3 origin,
    Vector2 mouseGrabScreen,
    const Editor& editor,
    const Camera3D& camera,
    Rectangle viewport,
    const ConstructionPlane* fallbackPlane) {
    axis = orientAxisForScreen(axis, origin, camera, viewport);
    const Vector2 mouse = toolMouseScreen(editor);
    const Vector2 axisScreen = screenAxisForWorldAxis(origin, axis, camera, viewport);
    const float lenSq = axisScreen.x * axisScreen.x + axisScreen.y * axisScreen.y;
    const Vector2 mouseDelta{mouse.x - mouseGrabScreen.x, mouse.y - mouseGrabScreen.y};
    float amount = 0.0f;
    if (lenSq < 4.0f) {
        if (fallbackPlane != nullptr) {
            const Vector3 planeDelta = computePlaneTranslateDelta(
                origin, mouseGrabScreen, editor, camera, viewport, *fallbackPlane);
            return scale3(axis, dot3(planeDelta, axis));
        }
        const float dist = std::max(length3(sub3(camera.position, origin)), 0.25f);
        amount = -mouseDelta.y * dist * 0.0025f;
    } else {
        amount = (mouseDelta.x * axisScreen.x + mouseDelta.y * axisScreen.y) / lenSq;
    }
    return scale3(axis, amount);
}

Vector3 cameraRight(const Camera3D& camera) {
    return normalize3(cross3(cameraForward(camera), camera.up));
}

Vector3 computeFreeTranslateDelta(
    Vector3 origin,
    Vector2 mouseGrabScreen,
    const Editor& editor,
    const Camera3D& camera) {
    if (editor.viewPlane != ViewPlane::PerspectiveY0) {
        const ConstructionPlane plane =
            constructionPlaneForView(editor.viewPlane, editor.gridPlane);
        return computePlaneTranslateDelta(
            origin, mouseGrabScreen, editor, camera, editor.contentViewport, plane);
    }
    const Vector2 mouse = toolMouseScreen(editor);
    const Vector2 mouseDelta{mouse.x - mouseGrabScreen.x, mouse.y - mouseGrabScreen.y};
    const float dist = std::max(length3(sub3(camera.position, origin)), 0.25f);
    const float scale = dist * 0.0025f;
    const Vector3 right = cameraRight(camera);
    const Vector3 up = normalize3(camera.up);
    return add3(scale3(right, mouseDelta.x * scale), scale3(up, -mouseDelta.y * scale));
}

void captureTranslateGrab(SelectTool& tool, Editor& editor, const Camera3D& camera) {
    tool.mouseGrabScreen = toolMouseScreen(editor);
    const Vector3 applied = currentTranslateDelta(editor, tool);

    if (const auto axis = constrainedTranslateAxis(editor, tool)) {
        const Vector3 oriented =
            orientAxisForScreen(*axis, tool.translateOrigin, camera, editor.contentViewport);
        const Vector2 axisScreen = screenAxisForWorldAxis(
            tool.translateOrigin, oriented, camera, editor.contentViewport);
        const float appliedAmount = dot3(applied, oriented);
        const Vector2 mouse = tool.mouseGrabScreen;
        tool.mouseGrabScreen = {
            mouse.x - axisScreen.x * appliedAmount,
            mouse.y - axisScreen.y * appliedAmount,
        };
        tool.mouseGrabWorld = {};
    } else {
        tool.mouseGrabWorld = applied;
    }
}

void refreshTranslateGrab(SelectTool& tool, Editor& editor, const Camera3D& camera) {
    captureTranslateGrab(tool, editor, camera);
}

const char* translateTargetName(const Editor& editor) {
    const EditorDocument& d = editor.doc();
    if (d.selectionMode == SelectionMode::Entity) {
        return "Thing";
    }
    if (d.selectionMode == SelectionMode::Face) {
        return "Face";
    }
    if (d.selectionMode == SelectionMode::Edge) {
        return "Edge";
    }
    if (d.selectionMode == SelectionMode::Vert) {
        return "Vert";
    }
    return "Brush";
}

const char* translateAxisName(const Editor& editor, TranslateAxis axisLock) {
    if (editor.doc().selectionMode == SelectionMode::Face &&
        editor.transformSpace == TransformSpace::Relative) {
        return "Normal";
    }
    switch (axisLock) {
    case TranslateAxis::X: return "X";
    case TranslateAxis::Y: return "Y";
    case TranslateAxis::Z: return "Z";
    case TranslateAxis::None: break;
    }
    return "Free";
}

const char* transformSpaceName(TransformSpace space) {
    switch (space) {
    case TransformSpace::Global:
        return "Global";
    case TransformSpace::Relative:
        return "Relative";
    }
    return "Relative";
}

const char* translateSnapModeName(TranslateSnapMode mode) {
    switch (mode) {
    case TranslateSnapMode::Offset:
        return "Offset";
    case TranslateSnapMode::Absolute:
        return "Absolute";
    }
    return "Offset";
}

bool bufferHasDigit(const std::string& buffer) {
    for (char c : buffer) {
        if (c >= '0' && c <= '9') {
            return true;
        }
    }
    return false;
}

Vector3 snapTranslateDelta(Vector3 rawDelta, Vector3 origin, float grid, TranslateSnapMode mode) {
    if (mode == TranslateSnapMode::Absolute) {
        return sub3(snapToGrid(add3(origin, rawDelta), grid), origin);
    }
    return snapToGrid(rawDelta, grid);
}

float snapTranslateDistance(
    float rawDistance,
    float originAlong,
    float grid,
    TranslateSnapMode mode) {
    if (mode == TranslateSnapMode::Absolute) {
        return snapToGrid(originAlong + rawDistance, grid) - originAlong;
    }
    return snapToGrid(rawDistance, grid);
}

void updateTranslateStatus(Editor& editor, const SelectTool& tool) {
    std::string message = "Translate ";
    message += translateTargetName(editor);
    message += " | ";
    message += transformSpaceName(editor.transformSpace);
    message += " | ";
    message += translateAxisName(editor, tool.axisLock);
    message += " | ";
    message += translateSnapModeName(editor.translateSnapMode);
    message += " | ";
    if (tool.numericLocked(editor)) {
        message += editor.numericBuffer;
    } else if (!editor.numericBuffer.empty()) {
        message += "drag ";
        message += editor.numericBuffer;
    } else {
        message += "drag";
    }
    message += "  (Enter confirm, Esc cancel, O snap)";
    editor.statusMessage = std::move(message);
}

bool rayAabb(Ray ray, Vector3 mins, Vector3 maxs, float& outT) {
    float tmin = 0.0f;
    float tmax = std::numeric_limits<float>::max();
    const float* ro = &ray.position.x;
    const float* rd = &ray.direction.x;
    const float* bmin = &mins.x;
    const float* bmax = &maxs.x;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(rd[i]) < 1e-8f) {
            if (ro[i] < bmin[i] || ro[i] > bmax[i]) {
                return false;
            }
            continue;
        }
        float t1 = (bmin[i] - ro[i]) / rd[i];
        float t2 = (bmax[i] - ro[i]) / rd[i];
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) {
            return false;
        }
    }
    outT = tmin;
    return tmax >= 0.0f;
}

bool pointInTriangle(Vector3 p, Vector3 a, Vector3 b, Vector3 c) {
    const Vector3 v0 = sub3(c, a);
    const Vector3 v1 = sub3(b, a);
    const Vector3 v2 = sub3(p, a);
    const float dot00 = dot3(v0, v0);
    const float dot01 = dot3(v0, v1);
    const float dot02 = dot3(v0, v2);
    const float dot11 = dot3(v1, v1);
    const float dot12 = dot3(v1, v2);
    const float denom = dot00 * dot11 - dot01 * dot01;
    if (std::fabs(denom) < 1e-10f) {
        return false;
    }
    const float u = (dot11 * dot02 - dot01 * dot12) / denom;
    const float v = (dot00 * dot12 - dot01 * dot02) / denom;
    return u >= -1e-4f && v >= -1e-4f && (u + v) <= 1.0f + 1e-4f;
}

bool rayTriangle(Ray ray, Vector3 a, Vector3 b, Vector3 c, float& outT) {
    const Vector3 edge1 = sub3(b, a);
    const Vector3 edge2 = sub3(c, a);
    const Vector3 h{
        ray.direction.y * edge2.z - ray.direction.z * edge2.y,
        ray.direction.z * edge2.x - ray.direction.x * edge2.z,
        ray.direction.x * edge2.y - ray.direction.y * edge2.x,
    };
    const float det = dot3(edge1, h);
    if (std::fabs(det) < 1e-8f) {
        return false;
    }
    const float invDet = 1.0f / det;
    const Vector3 s = sub3(ray.position, a);
    const float u = invDet * dot3(s, h);
    if (u < 0.0f || u > 1.0f) {
        return false;
    }
    const Vector3 q{
        s.y * edge1.z - s.z * edge1.y,
        s.z * edge1.x - s.x * edge1.z,
        s.x * edge1.y - s.y * edge1.x,
    };
    const float v = invDet * dot3(ray.direction, q);
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }
    const float t = invDet * dot3(edge2, q);
    if (t < 0.0f) {
        return false;
    }
    outT = t;
    (void)pointInTriangle;
    return true;
}

slopengine::Brush makeBoxAt(
    const slopengine::Brush& src,
    Vector3 mins,
    Vector3 maxs,
    slopengine::AssetStore& assets) {
    std::string material = src.faces.empty() ? "default/cube" : src.faces.front().material;
    std::vector<std::pair<slopengine::BrushBoxSide, slopengine::BrushFace>> overrides;
    constexpr slopengine::BrushBoxSide kSides[] = {
        slopengine::BrushBoxSide::Top,
        slopengine::BrushBoxSide::Bottom,
        slopengine::BrushBoxSide::North,
        slopengine::BrushBoxSide::South,
        slopengine::BrushBoxSide::East,
        slopengine::BrushBoxSide::West,
    };
    for (slopengine::BrushBoxSide side : kSides) {
        const std::string suffix = std::string("/") + slopengine::brushBoxSideName(side);
        for (const slopengine::BrushFace& face : src.faces) {
            if (face.id.size() >= suffix.size() &&
                face.id.compare(face.id.size() - suffix.size(), suffix.size(), suffix) == 0) {
                if (face.nodraw || face.uvLock || face.uvShiftPixels.x != 0.0f ||
                    face.uvShiftPixels.y != 0.0f || face.uvScale.x != 1.0f ||
                    face.uvScale.y != 1.0f || face.material != material ||
                    face.id != src.id + suffix || !face.onUse.empty() ||
                    !face.onTouch.empty()) {
                    slopengine::BrushFace overrideFace;
                    overrideFace.id = face.id;
                    overrideFace.material = face.material;
                    overrideFace.uvShiftPixels = face.uvShiftPixels;
                    overrideFace.uvScale = face.uvScale;
                    overrideFace.nodraw = face.nodraw;
                    overrideFace.uvLock = face.uvLock;
                    overrideFace.uvUAxis = face.uvUAxis;
                    overrideFace.uvVAxis = face.uvVAxis;
                    overrideFace.onUse = face.onUse;
                    overrideFace.onTouch = face.onTouch;
                    overrides.emplace_back(side, overrideFace);
                }
                break;
            }
        }
    }
    slopengine::Brush brush =
        slopengine::makeBrushBox(src.id, mins, maxs, material, overrides, src.role);
    brush.blocks = src.blocks;
    slopengine::syncBrushNocollide(brush);
    compensateUvLocks(src, brush, assets);
    return brush;
}

slopengine::Brush translateBrush(
    const slopengine::Brush& src,
    Vector3 delta,
    slopengine::AssetStore& assets) {
    if (src.box) {
        return makeBoxAt(src, add3(src.mins, delta), add3(src.maxs, delta), assets);
    }

    slopengine::Brush brush = src;
    for (slopengine::BrushFace& face : brush.faces) {
        const Vector3 oldRef = face.vertices.empty() ? Vector3{} : face.vertices[0];
        Vector3 oldU{};
        Vector3 oldV{};
        if (face.uvLock) {
            slopengine::ensureFaceUvAxes(face);
            oldU = face.uvUAxis;
            oldV = face.uvVAxis;
        }
        for (Vector3& v : face.vertices) {
            v = add3(v, delta);
        }
        face.normal = slopengine::faceNormalFromVertices(face.vertices);
        if (face.uvLock) {
            slopengine::lockFaceUvShift(
                face,
                oldRef,
                oldU,
                oldV,
                facePixelsPerMeter(assets, face.material));
        }
    }
    slopengine::recomputeBrushBounds(brush);
    return brush;
}

void appendUniqueSeed(std::vector<Vector3>& seeds, Vector3 pos) {
    for (const Vector3& existing : seeds) {
        if (vertsNear(existing, pos)) {
            return;
        }
    }
    seeds.push_back(pos);
}

void collectVertSeeds(
    const slopengine::Brush& brush,
    const std::vector<VertRef>& refs,
    int brushIndex,
    std::vector<Vector3>& seeds) {
    for (const VertRef& ref : refs) {
        if (ref.brush != brushIndex || !ref.valid()) {
            continue;
        }
        if (ref.face >= static_cast<int>(brush.faces.size())) {
            continue;
        }
        const auto& verts = brush.faces[static_cast<std::size_t>(ref.face)].vertices;
        if (ref.vert < 0 || ref.vert >= static_cast<int>(verts.size())) {
            continue;
        }
        appendUniqueSeed(seeds, verts[static_cast<std::size_t>(ref.vert)]);
    }
}

void collectEdgeSeeds(
    const slopengine::Brush& brush,
    const std::vector<EdgeRef>& refs,
    int brushIndex,
    std::vector<Vector3>& seeds) {
    for (const EdgeRef& ref : refs) {
        if (ref.brush != brushIndex || !ref.valid()) {
            continue;
        }
        if (ref.face >= static_cast<int>(brush.faces.size())) {
            continue;
        }
        const auto& verts = brush.faces[static_cast<std::size_t>(ref.face)].vertices;
        if (verts.size() < 2 || ref.edge < 0 || ref.edge >= static_cast<int>(verts.size())) {
            continue;
        }
        appendUniqueSeed(seeds, verts[static_cast<std::size_t>(ref.edge)]);
        appendUniqueSeed(
            seeds, verts[static_cast<std::size_t>((ref.edge + 1) % verts.size())]);
    }
}

void collectFaceSeeds(
    const slopengine::Brush& brush,
    const std::vector<FaceRef>& refs,
    int brushIndex,
    std::vector<Vector3>& seeds) {
    for (const FaceRef& ref : refs) {
        if (ref.brush != brushIndex || !ref.valid()) {
            continue;
        }
        if (ref.face >= static_cast<int>(brush.faces.size())) {
            continue;
        }
        const auto& verts = brush.faces[static_cast<std::size_t>(ref.face)].vertices;
        for (const Vector3& v : verts) {
            appendUniqueSeed(seeds, v);
        }
    }
}

bool seedMatches(const std::vector<Vector3>& seeds, Vector3 pos) {
    for (const Vector3& seed : seeds) {
        if (vertsNear(seed, pos)) {
            return true;
        }
    }
    return false;
}

slopengine::Brush moveWeldedVerts(
    const slopengine::Brush& src,
    const std::vector<Vector3>& seeds,
    Vector3 delta,
    slopengine::AssetStore& assets) {
    if (seeds.empty()) {
        return src;
    }
    slopengine::Brush brush = src;
    for (slopengine::BrushFace& face : brush.faces) {
        const Vector3 oldRef = face.vertices.empty() ? Vector3{} : face.vertices[0];
        Vector3 oldU{};
        Vector3 oldV{};
        if (face.uvLock) {
            slopengine::ensureFaceUvAxes(face);
            oldU = face.uvUAxis;
            oldV = face.uvVAxis;
        }
        bool moved = false;
        std::vector<char> moveFlags(face.vertices.size(), 0);
        for (std::size_t vi = 0; vi < face.vertices.size(); ++vi) {
            if (seedMatches(seeds, face.vertices[vi])) {
                moveFlags[vi] = 1;
                moved = true;
            }
        }
        if (!moved) {
            continue;
        }
        for (std::size_t vi = 0; vi < face.vertices.size(); ++vi) {
            if (moveFlags[vi]) {
                face.vertices[vi] = add3(face.vertices[vi], delta);
            }
        }
        face.normal = slopengine::faceNormalFromVertices(face.vertices);
        if (face.uvLock) {
            slopengine::lockFaceUvShift(
                face,
                oldRef,
                oldU,
                oldV,
                facePixelsPerMeter(assets, face.material));
        }
    }
    slopengine::recomputeBrushBounds(brush);
    brush.box = false;
    return brush;
}

bool normalIsAxisAligned(Vector3 n) {
    const float ax = std::fabs(n.x);
    const float ay = std::fabs(n.y);
    const float az = std::fabs(n.z);
    const float dominant = std::max(ax, std::max(ay, az));
    return dominant > 0.999f;
}

slopengine::Brush promoteToBoxIfPossible(
    const slopengine::Brush& src,
    slopengine::AssetStore& assets) {
    if (src.faces.size() != 6) {
        return src;
    }
    std::array<bool, 6> seen{};
    std::vector<std::pair<slopengine::BrushBoxSide, slopengine::BrushFace>> overrides;
    overrides.reserve(6);
    std::string material = src.faces.front().material;
    for (const slopengine::BrushFace& face : src.faces) {
        if (!normalIsAxisAligned(face.normal)) {
            return src;
        }
        const slopengine::BrushBoxSide side = slopengine::brushBoxSideFromNormal(face.normal);
        const int sideIndex = static_cast<int>(side);
        if (seen[static_cast<std::size_t>(sideIndex)]) {
            return src;
        }
        seen[static_cast<std::size_t>(sideIndex)] = true;
        slopengine::BrushFace overrideFace;
        overrideFace.material = face.material;
        overrideFace.uvShiftPixels = face.uvShiftPixels;
        overrideFace.uvScale = face.uvScale;
        overrideFace.nodraw = face.nodraw;
        overrideFace.uvLock = face.uvLock;
        overrideFace.uvUAxis = face.uvUAxis;
        overrideFace.uvVAxis = face.uvVAxis;
        overrideFace.onUse = face.onUse;
        overrideFace.onTouch = face.onTouch;
        overrides.emplace_back(side, std::move(overrideFace));
    }
    for (bool hasSide : seen) {
        if (!hasSide) {
            return src;
        }
    }
    slopengine::Brush brush = slopengine::makeBrushBox(
        src.id, src.mins, src.maxs, material, overrides, src.role);
    brush.blocks = src.blocks;
    slopengine::syncBrushNocollide(brush);
    compensateUvLocks(src, brush, assets);
    return brush;
}

Vector3 rotateAnglesForAxis(TranslateAxis axis, float angleRadians) {
    switch (axis) {
    case TranslateAxis::X:
        return {angleRadians, 0.0f, 0.0f};
    case TranslateAxis::Z:
        return {0.0f, 0.0f, angleRadians};
    case TranslateAxis::Y:
    case TranslateAxis::None:
        break;
    }
    return {0.0f, angleRadians, 0.0f};
}

float wrapAngle(float radians) {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 6.28318530717958647692f;
    while (radians > kPi) {
        radians -= kTwoPi;
    }
    while (radians < -kPi) {
        radians += kTwoPi;
    }
    return radians;
}

float snapAngleRadians(float radians, float snapDegrees) {
    if (snapDegrees <= 0.0f) {
        return radians;
    }
    const float snap = snapDegrees * (3.14159265358979323846f / 180.0f);
    return std::round(radians / snap) * snap;
}

float screenAngleAround(Vector2 center, Vector2 point) {
    return std::atan2(point.y - center.y, point.x - center.x);
}

slopengine::Brush rotateBrushAround(
    const slopengine::Brush& src,
    Vector3 pivot,
    Vector3 anglesPitchYawRoll,
    slopengine::AssetStore& assets) {
    slopengine::Brush brush = src;
    const Matrix rotation = MatrixRotateXYZ(anglesPitchYawRoll);
    for (slopengine::BrushFace& face : brush.faces) {
        const Vector3 oldRef = face.vertices.empty() ? Vector3{} : face.vertices[0];
        Vector3 oldU{};
        Vector3 oldV{};
        if (face.uvLock) {
            slopengine::ensureFaceUvAxes(face);
            oldU = face.uvUAxis;
            oldV = face.uvVAxis;
        }
        for (Vector3& vertex : face.vertices) {
            vertex = add3(Vector3Transform(sub3(vertex, pivot), rotation), pivot);
        }
        face.normal = slopengine::faceNormalFromVertices(face.vertices);
        if (face.uvLock) {
            face.uvUAxis = normalize3(Vector3Transform(oldU, rotation));
            face.uvVAxis = normalize3(Vector3Transform(oldV, rotation));
            slopengine::lockFaceUvShift(
                face,
                oldRef,
                oldU,
                oldV,
                facePixelsPerMeter(assets, face.material));
        }
    }
    slopengine::recomputeBrushBounds(brush);
    brush.box = false;
    return promoteToBoxIfPossible(brush, assets);
}

const char* rotateAxisName(TranslateAxis axis) {
    switch (axis) {
    case TranslateAxis::X:
        return "X";
    case TranslateAxis::Y:
        return "Y";
    case TranslateAxis::Z:
        return "Z";
    case TranslateAxis::None:
        break;
    }
    return "Y";
}

void updateRotateStatus(Editor& editor, const SelectTool& tool) {
    std::string message = "Rotate ";
    message += translateTargetName(editor);
    message += " | ";
    message += rotateAxisName(tool.rotateAxisLock);
    message += " | ";
    if (tool.numericLocked(editor)) {
        message += editor.numericBuffer;
        message += " deg";
    } else {
        char buf[32];
        std::snprintf(
            buf,
            sizeof(buf),
            "%.1f deg",
            static_cast<double>(tool.rotateAngle * (180.0f / 3.14159265358979323846f)));
        message += buf;
    }
    message += " | snap ";
    if (editor.rotateSnapDegrees <= 0.0f) {
        message += "off";
    } else {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f", static_cast<double>(editor.rotateSnapDegrees));
        message += buf;
        message += " deg";
    }
    message += "  (Enter confirm, Esc cancel, X/Y/Z axis)";
    editor.statusMessage = std::move(message);
}

slopengine::Brush pushFace(
    const slopengine::Brush& src,
    int faceIndex,
    float distance,
    slopengine::AssetStore& assets) {
    if (faceIndex < 0 || faceIndex >= static_cast<int>(src.faces.size())) {
        return src;
    }
    if (src.box) {
        const slopengine::BrushFace& face = src.faces[static_cast<std::size_t>(faceIndex)];
        Vector3 mins = src.mins;
        Vector3 maxs = src.maxs;
        const Vector3 n = face.normal;
        if (std::fabs(n.x) > 0.9f) {
            if (n.x > 0.0f) {
                maxs.x += distance;
            } else {
                mins.x -= distance;
            }
        } else if (std::fabs(n.y) > 0.9f) {
            if (n.y > 0.0f) {
                maxs.y += distance;
            } else {
                mins.y -= distance;
            }
        } else if (std::fabs(n.z) > 0.9f) {
            if (n.z > 0.0f) {
                maxs.z += distance;
            } else {
                mins.z -= distance;
            }
        }
        if (mins.x >= maxs.x || mins.y >= maxs.y || mins.z >= maxs.z) {
            return src;
        }
        return makeBoxAt(src, mins, maxs, assets);
    }

    const slopengine::BrushFace& face = src.faces[static_cast<std::size_t>(faceIndex)];
    if (face.vertices.empty()) {
        return src;
    }
    std::vector<Vector3> seeds;
    seeds.reserve(face.vertices.size());
    for (const Vector3& v : face.vertices) {
        appendUniqueSeed(seeds, v);
    }
    return moveWeldedVerts(src, seeds, scale3(face.normal, distance), assets);
}

bool faceFacesRay(const slopengine::BrushFace& face, Ray ray, bool ignoreBackfaces) {
    if (!ignoreBackfaces) {
        return true;
    }
    return dot3(face.normal, ray.direction) < 0.0f;
}

bool normalFacesRay(Vector3 normal, Ray ray, bool ignoreBackfaces) {
    if (!ignoreBackfaces) {
        return true;
    }
    return dot3(normal, ray.direction) < 0.0f;
}

bool useVisPick(const Editor& editor) {
    switch (editor.fill) {
    case PreviewFill::Unlit:
    case PreviewFill::Lit:
    case PreviewFill::SolidLit:
        return !editor.preview.pickFac.faces.empty();
    case PreviewFill::Wireframe:
    case PreviewFill::Solid:
    case PreviewFill::Textures:
    default:
        return false;
    }
}

std::optional<FaceRef> findFaceRefById(
    const std::vector<slopengine::Brush>& brushes,
    std::string_view sourceFaceId) {
    if (sourceFaceId.empty()) {
        return std::nullopt;
    }
    auto lookupExact = [&](std::string_view id) -> std::optional<FaceRef> {
        for (std::size_t bi = 0; bi < brushes.size(); ++bi) {
            const slopengine::Brush& brush = brushes[bi];
            for (std::size_t fi = 0; fi < brush.faces.size(); ++fi) {
                if (brush.faces[fi].id == id) {
                    return FaceRef{static_cast<int>(bi), static_cast<int>(fi)};
                }
            }
        }
        return std::nullopt;
    };
    if (auto hit = lookupExact(sourceFaceId)) {
        return hit;
    }
    std::size_t start = 0;
    while (start < sourceFaceId.size()) {
        const std::size_t plus = sourceFaceId.find('+', start);
        const std::string_view part = plus == std::string_view::npos
            ? sourceFaceId.substr(start)
            : sourceFaceId.substr(start, plus - start);
        if (!part.empty()) {
            if (auto hit = lookupExact(part)) {
                return hit;
            }
        }
        if (plus == std::string_view::npos) {
            break;
        }
        start = plus + 1;
    }
    return std::nullopt;
}

std::optional<float> rayVisibleFaceDistance(
    Ray ray,
    const slopengine::VisibleFace& face,
    bool ignoreBackfaces) {
    if (face.vertices.size() < 3 || !normalFacesRay(face.normal, ray, ignoreBackfaces)) {
        return std::nullopt;
    }
    float best = std::numeric_limits<float>::max();
    bool hit = false;
    for (std::size_t i = 1; i + 1 < face.vertices.size(); ++i) {
        float t = 0.0f;
        if (rayTriangle(ray, face.vertices[0], face.vertices[i], face.vertices[i + 1], t) &&
            t < best) {
            best = t;
            hit = true;
        }
    }
    if (!hit) {
        return std::nullopt;
    }
    return best;
}

constexpr float kPickCyclePixelSlop = 6.0f;

bool pickCycleMouseNear(Vector2 a, Vector2 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy <= kPickCyclePixelSlop * kPickCyclePixelSlop;
}

template <typename T>
bool samePickStack(const std::vector<T>& a, const std::vector<T>& b) {
    return a == b;
}

} // namespace

std::optional<float> rayBrushHitDistance(
    Ray ray,
    const slopengine::Brush& brush,
    bool ignoreBackfaces) {
    float best = std::numeric_limits<float>::max();
    bool hit = false;
    for (const slopengine::BrushFace& face : brush.faces) {
        if (face.vertices.size() < 3 || !faceFacesRay(face, ray, ignoreBackfaces)) {
            continue;
        }
        for (std::size_t i = 1; i + 1 < face.vertices.size(); ++i) {
            float t = 0.0f;
            if (rayTriangle(ray, face.vertices[0], face.vertices[i], face.vertices[i + 1], t) &&
                t < best) {
                best = t;
                hit = true;
            }
        }
    }
    if (!hit) {
        if (ignoreBackfaces) {
            return std::nullopt;
        }
        float t = 0.0f;
        if (rayAabb(ray, brush.mins, brush.maxs, t)) {
            return t;
        }
        return std::nullopt;
    }
    return best;
}

std::optional<int> rayBrushFaceIndex(
    Ray ray,
    const slopengine::Brush& brush,
    float* outDistance,
    bool ignoreBackfaces) {
    float best = std::numeric_limits<float>::max();
    int bestFace = -1;
    for (std::size_t fi = 0; fi < brush.faces.size(); ++fi) {
        const slopengine::BrushFace& face = brush.faces[fi];
        if (face.vertices.size() < 3 || !faceFacesRay(face, ray, ignoreBackfaces)) {
            continue;
        }
        for (std::size_t i = 1; i + 1 < face.vertices.size(); ++i) {
            float t = 0.0f;
            if (rayTriangle(ray, face.vertices[0], face.vertices[i], face.vertices[i + 1], t) &&
                t < best) {
                best = t;
                bestFace = static_cast<int>(fi);
            }
        }
    }
    if (bestFace < 0) {
        return std::nullopt;
    }
    if (outDistance != nullptr) {
        *outDistance = best;
    }
    return bestFace;
}

void SelectTool::beginTranslate(Editor& editor, const Camera3D& camera) {
    if (rotating) {
        cancelRotate(editor);
    }
    EditorDocument& d = editor.doc();
    brushSnapshot.clear();
    brushSnapshotIndices.clear();
    entityAtSnapshots.clear();
    entityYawSnapshots.clear();
    entityAnglesSnapshots.clear();
    entityHaveAnglesSnapshots.clear();
    entitySnapshotRefs.clear();
    snapAnchorIsVertex = false;

    if (d.selectionMode == SelectionMode::Entity) {
        if (d.selectedEntities.empty()) {
            return;
        }
        translating = true;
        axisLock = TranslateAxis::None;
        numericActive = false;
        editor.numericBuffer.clear();
        for (const EntityRef& ref : d.selectedEntities) {
            if (ref.kind == EntityRef::Kind::Thing &&
                ref.index >= 0 && ref.index < static_cast<int>(d.things.size())) {
                entitySnapshotRefs.push_back(ref);
                entityAtSnapshots.push_back(
                    d.things[static_cast<std::size_t>(ref.index)].haveAt
                        ? d.things[static_cast<std::size_t>(ref.index)].at
                        : Vector3{});
            } else if (
                ref.kind == EntityRef::Kind::Instance && ref.index >= 0 &&
                ref.index < static_cast<int>(d.instances.size())) {
                entitySnapshotRefs.push_back(ref);
                entityAtSnapshots.push_back(
                    d.instances[static_cast<std::size_t>(ref.index)].at);
            }
        }
        if (entitySnapshotRefs.empty()) {
            translating = false;
            return;
        }
        translateOrigin = editor.selectionCenter();
    } else if (d.selectionMode == SelectionMode::Face) {
        if (d.selectedFaces.empty()) {
            return;
        }
        std::unordered_set<int> brushSet;
        for (const FaceRef& ref : d.selectedFaces) {
            if (ref.brush >= 0) {
                brushSet.insert(ref.brush);
            }
        }
        translating = true;
        axisLock = TranslateAxis::None;
        numericActive = false;
        editor.numericBuffer.clear();
        for (int index : brushSet) {
            if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
                continue;
            }
            brushSnapshotIndices.push_back(index);
            brushSnapshot.push_back(d.brushes[static_cast<std::size_t>(index)]);
        }
        if (brushSnapshot.empty()) {
            translating = false;
            return;
        }
        translateOrigin = editor.selectionCenter();
    } else if (
        d.selectionMode == SelectionMode::Vert || d.selectionMode == SelectionMode::Edge) {
        const bool haveVerts =
            d.selectionMode == SelectionMode::Vert && !d.selectedVerts.empty();
        const bool haveEdges =
            d.selectionMode == SelectionMode::Edge && !d.selectedEdges.empty();
        if (!haveVerts && !haveEdges) {
            return;
        }
        std::unordered_set<int> brushSet;
        if (haveVerts) {
            for (const VertRef& ref : d.selectedVerts) {
                if (ref.brush >= 0) {
                    brushSet.insert(ref.brush);
                }
            }
        } else {
            for (const EdgeRef& ref : d.selectedEdges) {
                if (ref.brush >= 0) {
                    brushSet.insert(ref.brush);
                }
            }
        }
        translating = true;
        axisLock = TranslateAxis::None;
        numericActive = false;
        editor.numericBuffer.clear();
        for (int index : brushSet) {
            if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
                continue;
            }
            brushSnapshotIndices.push_back(index);
            brushSnapshot.push_back(d.brushes[static_cast<std::size_t>(index)]);
        }
        if (brushSnapshot.empty()) {
            translating = false;
            return;
        }
        translateOrigin = editor.selectionCenter();
    } else if (d.selectionMode == SelectionMode::Brush) {
        if (d.selectedBrushes.empty()) {
            return;
        }
        translating = true;
        axisLock = TranslateAxis::None;
        numericActive = false;
        editor.numericBuffer.clear();
        for (int index : d.selectedBrushes) {
            if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
                continue;
            }
            brushSnapshotIndices.push_back(index);
            brushSnapshot.push_back(d.brushes[static_cast<std::size_t>(index)]);
        }
        if (brushSnapshot.empty()) {
            translating = false;
            return;
        }
        if (const auto picked = nearestSelectedVertexToScreen(
                d, camera, editor.contentViewport, GetMousePosition())) {
            translateOrigin = vertRefPosition(d, *picked);
            snapAnchorIsVertex = true;
        } else {
            translateOrigin = editor.selectionCenter();
        }
    } else {
        return;
    }

    editor.prepareEdit();
    const Vector2 grabScreen = GetMousePosition();
    beginToolMouseCapture(editor, grabScreen);
    captureTranslateGrab(*this, editor, camera);
    updateTranslateStatus(editor, *this);
}

void SelectTool::applyTranslate(Editor& editor, slopengine::AssetStore& assets, Vector3 delta) {
    EditorDocument& d = editor.doc();
    const bool exact = numericLocked(editor);
    if (d.selectionMode == SelectionMode::Entity) {
        if (!exact) {
            delta = snapTranslateDelta(
                delta, translateOrigin, editor.gridSize, editor.translateSnapMode);
        }
        for (std::size_t i = 0; i < entitySnapshotRefs.size(); ++i) {
            const EntityRef& ref = entitySnapshotRefs[i];
            if (ref.kind == EntityRef::Kind::Thing &&
                ref.index >= 0 && ref.index < static_cast<int>(d.things.size())) {
                slopengine::Thing& thing = d.things[static_cast<std::size_t>(ref.index)];
                thing.at = add3(entityAtSnapshots[i], delta);
                thing.haveAt = true;
            } else if (
                ref.kind == EntityRef::Kind::Instance && ref.index >= 0 &&
                ref.index < static_cast<int>(d.instances.size())) {
                d.instances[static_cast<std::size_t>(ref.index)].at =
                    add3(entityAtSnapshots[i], delta);
            }
        }
        return;
    }

    if (brushSnapshot.empty() || brushSnapshotIndices.empty()) {
        return;
    }

    if (d.selectionMode == SelectionMode::Face && !d.selectedFaces.empty()) {
        Vector3 snappedDelta = delta;
        if (editor.transformSpace != TransformSpace::Relative && !exact) {
            snappedDelta = snapTranslateDelta(
                delta, translateOrigin, editor.gridSize, editor.translateSnapMode);
        }
        for (std::size_t i = 0; i < brushSnapshot.size(); ++i) {
            const int index = brushSnapshotIndices[i];
            if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
                continue;
            }
            if (editor.transformSpace == TransformSpace::Relative) {
                slopengine::Brush brush = brushSnapshot[i];
                for (const FaceRef& ref : d.selectedFaces) {
                    if (ref.brush != index || !ref.valid()) {
                        continue;
                    }
                    if (ref.face < 0 || ref.face >= static_cast<int>(brush.faces.size())) {
                        continue;
                    }
                    const slopengine::BrushFace& face =
                        brush.faces[static_cast<std::size_t>(ref.face)];
                    const float rawDistance = dot3(delta, face.normal);
                    float distance = rawDistance;
                    if (!exact) {
                        const Vector3 originPoint =
                            face.vertices.empty() ? translateOrigin : face.vertices[0];
                        const float originAlong = dot3(originPoint, face.normal);
                        distance = snapTranslateDistance(
                            rawDistance,
                            originAlong,
                            editor.gridSize,
                            editor.translateSnapMode);
                    }
                    brush = pushFace(brush, ref.face, distance, assets);
                }
                d.brushes[static_cast<std::size_t>(index)] = brush;
            } else {
                std::vector<Vector3> seeds;
                collectFaceSeeds(brushSnapshot[i], d.selectedFaces, index, seeds);
                d.brushes[static_cast<std::size_t>(index)] =
                    moveWeldedVerts(brushSnapshot[i], seeds, snappedDelta, assets);
            }
        }
        return;
    }

    if (!exact) {
        delta = snapTranslateDelta(
            delta, translateOrigin, editor.gridSize, editor.translateSnapMode);
    }

    if (d.selectionMode == SelectionMode::Vert || d.selectionMode == SelectionMode::Edge) {
        for (std::size_t i = 0; i < brushSnapshot.size(); ++i) {
            const int index = brushSnapshotIndices[i];
            if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
                continue;
            }
            std::vector<Vector3> seeds;
            if (d.selectionMode == SelectionMode::Vert) {
                collectVertSeeds(
                    brushSnapshot[i], d.selectedVerts, index, seeds);
            } else {
                collectEdgeSeeds(
                    brushSnapshot[i], d.selectedEdges, index, seeds);
            }
            d.brushes[static_cast<std::size_t>(index)] =
                moveWeldedVerts(brushSnapshot[i], seeds, delta, assets);
        }
        return;
    }

    for (std::size_t i = 0; i < brushSnapshot.size(); ++i) {
        const int index = brushSnapshotIndices[i];
        if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
            continue;
        }
        d.brushes[static_cast<std::size_t>(index)] =
            translateBrush(brushSnapshot[i], delta, assets);
    }
}

void SelectTool::confirmTranslate(Editor& editor, slopengine::AssetStore& assets) {
    if (!translating) {
        return;
    }
    EditorDocument& d = editor.doc();
    const bool wasEntity = d.selectionMode == SelectionMode::Entity;
    const bool wasFace = d.selectionMode == SelectionMode::Face;
    const bool wasVertEdge =
        d.selectionMode == SelectionMode::Vert || d.selectionMode == SelectionMode::Edge;
    const bool wasBrush = d.selectionMode == SelectionMode::Brush || wasFace || wasVertEdge;

    if (wasVertEdge || wasFace) {
        for (std::size_t i = 0; i < brushSnapshotIndices.size(); ++i) {
            const int index = brushSnapshotIndices[i];
            if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
                continue;
            }
            slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(index)];
            slopengine::cleanupBrushGeometry(brush, editor.gridSize);
            brush = promoteToBoxIfPossible(brush, assets);
        }
    }

    bool anyInstance = false;
    slopengine::BrushRole brushRole = slopengine::BrushRole::Hull;
    slopengine::ThingKind thingKind = slopengine::ThingKind::Prop;
    if (wasBrush && !brushSnapshotIndices.empty()) {
        const int index = brushSnapshotIndices[0];
        if (index >= 0 && index < static_cast<int>(d.brushes.size())) {
            brushRole = d.brushes[static_cast<std::size_t>(index)].role;
        }
    }
    if (wasEntity) {
        for (const EntityRef& ref : entitySnapshotRefs) {
            if (ref.kind == EntityRef::Kind::Instance) {
                anyInstance = true;
            } else if (
                ref.kind == EntityRef::Kind::Thing && ref.index >= 0 &&
                ref.index < static_cast<int>(d.things.size())) {
                thingKind = d.things[static_cast<std::size_t>(ref.index)].kind;
            }
        }
    }
    translating = false;
    axisLock = TranslateAxis::None;
    snapAnchorIsVertex = false;
    numericActive = false;
    editor.numericBuffer.clear();
    brushSnapshot.clear();
    brushSnapshotIndices.clear();
    entityAtSnapshots.clear();
    entitySnapshotRefs.clear();
    editor.markDirty();
    if (anyInstance) {
        editor.markBspDirty();
        editor.rebuildPreview(assets);
    } else if (wasBrush) {
        editor.markBrushCompileDirty(brushRole);
    } else if (wasEntity) {
        editor.markThingCompileDirty(thingKind);
    }
    editor.endEdit();
    endToolMouseCapture(editor);
    editor.statusMessage = "Translate confirmed";
}

void SelectTool::cancelTranslate(Editor& editor) {
    if (!translating) {
        return;
    }
    translating = false;
    axisLock = TranslateAxis::None;
    snapAnchorIsVertex = false;
    numericActive = false;
    editor.numericBuffer.clear();
    brushSnapshot.clear();
    brushSnapshotIndices.clear();
    entityAtSnapshots.clear();
    entitySnapshotRefs.clear();
    editor.abortEdit();
    endToolMouseCapture(editor);
    editor.statusMessage = "Translate cancelled";
}

bool SelectTool::numericLocked(const Editor& editor) const {
    return bufferHasDigit(editor.numericBuffer);
}

Vector3 SelectTool::snapAnchorWorldPos(const Editor& editor) const {
    if (rotating) {
        // The pivot never moves during a rotate; the anchor vertex stays put by definition.
        return rotateOrigin;
    }
    if (translating) {
        return add3(translateOrigin, currentTranslateDelta(editor, *this));
    }
    return translateOrigin;
}

void SelectTool::handleNumeric(
    Editor& editor,
    slopengine::AssetStore& assets,
    bool uiWantsKeyboard) {
    if (uiWantsKeyboard || (!translating && !rotating)) {
        return;
    }

    for (int key = KEY_ZERO; key <= KEY_NINE; ++key) {
        if (IsKeyPressed(key)) {
            editor.numericBuffer.push_back(static_cast<char>('0' + (key - KEY_ZERO)));
            numericActive = true;
        }
    }
    if (IsKeyPressed(KEY_PERIOD) || IsKeyPressed(KEY_KP_DECIMAL)) {
        if (editor.numericBuffer.find('.') == std::string::npos) {
            editor.numericBuffer.push_back('.');
        }
    }
    if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) {
        if (numericLocked(editor)) {
            if (!editor.numericBuffer.empty() && editor.numericBuffer.front() == '-') {
                editor.numericBuffer.erase(editor.numericBuffer.begin());
            } else {
                editor.numericBuffer.insert(editor.numericBuffer.begin(), '-');
            }
        } else if (editor.numericBuffer.empty()) {
            editor.numericBuffer.push_back('-');
        } else if (editor.numericBuffer == "-") {
            editor.numericBuffer.clear();
        } else if (editor.numericBuffer == ".") {
            editor.numericBuffer = "-.";
        } else if (editor.numericBuffer == "-.") {
            editor.numericBuffer = ".";
        }
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !editor.numericBuffer.empty()) {
        editor.numericBuffer.pop_back();
        numericActive = numericLocked(editor);
    }

    if (rotating) {
        updateRotateStatus(editor, *this);
        if (!numericLocked(editor) || editor.numericBuffer.empty() ||
            editor.numericBuffer == "-" || editor.numericBuffer == "." ||
            editor.numericBuffer == "-.") {
            return;
        }
        char* end = nullptr;
        const float degrees = std::strtof(editor.numericBuffer.c_str(), &end);
        if (end == editor.numericBuffer.c_str()) {
            return;
        }
        applyRotate(
            editor,
            assets,
            degrees * (3.14159265358979323846f / 180.0f));
        return;
    }

    updateTranslateStatus(editor, *this);

    if (!numericLocked(editor) || editor.numericBuffer.empty() || editor.numericBuffer == "-" ||
        editor.numericBuffer == "." || editor.numericBuffer == "-.") {
        return;
    }

    char* end = nullptr;
    const float value = std::strtof(editor.numericBuffer.c_str(), &end);
    if (end == editor.numericBuffer.c_str()) {
        return;
    }

    Vector3 delta{};
    const EditorDocument& d = editor.doc();
    if (d.selectionMode == SelectionMode::Face &&
        editor.transformSpace == TransformSpace::Relative && d.activeFace.valid() &&
        !brushSnapshot.empty()) {
        for (std::size_t i = 0; i < brushSnapshotIndices.size(); ++i) {
            if (brushSnapshotIndices[i] != d.activeFace.brush) {
                continue;
            }
            if (d.activeFace.face >= 0 &&
                d.activeFace.face < static_cast<int>(brushSnapshot[i].faces.size())) {
                const auto& face =
                    brushSnapshot[i].faces[static_cast<std::size_t>(d.activeFace.face)];
                delta = scale3(face.normal, value);
            }
            break;
        }
    } else if (axisLock == TranslateAxis::X) {
        delta = {value, 0.0f, 0.0f};
    } else if (axisLock == TranslateAxis::Y) {
        delta = {0.0f, value, 0.0f};
    } else if (axisLock == TranslateAxis::Z) {
        delta = {0.0f, 0.0f, value};
    } else {
        delta = {value, 0.0f, 0.0f};
    }
    applyTranslate(editor, assets, delta);
}

void SelectTool::toggleSelectedUvLock(Editor& editor) {
    EditorDocument& d = editor.doc();
    if (d.selectionMode == SelectionMode::Face) {
        if (d.selectedFaces.empty()) {
            return;
        }
        editor.prepareEdit();
        bool any = false;
        bool lastLock = false;
        std::string lastId;
        for (const FaceRef& ref : d.selectedFaces) {
            if (!ref.valid() || ref.brush >= static_cast<int>(d.brushes.size())) {
                continue;
            }
            slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(ref.brush)];
            if (ref.face >= static_cast<int>(brush.faces.size())) {
                continue;
            }
            slopengine::BrushFace& face = brush.faces[static_cast<std::size_t>(ref.face)];
            face.uvLock = !face.uvLock;
            if (face.uvLock) {
                face.uvUAxis = {};
                face.uvVAxis = {};
                slopengine::ensureFaceUvAxes(face);
            }
            any = true;
            lastLock = face.uvLock;
            lastId = face.id;
        }
        if (!any) {
            editor.abortEdit();
            return;
        }
        editor.markDirty();
        editor.markRadDirty();
        editor.endEdit();
        editor.statusMessage = lastLock ? "UV lock on " + lastId : "UV lock off " + lastId;
        return;
    }

    if (d.selectionMode != SelectionMode::Brush || d.selectedBrushes.empty()) {
        return;
    }
    editor.prepareEdit();
    bool next = true;
    bool determined = false;
    for (int index : d.selectedBrushes) {
        if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
            continue;
        }
        for (const slopengine::BrushFace& face : d.brushes[static_cast<std::size_t>(index)].faces) {
            if (!face.uvLock) {
                next = true;
                determined = true;
                break;
            }
        }
        if (determined) {
            break;
        }
    }
    if (!determined) {
        next = false;
    }
    std::string lastId;
    for (int index : d.selectedBrushes) {
        if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
            continue;
        }
        slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(index)];
        for (slopengine::BrushFace& face : brush.faces) {
            face.uvLock = next;
            if (next) {
                face.uvUAxis = {};
                face.uvVAxis = {};
                slopengine::ensureFaceUvAxes(face);
            }
        }
        lastId = brush.id;
    }
    editor.markDirty();
    editor.markRadDirty();
    editor.endEdit();
    editor.statusMessage = next ? "UV lock on " + lastId : "UV lock off " + lastId;
}

void SelectTool::pick(Editor& editor, const Camera3D& camera) {
    EditorDocument& d = editor.doc();
    const Ray ray = mouseRay(camera, editor.contentViewport);
    const bool additive = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    const bool ignoreBackfaces = editor.ignoreBackfaces;
    const Vector2 mouse = GetMousePosition();

    if (additive) {
        pickCycleBrushes.clear();
        pickCycleFaces.clear();
        pickCycleEdges.clear();
        pickCycleVerts.clear();
        pickCycleEntities.clear();
        pickCycleIndex = 0;
    }

    if (d.selectionMode == SelectionMode::Brush) {
        struct BrushHit {
            int brush = -1;
            float t = 0.0f;
        };
        std::vector<BrushHit> hits;
        if (useVisPick(editor)) {
            for (const slopengine::VisibleFace& face : editor.preview.pickFac.faces) {
                const auto faceT = rayVisibleFaceDistance(ray, face, ignoreBackfaces);
                if (!faceT) {
                    continue;
                }
                const auto ref = findFaceRefById(d.brushes, face.sourceFaceId);
                if (!ref) {
                    continue;
                }
                hits.push_back({ref->brush, *faceT});
            }
            slopengine::ThingDocument thingsDoc{};
            thingsDoc.things = d.things;
            const std::unordered_set<std::string> moverBrushIds =
                slopengine::collectClaimedBrushIds(&thingsDoc, d.brushes);
            for (std::size_t i = 0; i < d.brushes.size(); ++i) {
                if (moverBrushIds.find(d.brushes[i].id) == moverBrushIds.end()) {
                    continue;
                }
                float faceT = 0.0f;
                if (rayBrushFaceIndex(ray, d.brushes[i], &faceT, ignoreBackfaces)) {
                    hits.push_back({static_cast<int>(i), faceT});
                }
            }
            std::sort(hits.begin(), hits.end(), [](const BrushHit& a, const BrushHit& b) {
                return a.t < b.t;
            });
            std::vector<BrushHit> unique;
            unique.reserve(hits.size());
            std::unordered_set<int> seen;
            for (const BrushHit& hit : hits) {
                if (seen.insert(hit.brush).second) {
                    unique.push_back(hit);
                }
            }
            hits = std::move(unique);
        } else {
            for (std::size_t i = 0; i < d.brushes.size(); ++i) {
                float faceT = 0.0f;
                const auto face =
                    rayBrushFaceIndex(ray, d.brushes[i], &faceT, ignoreBackfaces);
                if (face) {
                    hits.push_back({static_cast<int>(i), faceT});
                    continue;
                }
                if (!ignoreBackfaces) {
                    const auto hit = rayBrushHitDistance(ray, d.brushes[i], false);
                    if (hit) {
                        hits.push_back({static_cast<int>(i), *hit});
                    }
                }
            }
            std::sort(hits.begin(), hits.end(), [](const BrushHit& a, const BrushHit& b) {
                return a.t < b.t;
            });
        }
        std::vector<int> stack;
        stack.reserve(hits.size());
        for (const BrushHit& hit : hits) {
            stack.push_back(hit.brush);
        }

        if (stack.empty()) {
            if (!additive) {
                editor.clearSelection();
                editor.statusMessage = "Selection cleared";
            }
            pickCycleBrushes.clear();
            pickCycleIndex = 0;
            return;
        }

        int index = 0;
        if (!additive && pickCycleMouseNear(mouse, pickCycleMouse) &&
            samePickStack(stack, pickCycleBrushes) && !pickCycleBrushes.empty()) {
            index = (pickCycleIndex + 1) % static_cast<int>(stack.size());
        }
        pickCycleMouse = mouse;
        pickCycleBrushes = stack;
        pickCycleFaces.clear();
        pickCycleEdges.clear();
        pickCycleVerts.clear();
        pickCycleEntities.clear();
        pickCycleIndex = index;

        const int bestBrush = stack[static_cast<std::size_t>(index)];
        editor.selectBrush(bestBrush, additive);
        std::string msg = "Selected " + d.brushes[static_cast<std::size_t>(bestBrush)].id;
        if (stack.size() > 1 && !additive) {
            msg += " (" + std::to_string(index + 1) + "/" + std::to_string(stack.size()) + ")";
        } else if (d.selectedBrushes.size() > 1) {
            msg += " (+" + std::to_string(d.selectedBrushes.size() - 1) + ")";
        }
        editor.statusMessage = std::move(msg);
        return;
    }

    if (d.selectionMode == SelectionMode::Face) {
        struct FaceHit {
            FaceRef ref{};
            float t = 0.0f;
        };
        std::vector<FaceHit> hits;
        if (useVisPick(editor)) {
            for (const slopengine::VisibleFace& face : editor.preview.pickFac.faces) {
                const auto faceT = rayVisibleFaceDistance(ray, face, ignoreBackfaces);
                if (!faceT) {
                    continue;
                }
                const auto ref = findFaceRefById(d.brushes, face.sourceFaceId);
                if (!ref) {
                    continue;
                }
                hits.push_back({*ref, *faceT});
            }
            slopengine::ThingDocument thingsDoc{};
            thingsDoc.things = d.things;
            const std::unordered_set<std::string> moverBrushIds =
                slopengine::collectClaimedBrushIds(&thingsDoc, d.brushes);
            for (std::size_t i = 0; i < d.brushes.size(); ++i) {
                if (moverBrushIds.find(d.brushes[i].id) == moverBrushIds.end()) {
                    continue;
                }
                const slopengine::Brush& brush = d.brushes[i];
                for (std::size_t fi = 0; fi < brush.faces.size(); ++fi) {
                    const slopengine::BrushFace& face = brush.faces[fi];
                    if (face.vertices.size() < 3 || !faceFacesRay(face, ray, ignoreBackfaces)) {
                        continue;
                    }
                    float bestT = std::numeric_limits<float>::max();
                    bool hit = false;
                    for (std::size_t vi = 1; vi + 1 < face.vertices.size(); ++vi) {
                        float t = 0.0f;
                        if (rayTriangle(
                                ray,
                                face.vertices[0],
                                face.vertices[vi],
                                face.vertices[vi + 1],
                                t) &&
                            t < bestT) {
                            bestT = t;
                            hit = true;
                        }
                    }
                    if (hit) {
                        hits.push_back({{static_cast<int>(i), static_cast<int>(fi)}, bestT});
                    }
                }
            }
            std::sort(hits.begin(), hits.end(), [](const FaceHit& a, const FaceHit& b) {
                return a.t < b.t;
            });
            std::vector<FaceHit> unique;
            unique.reserve(hits.size());
            for (const FaceHit& hit : hits) {
                const bool exists = std::any_of(
                    unique.begin(),
                    unique.end(),
                    [&](const FaceHit& existing) { return existing.ref == hit.ref; });
                if (!exists) {
                    unique.push_back(hit);
                }
            }
            hits = std::move(unique);
        } else {
            for (std::size_t i = 0; i < d.brushes.size(); ++i) {
                const slopengine::Brush& brush = d.brushes[i];
                for (std::size_t fi = 0; fi < brush.faces.size(); ++fi) {
                    const slopengine::BrushFace& face = brush.faces[fi];
                    if (face.vertices.size() < 3 || !faceFacesRay(face, ray, ignoreBackfaces)) {
                        continue;
                    }
                    float bestT = std::numeric_limits<float>::max();
                    bool hit = false;
                    for (std::size_t vi = 1; vi + 1 < face.vertices.size(); ++vi) {
                        float t = 0.0f;
                        if (rayTriangle(
                                ray,
                                face.vertices[0],
                                face.vertices[vi],
                                face.vertices[vi + 1],
                                t) &&
                            t < bestT) {
                            bestT = t;
                            hit = true;
                        }
                    }
                    if (hit) {
                        hits.push_back({{static_cast<int>(i), static_cast<int>(fi)}, bestT});
                    }
                }
            }
            std::sort(hits.begin(), hits.end(), [](const FaceHit& a, const FaceHit& b) {
                return a.t < b.t;
            });
        }
        std::vector<FaceRef> stack;
        stack.reserve(hits.size());
        for (const FaceHit& hit : hits) {
            stack.push_back(hit.ref);
        }

        if (stack.empty()) {
            if (!additive) {
                editor.clearSelection();
                editor.statusMessage = "Selection cleared";
            }
            pickCycleFaces.clear();
            pickCycleIndex = 0;
            return;
        }

        int index = 0;
        if (!additive && pickCycleMouseNear(mouse, pickCycleMouse) &&
            samePickStack(stack, pickCycleFaces) && !pickCycleFaces.empty()) {
            index = (pickCycleIndex + 1) % static_cast<int>(stack.size());
        }
        pickCycleMouse = mouse;
        pickCycleFaces = stack;
        pickCycleBrushes.clear();
        pickCycleEdges.clear();
        pickCycleVerts.clear();
        pickCycleEntities.clear();
        pickCycleIndex = index;

        const FaceRef best = stack[static_cast<std::size_t>(index)];
        editor.selectFace(best, additive);
        std::string msg = "Selected " +
            d.brushes[static_cast<std::size_t>(best.brush)]
                .faces[static_cast<std::size_t>(best.face)]
                .id;
        if (stack.size() > 1 && !additive) {
            msg += " (" + std::to_string(index + 1) + "/" + std::to_string(stack.size()) + ")";
        }
        editor.statusMessage = std::move(msg);
        return;
    }

    if (d.selectionMode == SelectionMode::Vert) {
        struct VertHit {
            VertRef ref{};
            float screenDist = 0.0f;
            float depth = 0.0f;
        };
        std::vector<VertHit> hits;
        for (std::size_t bi = 0; bi < d.brushes.size(); ++bi) {
            const slopengine::Brush& brush = d.brushes[bi];
            for (std::size_t fi = 0; fi < brush.faces.size(); ++fi) {
                const slopengine::BrushFace& face = brush.faces[fi];
                for (std::size_t vi = 0; vi < face.vertices.size(); ++vi) {
                    const Vector3& world = face.vertices[vi];
                    const Vector2 screen =
                        worldToViewportScreen(world, camera, editor.contentViewport);
                    const float dx = screen.x - mouse.x;
                    const float dy = screen.y - mouse.y;
                    const float screenDist = std::sqrt(dx * dx + dy * dy);
                    if (screenDist > kVertPickPixels) {
                        continue;
                    }
                    const Vector3 toVert = sub3(world, ray.position);
                    const float depth = dot3(toVert, ray.direction);
                    if (depth < 0.0f) {
                        continue;
                    }
                    hits.push_back(
                        {{static_cast<int>(bi), static_cast<int>(fi), static_cast<int>(vi)},
                         screenDist,
                         depth});
                }
            }
        }
        std::sort(hits.begin(), hits.end(), [](const VertHit& a, const VertHit& b) {
            if (a.screenDist != b.screenDist) {
                return a.screenDist < b.screenDist;
            }
            return a.depth < b.depth;
        });
        std::vector<VertRef> stack;
        stack.reserve(hits.size());
        for (const VertHit& hit : hits) {
            const bool exists = std::any_of(
                stack.begin(), stack.end(), [&](const VertRef& existing) {
                    if (existing.brush != hit.ref.brush) {
                        return false;
                    }
                    const auto& brush = d.brushes[static_cast<std::size_t>(existing.brush)];
                    const Vector3& a = brush.faces[static_cast<std::size_t>(existing.face)]
                                           .vertices[static_cast<std::size_t>(existing.vert)];
                    const Vector3& b = brush.faces[static_cast<std::size_t>(hit.ref.face)]
                                           .vertices[static_cast<std::size_t>(hit.ref.vert)];
                    return vertsNear(a, b);
                });
            if (!exists) {
                stack.push_back(hit.ref);
            }
        }

        if (stack.empty()) {
            if (!additive) {
                editor.clearSelection();
                editor.statusMessage = "Selection cleared";
            }
            pickCycleVerts.clear();
            pickCycleIndex = 0;
            return;
        }

        int index = 0;
        if (!additive && pickCycleMouseNear(mouse, pickCycleMouse) &&
            samePickStack(stack, pickCycleVerts) && !pickCycleVerts.empty()) {
            index = (pickCycleIndex + 1) % static_cast<int>(stack.size());
        }
        pickCycleMouse = mouse;
        pickCycleVerts = stack;
        pickCycleBrushes.clear();
        pickCycleFaces.clear();
        pickCycleEdges.clear();
        pickCycleEntities.clear();
        pickCycleIndex = index;

        const VertRef best = stack[static_cast<std::size_t>(index)];
        editor.selectVert(best, additive);
        std::string msg = "Selected vert " +
            d.brushes[static_cast<std::size_t>(best.brush)].id + " f" +
            std::to_string(best.face) + "v" + std::to_string(best.vert);
        if (stack.size() > 1 && !additive) {
            msg += " (" + std::to_string(index + 1) + "/" + std::to_string(stack.size()) + ")";
        }
        editor.statusMessage = std::move(msg);
        return;
    }

    if (d.selectionMode == SelectionMode::Edge) {
        auto screenDistToSegment = [](Vector2 p, Vector2 a, Vector2 b) {
            const float abx = b.x - a.x;
            const float aby = b.y - a.y;
            const float lenSq = abx * abx + aby * aby;
            float t = 0.0f;
            if (lenSq > 1e-6f) {
                t = ((p.x - a.x) * abx + (p.y - a.y) * aby) / lenSq;
                t = std::clamp(t, 0.0f, 1.0f);
            }
            const float cx = a.x + abx * t;
            const float cy = a.y + aby * t;
            const float dx = p.x - cx;
            const float dy = p.y - cy;
            return std::sqrt(dx * dx + dy * dy);
        };

        struct EdgeHit {
            EdgeRef ref{};
            float screenDist = 0.0f;
            float depth = 0.0f;
        };
        std::vector<EdgeHit> hits;
        for (std::size_t bi = 0; bi < d.brushes.size(); ++bi) {
            const slopengine::Brush& brush = d.brushes[bi];
            for (std::size_t fi = 0; fi < brush.faces.size(); ++fi) {
                const slopengine::BrushFace& face = brush.faces[fi];
                if (face.vertices.size() < 2) {
                    continue;
                }
                for (std::size_t ei = 0; ei < face.vertices.size(); ++ei) {
                    const Vector3& wa = face.vertices[ei];
                    const Vector3& wb = face.vertices[(ei + 1) % face.vertices.size()];
                    const Vector2 sa =
                        worldToViewportScreen(wa, camera, editor.contentViewport);
                    const Vector2 sb =
                        worldToViewportScreen(wb, camera, editor.contentViewport);
                    const float screenDist = screenDistToSegment(mouse, sa, sb);
                    if (screenDist > kEdgePickPixels) {
                        continue;
                    }
                    const Vector3 mid = {
                        0.5f * (wa.x + wb.x),
                        0.5f * (wa.y + wb.y),
                        0.5f * (wa.z + wb.z),
                    };
                    const float depth = dot3(sub3(mid, ray.position), ray.direction);
                    if (depth < 0.0f) {
                        continue;
                    }
                    hits.push_back(
                        {{static_cast<int>(bi), static_cast<int>(fi), static_cast<int>(ei)},
                         screenDist,
                         depth});
                }
            }
        }
        std::sort(hits.begin(), hits.end(), [](const EdgeHit& a, const EdgeHit& b) {
            if (a.screenDist != b.screenDist) {
                return a.screenDist < b.screenDist;
            }
            return a.depth < b.depth;
        });
        std::vector<EdgeRef> stack;
        stack.reserve(hits.size());
        for (const EdgeHit& hit : hits) {
            const bool exists = std::any_of(
                stack.begin(), stack.end(), [&](const EdgeRef& existing) {
                    if (existing.brush != hit.ref.brush) {
                        return false;
                    }
                    const auto& brush = d.brushes[static_cast<std::size_t>(existing.brush)];
                    const auto& faceA =
                        brush.faces[static_cast<std::size_t>(existing.face)].vertices;
                    const auto& faceB =
                        brush.faces[static_cast<std::size_t>(hit.ref.face)].vertices;
                    if (faceA.size() < 2 || faceB.size() < 2) {
                        return false;
                    }
                    const Vector3& a0 = faceA[static_cast<std::size_t>(existing.edge)];
                    const Vector3& a1 =
                        faceA[static_cast<std::size_t>((existing.edge + 1) % faceA.size())];
                    const Vector3& b0 = faceB[static_cast<std::size_t>(hit.ref.edge)];
                    const Vector3& b1 =
                        faceB[static_cast<std::size_t>((hit.ref.edge + 1) % faceB.size())];
                    return (vertsNear(a0, b0) && vertsNear(a1, b1)) ||
                        (vertsNear(a0, b1) && vertsNear(a1, b0));
                });
            if (!exists) {
                stack.push_back(hit.ref);
            }
        }

        if (stack.empty()) {
            if (!additive) {
                editor.clearSelection();
                editor.statusMessage = "Selection cleared";
            }
            pickCycleEdges.clear();
            pickCycleIndex = 0;
            return;
        }

        int index = 0;
        if (!additive && pickCycleMouseNear(mouse, pickCycleMouse) &&
            samePickStack(stack, pickCycleEdges) && !pickCycleEdges.empty()) {
            index = (pickCycleIndex + 1) % static_cast<int>(stack.size());
        }
        pickCycleMouse = mouse;
        pickCycleEdges = stack;
        pickCycleBrushes.clear();
        pickCycleFaces.clear();
        pickCycleVerts.clear();
        pickCycleEntities.clear();
        pickCycleIndex = index;

        const EdgeRef best = stack[static_cast<std::size_t>(index)];
        editor.selectEdge(best, additive);
        std::string msg = "Selected edge " +
            d.brushes[static_cast<std::size_t>(best.brush)].id + " f" +
            std::to_string(best.face) + "e" + std::to_string(best.edge);
        if (stack.size() > 1 && !additive) {
            msg += " (" + std::to_string(index + 1) + "/" + std::to_string(stack.size()) + ")";
        }
        editor.statusMessage = std::move(msg);
        return;
    }

    if (d.selectionMode != SelectionMode::Entity) {
        return;
    }

    struct EntityHit {
        EntityRef ref{};
        float t = 0.0f;
    };
    std::vector<EntityHit> hits;
    for (std::size_t i = 0; i < editor.expandedInstanceBrushes.size(); ++i) {
        const auto hit =
            rayBrushHitDistance(ray, editor.expandedInstanceBrushes[i], ignoreBackfaces);
        if (!hit) {
            continue;
        }
        const EntityRef ref{EntityRef::Kind::Instance, editor.expandedInstanceOwners[i]};
        auto existing = std::find_if(hits.begin(), hits.end(), [&](const EntityHit& h) {
            return h.ref == ref;
        });
        if (existing == hits.end()) {
            hits.push_back({ref, *hit});
        } else if (*hit < existing->t) {
            existing->t = *hit;
        }
    }
    for (std::size_t i = 0; i < d.things.size(); ++i) {
        const slopengine::Thing& thing = d.things[i];
        const Vector3 pos = thing.haveAt ? thing.at : Vector3{0.0f, 1.0f, 0.0f};
        const BoundingBox box = {
            {pos.x - 0.35f, pos.y - 0.1f, pos.z - 0.35f},
            {pos.x + 0.35f, pos.y + 1.0f, pos.z + 0.35f},
        };
        const RayCollision hit = GetRayCollisionBox(ray, box);
        if (hit.hit) {
            hits.push_back({{EntityRef::Kind::Thing, static_cast<int>(i)}, hit.distance});
        }
    }

    std::sort(hits.begin(), hits.end(), [](const EntityHit& a, const EntityHit& b) {
        return a.t < b.t;
    });
    std::vector<EntityRef> stack;
    stack.reserve(hits.size());
    for (const EntityHit& hit : hits) {
        stack.push_back(hit.ref);
    }

    if (stack.empty()) {
        if (!additive) {
            editor.clearSelection();
            editor.statusMessage = "Selection cleared";
        }
        pickCycleEntities.clear();
        pickCycleIndex = 0;
        return;
    }

    int index = 0;
    if (!additive && pickCycleMouseNear(mouse, pickCycleMouse) &&
        samePickStack(stack, pickCycleEntities) && !pickCycleEntities.empty()) {
        index = (pickCycleIndex + 1) % static_cast<int>(stack.size());
    }
    pickCycleMouse = mouse;
    pickCycleEntities = stack;
    pickCycleBrushes.clear();
    pickCycleFaces.clear();
    pickCycleEdges.clear();
    pickCycleVerts.clear();
    pickCycleIndex = index;

    const EntityRef best = stack[static_cast<std::size_t>(index)];
    editor.selectEntity(best, additive);
    std::string msg;
    if (best.kind == EntityRef::Kind::Thing) {
        msg = "Selected " + d.things[static_cast<std::size_t>(best.index)].id;
    } else {
        msg = "Selected instance " + d.instances[static_cast<std::size_t>(best.index)].id;
    }
    if (stack.size() > 1 && !additive) {
        msg += " (" + std::to_string(index + 1) + "/" + std::to_string(stack.size()) + ")";
    }
    editor.statusMessage = std::move(msg);
}

void SelectTool::deleteSelected(Editor& editor, slopengine::AssetStore& assets) {
    EditorDocument& d = editor.doc();
    if (d.selectionMode == SelectionMode::Entity) {
        if (d.selectedEntities.empty()) {
            return;
        }
        editor.prepareEdit();
        std::vector<int> thingIndices;
        std::vector<int> instanceIndices;
        for (const EntityRef& ref : d.selectedEntities) {
            if (ref.kind == EntityRef::Kind::Thing) {
                thingIndices.push_back(ref.index);
            } else {
                instanceIndices.push_back(ref.index);
            }
        }
        std::sort(thingIndices.begin(), thingIndices.end(), std::greater<int>());
        std::sort(instanceIndices.begin(), instanceIndices.end(), std::greater<int>());
        slopengine::ThingKind kind = slopengine::ThingKind::Prop;
        bool anyThing = false;
        bool anyInstance = false;
        for (int index : thingIndices) {
            if (index < 0 || index >= static_cast<int>(d.things.size())) {
                continue;
            }
            kind = d.things[static_cast<std::size_t>(index)].kind;
            d.things.erase(d.things.begin() + index);
            anyThing = true;
        }
        for (int index : instanceIndices) {
            if (index < 0 || index >= static_cast<int>(d.instances.size())) {
                continue;
            }
            d.instances.erase(d.instances.begin() + index);
            anyInstance = true;
        }
        editor.clearSelection();
        editor.markDirty();
        if (anyInstance) {
            editor.markBspDirty();
            editor.rebuildPreview(assets);
        }
        if (anyThing) {
            editor.markThingCompileDirty(kind);
        }
        editor.endEdit();
        editor.statusMessage = "Deleted selection";
        return;
    }

    std::vector<int> brushIndices;
    if (d.selectionMode == SelectionMode::Face) {
        for (const FaceRef& ref : d.selectedFaces) {
            if (ref.brush >= 0) {
                brushIndices.push_back(ref.brush);
            }
        }
        std::sort(brushIndices.begin(), brushIndices.end());
        brushIndices.erase(std::unique(brushIndices.begin(), brushIndices.end()), brushIndices.end());
    } else if (d.selectionMode == SelectionMode::Edge) {
        for (const EdgeRef& ref : d.selectedEdges) {
            if (ref.brush >= 0) {
                brushIndices.push_back(ref.brush);
            }
        }
        std::sort(brushIndices.begin(), brushIndices.end());
        brushIndices.erase(std::unique(brushIndices.begin(), brushIndices.end()), brushIndices.end());
    } else if (d.selectionMode == SelectionMode::Vert) {
        for (const VertRef& ref : d.selectedVerts) {
            if (ref.brush >= 0) {
                brushIndices.push_back(ref.brush);
            }
        }
        std::sort(brushIndices.begin(), brushIndices.end());
        brushIndices.erase(std::unique(brushIndices.begin(), brushIndices.end()), brushIndices.end());
    } else {
        brushIndices = d.selectedBrushes;
    }
    if (brushIndices.empty()) {
        return;
    }
    editor.prepareEdit();
    std::sort(brushIndices.begin(), brushIndices.end(), std::greater<int>());
    slopengine::BrushRole role = slopengine::BrushRole::Hull;
    for (int index : brushIndices) {
        if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
            continue;
        }
        role = d.brushes[static_cast<std::size_t>(index)].role;
        d.brushes.erase(d.brushes.begin() + index);
    }
    editor.clearSelection();
    editor.markDirty();
    editor.markBrushCompileDirty(role);
    editor.endEdit();
    editor.statusMessage = "Deleted selection";
}

void SelectTool::duplicateSelected(Editor& editor, const Camera3D& camera) {
    EditorDocument& d = editor.doc();
    if (d.selectionMode == SelectionMode::Entity) {
        if (d.selectedEntities.empty()) {
            return;
        }
        editor.prepareEdit();
        std::vector<EntityRef> created;
        bool anyInstance = false;
        slopengine::ThingKind kind = slopengine::ThingKind::Prop;
        for (const EntityRef& ref : d.selectedEntities) {
            if (ref.kind == EntityRef::Kind::Thing &&
                ref.index >= 0 && ref.index < static_cast<int>(d.things.size())) {
                slopengine::Thing copy = d.things[static_cast<std::size_t>(ref.index)];
                copy.id = editor.allocateThingId(slopengine::thingKindName(copy.kind));
                kind = copy.kind;
                d.things.push_back(std::move(copy));
                created.push_back(
                    {EntityRef::Kind::Thing, static_cast<int>(d.things.size()) - 1});
            } else if (
                ref.kind == EntityRef::Kind::Instance && ref.index >= 0 &&
                ref.index < static_cast<int>(d.instances.size())) {
                slopengine::PrefabInstance copy =
                    d.instances[static_cast<std::size_t>(ref.index)];
                copy.id = editor.allocatePrefabId();
                d.instances.push_back(std::move(copy));
                created.push_back(
                    {EntityRef::Kind::Instance, static_cast<int>(d.instances.size()) - 1});
                anyInstance = true;
            }
        }
        if (created.empty()) {
            editor.abortEdit();
            return;
        }
        editor.clearSelection();
        d.selectionMode = SelectionMode::Entity;
        d.selectedEntities = created;
        d.activeEntity = created.back();
        editor.markDirty();
        if (anyInstance) {
            editor.markBspDirty();
        }
        editor.markThingCompileDirty(kind);
        beginTranslate(editor, camera);
        return;
    }

    if (d.selectionMode != SelectionMode::Brush || d.selectedBrushes.empty()) {
        return;
    }
    editor.prepareEdit();
    std::vector<int> created;
    slopengine::BrushRole role = slopengine::BrushRole::Hull;
    for (int index : d.selectedBrushes) {
        if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
            continue;
        }
        slopengine::Brush copy = d.brushes[static_cast<std::size_t>(index)];
        const std::string oldId = copy.id;
        copy.id = editor.allocateBrushId();
        const std::string oldPrefix = oldId + "/";
        const std::string newPrefix = copy.id + "/";
        for (slopengine::BrushFace& face : copy.faces) {
            if (face.id.rfind(oldPrefix, 0) == 0) {
                face.id = newPrefix + face.id.substr(oldPrefix.size());
            } else if (face.id == oldId) {
                face.id = copy.id;
            }
        }
        role = copy.role;
        d.brushes.push_back(std::move(copy));
        created.push_back(static_cast<int>(d.brushes.size()) - 1);
    }
    if (created.empty()) {
        editor.abortEdit();
        return;
    }
    editor.selectBrushes(created, created.back());
    editor.markDirty();
    editor.markBrushCompileDirty(role);
    beginTranslate(editor, camera);
}

void SelectTool::beginRotate(Editor& editor, const Camera3D& camera) {
    if (translating) {
        cancelTranslate(editor);
    }
    EditorDocument& d = editor.doc();
    brushSnapshot.clear();
    brushSnapshotIndices.clear();
    entityAtSnapshots.clear();
    entityYawSnapshots.clear();
    entityAnglesSnapshots.clear();
    entityHaveAnglesSnapshots.clear();
    entitySnapshotRefs.clear();
    rotateAngle = 0.0f;
    rotateAxisLock = TranslateAxis::Y;
    snapAnchorIsVertex = false;

    if (d.selectionMode == SelectionMode::Entity) {
        if (d.selectedEntities.empty()) {
            return;
        }
        for (const EntityRef& ref : d.selectedEntities) {
            if (ref.kind == EntityRef::Kind::Thing &&
                ref.index >= 0 && ref.index < static_cast<int>(d.things.size())) {
                const slopengine::Thing& thing = d.things[static_cast<std::size_t>(ref.index)];
                entitySnapshotRefs.push_back(ref);
                entityAtSnapshots.push_back(thing.haveAt ? thing.at : Vector3{});
                entityYawSnapshots.push_back(thing.yaw);
                entityAnglesSnapshots.push_back(thing.angles);
                entityHaveAnglesSnapshots.push_back(thing.haveAngles);
            } else if (
                ref.kind == EntityRef::Kind::Instance && ref.index >= 0 &&
                ref.index < static_cast<int>(d.instances.size())) {
                const slopengine::PrefabInstance& instance =
                    d.instances[static_cast<std::size_t>(ref.index)];
                entitySnapshotRefs.push_back(ref);
                entityAtSnapshots.push_back(instance.at);
                entityYawSnapshots.push_back(instance.angles.y);
                entityAnglesSnapshots.push_back(instance.angles);
                entityHaveAnglesSnapshots.push_back(true);
            }
        }
        if (entitySnapshotRefs.empty()) {
            return;
        }
    } else if (d.selectionMode == SelectionMode::Brush) {
        if (d.selectedBrushes.empty()) {
            return;
        }
        for (int index : d.selectedBrushes) {
            if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
                continue;
            }
            brushSnapshotIndices.push_back(index);
            brushSnapshot.push_back(d.brushes[static_cast<std::size_t>(index)]);
        }
        if (brushSnapshot.empty()) {
            return;
        }
    } else {
        return;
    }

    rotating = true;
    numericActive = false;
    editor.numericBuffer.clear();
    editor.prepareEdit();
    if (d.selectionMode == SelectionMode::Brush) {
        if (const auto picked = nearestSelectedVertexToScreen(
                d, camera, editor.contentViewport, GetMousePosition())) {
            rotateOrigin = vertRefPosition(d, *picked);
            snapAnchorIsVertex = true;
        } else {
            rotateOrigin = editor.selectionCenter();
        }
    } else {
        rotateOrigin = editor.selectionCenter();
    }
    const Vector2 grabScreen = GetMousePosition();
    beginToolMouseCapture(editor, grabScreen);
    mouseGrabScreen = toolMouseScreen(editor);
    const Vector2 originScreen =
        worldToViewportScreen(rotateOrigin, camera, editor.contentViewport);
    rotateGrabAngle = screenAngleAround(originScreen, mouseGrabScreen);
    updateRotateStatus(editor, *this);
}

void SelectTool::applyRotate(Editor& editor, slopengine::AssetStore& assets, float angleRadians) {
    EditorDocument& d = editor.doc();
    rotateAngle = angleRadians;
    const Vector3 angles = rotateAnglesForAxis(rotateAxisLock, angleRadians);
    const Matrix rotation = MatrixRotateXYZ(angles);

    if (d.selectionMode == SelectionMode::Entity) {
        for (std::size_t i = 0; i < entitySnapshotRefs.size(); ++i) {
            const EntityRef& ref = entitySnapshotRefs[i];
            const Vector3 local = sub3(entityAtSnapshots[i], rotateOrigin);
            const Vector3 at = add3(Vector3Transform(local, rotation), rotateOrigin);
            if (ref.kind == EntityRef::Kind::Thing &&
                ref.index >= 0 && ref.index < static_cast<int>(d.things.size())) {
                slopengine::Thing& thing = d.things[static_cast<std::size_t>(ref.index)];
                thing.at = at;
                thing.haveAt = true;
                if (entityHaveAnglesSnapshots[i]) {
                    thing.haveAngles = true;
                    thing.angles = entityAnglesSnapshots[i];
                    if (rotateAxisLock == TranslateAxis::X) {
                        thing.angles.x = entityAnglesSnapshots[i].x + angleRadians;
                    } else if (rotateAxisLock == TranslateAxis::Z) {
                        thing.angles.z = entityAnglesSnapshots[i].z + angleRadians;
                    } else {
                        thing.angles.y = entityAnglesSnapshots[i].y + angleRadians;
                    }
                    thing.yaw = thing.angles.y;
                } else if (rotateAxisLock == TranslateAxis::Y ||
                           rotateAxisLock == TranslateAxis::None) {
                    thing.yaw = entityYawSnapshots[i] + angleRadians;
                }
            } else if (
                ref.kind == EntityRef::Kind::Instance && ref.index >= 0 &&
                ref.index < static_cast<int>(d.instances.size())) {
                slopengine::PrefabInstance& instance =
                    d.instances[static_cast<std::size_t>(ref.index)];
                instance.at = at;
                instance.angles = entityAnglesSnapshots[i];
                if (rotateAxisLock == TranslateAxis::X) {
                    instance.angles.x = entityAnglesSnapshots[i].x + angleRadians;
                } else if (rotateAxisLock == TranslateAxis::Z) {
                    instance.angles.z = entityAnglesSnapshots[i].z + angleRadians;
                } else {
                    instance.angles.y = entityAnglesSnapshots[i].y + angleRadians;
                }
            }
        }
        return;
    }

    for (std::size_t i = 0; i < brushSnapshot.size(); ++i) {
        const int index = brushSnapshotIndices[i];
        if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
            continue;
        }
        d.brushes[static_cast<std::size_t>(index)] =
            rotateBrushAround(brushSnapshot[i], rotateOrigin, angles, assets);
    }
}

void SelectTool::confirmRotate(Editor& editor, slopengine::AssetStore& assets) {
    if (!rotating) {
        return;
    }
    EditorDocument& d = editor.doc();
    const bool wasEntity = d.selectionMode == SelectionMode::Entity;
    const bool wasBrush = d.selectionMode == SelectionMode::Brush;
    bool anyInstance = false;
    slopengine::BrushRole brushRole = slopengine::BrushRole::Hull;
    slopengine::ThingKind thingKind = slopengine::ThingKind::Prop;
    if (wasBrush && !brushSnapshotIndices.empty()) {
        const int index = brushSnapshotIndices[0];
        if (index >= 0 && index < static_cast<int>(d.brushes.size())) {
            brushRole = d.brushes[static_cast<std::size_t>(index)].role;
        }
    }
    if (wasEntity) {
        for (const EntityRef& ref : entitySnapshotRefs) {
            if (ref.kind == EntityRef::Kind::Instance) {
                anyInstance = true;
            } else if (
                ref.kind == EntityRef::Kind::Thing && ref.index >= 0 &&
                ref.index < static_cast<int>(d.things.size())) {
                thingKind = d.things[static_cast<std::size_t>(ref.index)].kind;
            }
        }
    }
    rotating = false;
    rotateAxisLock = TranslateAxis::Y;
    rotateAngle = 0.0f;
    snapAnchorIsVertex = false;
    numericActive = false;
    editor.numericBuffer.clear();
    brushSnapshot.clear();
    brushSnapshotIndices.clear();
    entityAtSnapshots.clear();
    entityYawSnapshots.clear();
    entityAnglesSnapshots.clear();
    entityHaveAnglesSnapshots.clear();
    entitySnapshotRefs.clear();
    editor.markDirty();
    if (anyInstance) {
        editor.markBspDirty();
    } else if (wasBrush) {
        editor.markBrushCompileDirty(brushRole);
    } else if (wasEntity) {
        editor.markThingCompileDirty(thingKind);
    }
    editor.rebuildPreview(assets);
    editor.endEdit();
    endToolMouseCapture(editor);
    editor.statusMessage = "Rotate confirmed";
}

void SelectTool::cancelRotate(Editor& editor) {
    if (!rotating) {
        return;
    }
    rotating = false;
    rotateAxisLock = TranslateAxis::Y;
    rotateAngle = 0.0f;
    snapAnchorIsVertex = false;
    numericActive = false;
    editor.numericBuffer.clear();
    brushSnapshot.clear();
    brushSnapshotIndices.clear();
    entityAtSnapshots.clear();
    entityYawSnapshots.clear();
    entityAnglesSnapshots.clear();
    entityHaveAnglesSnapshots.clear();
    entitySnapshotRefs.clear();
    editor.abortEdit();
    endToolMouseCapture(editor);
    editor.statusMessage = "Rotate cancelled";
}

void SelectTool::update(
    Editor& editor,
    slopengine::AssetStore& assets,
    const Camera3D& camera,
    bool uiWantsMouse,
    bool uiWantsKeyboard) {
    if (editor.mode != EditorMode::Select) {
        if (translating) {
            cancelTranslate(editor);
        }
        if (rotating) {
            cancelRotate(editor);
        }
        return;
    }

    if (translating) {
        handleNumeric(editor, assets, uiWantsKeyboard);

        if (!uiWantsKeyboard) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                cancelTranslate(editor);
                return;
            }
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
                confirmTranslate(editor, assets);
                return;
            }
            if (IsKeyPressed(KEY_O) && !IsKeyDown(KEY_LEFT_CONTROL) &&
                !IsKeyDown(KEY_RIGHT_CONTROL)) {
                editor.translateSnapMode =
                    editor.translateSnapMode == TranslateSnapMode::Offset
                    ? TranslateSnapMode::Absolute
                    : TranslateSnapMode::Offset;
                updateTranslateStatus(editor, *this);
            }
            const TranslateAxis previousLock = axisLock;
            if (IsKeyPressed(KEY_X)) {
                axisLock = axisLock == TranslateAxis::X ? TranslateAxis::None : TranslateAxis::X;
                numericActive = false;
                editor.numericBuffer.clear();
            }
            if (IsKeyPressed(KEY_Y)) {
                axisLock = axisLock == TranslateAxis::Y ? TranslateAxis::None : TranslateAxis::Y;
                numericActive = false;
                editor.numericBuffer.clear();
            }
            if (IsKeyPressed(KEY_Z)) {
                axisLock = axisLock == TranslateAxis::Z ? TranslateAxis::None : TranslateAxis::Z;
                numericActive = false;
                editor.numericBuffer.clear();
            }
            if (axisLock != previousLock) {
                const Vector3 applied = currentTranslateDelta(editor, *this);
                refreshTranslateGrab(*this, editor, camera);
                if (const auto axis = constrainedTranslateAxis(editor, *this)) {
                    const Vector3 oriented = orientAxisForScreen(
                        *axis, translateOrigin, camera, editor.contentViewport);
                    applyTranslate(editor, assets, scale3(oriented, dot3(applied, oriented)));
                }
                updateTranslateStatus(editor, *this);
            }
        }

        if (!uiWantsMouse && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            confirmTranslate(editor, assets);
            return;
        }

        const bool canDrag =
            !numericLocked(editor) &&
            (!entitySnapshotRefs.empty() || !brushSnapshot.empty());
        if (canDrag) {
            Vector3 delta{};
            bool haveDelta = false;
            const ConstructionPlane translatePlane =
                constructionPlaneForView(editor.viewPlane, editor.gridPlane);
            const ConstructionPlane* fallbackPlane =
                editor.viewPlane != ViewPlane::PerspectiveY0 ? &translatePlane : nullptr;
            if (const auto axis = constrainedTranslateAxis(editor, *this)) {
                const Vector3 oriented = orientAxisForScreen(
                    *axis, translateOrigin, camera, editor.contentViewport);
                delta = computeAxisTranslateDelta(
                    oriented,
                    translateOrigin,
                    mouseGrabScreen,
                    editor,
                    camera,
                    editor.contentViewport,
                    fallbackPlane);
                haveDelta = true;
            } else {
                delta = add3(
                    computeFreeTranslateDelta(
                        translateOrigin, mouseGrabScreen, editor, camera),
                    mouseGrabWorld);
                haveDelta = true;
            }
            if (haveDelta) {
                if (editor.numericBuffer == "-") {
                    delta = scale3(delta, -1.0f);
                }
                applyTranslate(editor, assets, delta);
                updateTranslateStatus(editor, *this);
            }
        }
        return;
    }

    if (rotating) {
        handleNumeric(editor, assets, uiWantsKeyboard);

        if (!uiWantsKeyboard) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                cancelRotate(editor);
                return;
            }
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
                confirmRotate(editor, assets);
                return;
            }
            const TranslateAxis previousLock = rotateAxisLock;
            if (IsKeyPressed(KEY_X)) {
                rotateAxisLock = TranslateAxis::X;
                numericActive = false;
                editor.numericBuffer.clear();
            }
            if (IsKeyPressed(KEY_Y)) {
                rotateAxisLock = TranslateAxis::Y;
                numericActive = false;
                editor.numericBuffer.clear();
            }
            if (IsKeyPressed(KEY_Z)) {
                rotateAxisLock = TranslateAxis::Z;
                numericActive = false;
                editor.numericBuffer.clear();
            }
            if (rotateAxisLock != previousLock) {
                mouseGrabScreen = toolMouseScreen(editor);
                const Vector2 originScreen =
                    worldToViewportScreen(rotateOrigin, camera, editor.contentViewport);
                rotateGrabAngle = screenAngleAround(originScreen, mouseGrabScreen);
                applyRotate(editor, assets, 0.0f);
                updateRotateStatus(editor, *this);
            }
        }

        if (!uiWantsMouse && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            confirmRotate(editor, assets);
            return;
        }

        if (!numericLocked(editor) &&
            (!entitySnapshotRefs.empty() || !brushSnapshot.empty())) {
            const Vector2 originScreen =
                worldToViewportScreen(rotateOrigin, camera, editor.contentViewport);
            const float current = screenAngleAround(originScreen, toolMouseScreen(editor));
            float angle = wrapAngle(rotateGrabAngle - current);
            if (editor.numericBuffer == "-") {
                angle = -angle;
            }
            angle = snapAngleRadians(angle, editor.rotateSnapDegrees);
            applyRotate(editor, assets, angle);
            updateRotateStatus(editor, *this);
        }
        return;
    }

    if (uiWantsKeyboard) {
        return;
    }

    if (IsKeyPressed(KEY_G)) {
        beginTranslate(editor, camera);
        return;
    }
    if (IsKeyPressed(KEY_R)) {
        beginRotate(editor, camera);
        return;
    }
    if (IsKeyPressed(KEY_DELETE)) {
        deleteSelected(editor, assets);
        return;
    }
    if (IsKeyDown(KEY_LEFT_SHIFT) && IsKeyPressed(KEY_D)) {
        duplicateSelected(editor, camera);
        return;
    }

    if (!uiWantsMouse && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        pick(editor, camera);
    }
}

}
