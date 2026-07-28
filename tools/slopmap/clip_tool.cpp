#include "clip_tool.hpp"

#include "map/brush.hpp"
#include "preview.hpp"
#include "select_tool.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <string>

namespace slopmap {

namespace {

float projectAxis(Vector3 point, Vector3 axis) {
    return point.x * axis.x + point.y * axis.y + point.z * axis.z;
}

Vector3 combineAxes(const ConstructionPlane& plane, float u, float v, float n) {
    return {
        plane.origin.x + plane.axisU.x * u + plane.axisV.x * v + plane.normal.x * n,
        plane.origin.y + plane.axisU.y * u + plane.axisV.y * v + plane.normal.y * n,
        plane.origin.z + plane.axisU.z * u + plane.axisV.z * v + plane.normal.z * n,
    };
}

Vector3 snapOnPlane(Vector3 point, const ConstructionPlane& plane, float grid) {
    const Vector3 rel = sub3(point, plane.origin);
    const float u = snapToGrid(projectAxis(rel, plane.axisU), grid);
    const float v = snapToGrid(projectAxis(rel, plane.axisV), grid);
    return combineAxes(plane, u, v, 0.0f);
}

void drawBrushSolidTint(const slopengine::Brush& brush, Color color) {
    for (const slopengine::BrushFace& face : brush.faces) {
        const auto tris = slopengine::triangulateFace(face.vertices);
        for (const auto& tri : tris) {
            DrawTriangle3D(tri[0], tri[1], tri[2], color);
            DrawTriangle3D(tri[0], tri[2], tri[1], color);
        }
    }
}

} // namespace

void ClipTool::reset() {
    phase = ClipPhase::Idle;
    keepMode = ClipKeepMode::Front;
    construction = {};
    point0 = {};
    point1 = {};
    planeNormal = {};
    planeFlipped = false;
    hoverValid = false;
    hoverPoint = {};
    brushIndices.clear();
}

const char* ClipTool::keepModeLabel() const {
    switch (keepMode) {
    case ClipKeepMode::Front:
        return "Front";
    case ClipKeepMode::Back:
        return "Back";
    case ClipKeepMode::Both:
        return "Both";
    }
    return "Front";
}

void ClipTool::setStatus(Editor& editor) const {
    switch (phase) {
    case ClipPhase::PickingP0:
        editor.statusMessage = "Clip: click first point on a face";
        break;
    case ClipPhase::PickingP1:
        editor.statusMessage = "Clip: click second point on the face";
        break;
    case ClipPhase::Preview:
        editor.statusMessage = std::string("Clip: keep ") + keepModeLabel() +
            " (F cycle, Shift+F flip, Enter commit, Esc cancel)";
        break;
    case ClipPhase::Idle:
        break;
    }
}

void ClipTool::refreshPlane() {
    const Vector3 edge = sub3(point1, point0);
    Vector3 n = cross3(construction.normal, edge);
    n = normalize3(n);
    if (length3(n) < 1e-6f) {
        planeNormal = {};
        return;
    }
    if (planeFlipped) {
        n = scale3(n, -1.0f);
    }
    planeNormal = n;
}

bool ClipTool::hitSelectedFace(
    Editor& editor,
    const Camera3D& camera,
    Vector3& outHit,
    ConstructionPlane& outPlane) const {
    const Ray ray = mouseRay(camera, editor.contentViewport);
    const EditorDocument& d = editor.doc();
    float bestT = std::numeric_limits<float>::max();
    int bestBrush = -1;
    int bestFace = -1;

    for (int index : brushIndices) {
        if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
            continue;
        }
        float faceT = 0.0f;
        const auto face =
            rayBrushFaceIndex(ray, d.brushes[static_cast<std::size_t>(index)], &faceT, editor.ignoreBackfaces);
        if (face && faceT < bestT) {
            bestT = faceT;
            bestBrush = index;
            bestFace = *face;
        }
    }

    if (bestBrush < 0 || bestFace < 0) {
        return false;
    }

    const slopengine::BrushFace& face =
        d.brushes[static_cast<std::size_t>(bestBrush)].faces[static_cast<std::size_t>(bestFace)];
    const Vector3 hit{
        ray.position.x + ray.direction.x * bestT,
        ray.position.y + ray.direction.y * bestT,
        ray.position.z + ray.direction.z * bestT,
    };
    outPlane = constructionPlaneFromFace(face, hit);
    outHit = snapOnPlane(hit, outPlane, editor.gridSize);
    outPlane.origin = outHit;
    return true;
}

bool ClipTool::hitLockedFacePlane(Editor& editor, const Camera3D& camera, Vector3& outHit) const {
    Vector3 hit{};
    if (!rayPlaneIntersection(
            mouseRay(camera, editor.contentViewport),
            construction.origin,
            construction.normal,
            hit)) {
        return false;
    }
    outHit = snapOnPlane(hit, construction, editor.gridSize);
    return true;
}

void ClipTool::beginFromSelection(Editor& editor) {
    reset();
    EditorDocument& d = editor.doc();
    if (d.selectionMode != SelectionMode::Brush || d.selectedBrushes.empty()) {
        editor.statusMessage = "Clip: select brush(es) first (Brush selection mode)";
        return;
    }
    brushIndices = d.selectedBrushes;
    std::sort(brushIndices.begin(), brushIndices.end());
    brushIndices.erase(std::unique(brushIndices.begin(), brushIndices.end()), brushIndices.end());
    for (int index : brushIndices) {
        if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
            editor.statusMessage = "Clip: invalid brush selection";
            reset();
            return;
        }
    }
    phase = ClipPhase::PickingP0;
    editor.mode = EditorMode::Select;
    editor.setSelectionMode(SelectionMode::Brush);
    setStatus(editor);
}

void ClipTool::commit(Editor& editor) {
    EditorDocument& d = editor.doc();
    if (phase != ClipPhase::Preview || length3(planeNormal) < 1e-6f) {
        reset();
        return;
    }

    std::vector<int> indices = brushIndices;
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    std::sort(indices.begin(), indices.end(), std::greater<int>());

    editor.prepareEdit();
    std::vector<int> created;
    slopengine::BrushRole dirtyRole = slopengine::BrushRole::Detail;
    int keptCount = 0;
    auto allocateId = [&]() { return editor.allocateBrushId(); };

    for (int index : indices) {
        if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
            continue;
        }
        const slopengine::Brush source = d.brushes[static_cast<std::size_t>(index)];
        dirtyRole = source.role;
        std::string error;
        auto split =
            slopengine::splitBrushByPlane(source, point0, planeNormal, allocateId, error);
        if (!split) {
            editor.abortEdit();
            editor.statusMessage = std::string("Clip failed: ") + error;
            reset();
            return;
        }

        d.brushes.erase(d.brushes.begin() + index);

        auto pushKept = [&](slopengine::Brush brush) {
            d.brushes.push_back(std::move(brush));
            created.push_back(static_cast<int>(d.brushes.size()) - 1);
            ++keptCount;
        };

        switch (keepMode) {
        case ClipKeepMode::Front:
            pushKept(std::move(split->front));
            break;
        case ClipKeepMode::Back:
            pushKept(std::move(split->back));
            break;
        case ClipKeepMode::Both:
            pushKept(std::move(split->front));
            pushKept(std::move(split->back));
            break;
        }
    }

    if (created.empty()) {
        editor.abortEdit();
        editor.statusMessage = "Clip produced no brushes";
        reset();
        return;
    }

    editor.selectBrushes(created, created.back());
    editor.markDirty();
    editor.markBrushCompileDirty(dirtyRole);
    editor.endEdit();
    editor.statusMessage =
        "Clip kept " + std::to_string(keptCount) + " brush(es) (" + keepModeLabel() + ")";
    reset();
}

void ClipTool::update(
    Editor& editor,
    const Camera3D& camera,
    bool uiWantsMouse,
    bool uiWantsKeyboard) {
    if (phase == ClipPhase::Idle) {
        return;
    }

    if (!uiWantsKeyboard && IsKeyPressed(KEY_ESCAPE)) {
        editor.statusMessage = "Clip cancelled";
        reset();
        return;
    }

    if (phase == ClipPhase::Preview) {
        hoverValid = false;
        if (!uiWantsKeyboard && IsKeyPressed(KEY_F)) {
            if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
                planeFlipped = !planeFlipped;
                refreshPlane();
            } else {
                switch (keepMode) {
                case ClipKeepMode::Front:
                    keepMode = ClipKeepMode::Back;
                    break;
                case ClipKeepMode::Back:
                    keepMode = ClipKeepMode::Both;
                    break;
                case ClipKeepMode::Both:
                    keepMode = ClipKeepMode::Front;
                    break;
                }
            }
            setStatus(editor);
        }
        if (!uiWantsKeyboard && IsKeyPressed(KEY_ENTER)) {
            commit(editor);
            return;
        }
        return;
    }

    hoverValid = false;
    if (phase == ClipPhase::PickingP0) {
        ConstructionPlane hoverPlane{};
        Vector3 hit{};
        if (hitSelectedFace(editor, camera, hit, hoverPlane)) {
            hoverValid = true;
            hoverPoint = hit;
        }
    } else if (phase == ClipPhase::PickingP1) {
        Vector3 hit{};
        if (hitLockedFacePlane(editor, camera, hit)) {
            hoverValid = true;
            hoverPoint = hit;
            point1 = hit;
            refreshPlane();
        }
    }

    if (uiWantsMouse || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        return;
    }

    if (phase == ClipPhase::PickingP0) {
        ConstructionPlane hitPlane{};
        Vector3 hit{};
        if (!hitSelectedFace(editor, camera, hit, hitPlane)) {
            editor.statusMessage = "Clip: click a face on the selected brush";
            return;
        }
        construction = hitPlane;
        point0 = hit;
        point1 = hit;
        hoverValid = true;
        hoverPoint = hit;
        phase = ClipPhase::PickingP1;
        setStatus(editor);
        return;
    }

    if (phase == ClipPhase::PickingP1) {
        Vector3 hit{};
        if (!hitLockedFacePlane(editor, camera, hit)) {
            editor.statusMessage = "Clip: click a face on the selected brush";
            return;
        }
        point1 = hit;
        refreshPlane();
        if (length3(planeNormal) < 1e-6f || length3(sub3(point1, point0)) < editor.gridSize * 0.5f) {
            editor.statusMessage = "Clip: points too close or parallel to face edges";
            return;
        }
        hoverValid = false;
        phase = ClipPhase::Preview;
        setStatus(editor);
    }
}

void ClipTool::drawPreview(const Editor& editor, Vector3 eye, float lineWidth) const {
    if (phase == ClipPhase::Idle) {
        return;
    }

    const float markerR = std::max(0.03f, lineWidth * 2.0f);
    const float axisWidth = lineWidth * 1.5f;

    if (hoverValid && (phase == ClipPhase::PickingP0 || phase == ClipPhase::PickingP1)) {
        DrawSphere(hoverPoint, markerR, Color{255, 240, 120, 220});
    }

    if (phase == ClipPhase::PickingP0) {
        return;
    }

    DrawSphere(point0, markerR, Color{255, 220, 80, 255});
    drawThickLine3D(point0, point1, Color{255, 220, 80, 255}, axisWidth, eye);
    DrawSphere(point1, markerR, Color{255, 180, 60, 255});

    if (phase != ClipPhase::Preview || length3(planeNormal) < 1e-6f) {
        return;
    }

    const Vector3 mid = scale3(add3(point0, point1), 0.5f);
    const Vector3 edge = sub3(point1, point0);
    const float halfLen = std::max(0.5f, length3(edge) * 0.5f + 0.5f);
    const Vector3 along = normalize3(edge);
    const Vector3 up = normalize3(construction.normal);
    const Vector3 c0 = add3(add3(mid, scale3(along, -halfLen)), scale3(up, -halfLen));
    const Vector3 c1 = add3(add3(mid, scale3(along, halfLen)), scale3(up, -halfLen));
    const Vector3 c2 = add3(add3(mid, scale3(along, halfLen)), scale3(up, halfLen));
    const Vector3 c3 = add3(add3(mid, scale3(along, -halfLen)), scale3(up, halfLen));
    DrawTriangle3D(c0, c1, c2, Color{255, 220, 80, 40});
    DrawTriangle3D(c0, c2, c3, Color{255, 220, 80, 40});
    DrawTriangle3D(c0, c2, c1, Color{255, 220, 80, 40});
    DrawTriangle3D(c0, c3, c2, Color{255, 220, 80, 40});
    drawThickLine3D(
        mid, add3(mid, scale3(planeNormal, 0.4f)), Color{80, 200, 255, 255}, axisWidth, eye);

    const EditorDocument& d = editor.doc();
    int previewId = 0;
    auto allocateId = [&]() { return "clip-preview-" + std::to_string(previewId++); };
    for (int index : brushIndices) {
        if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
            continue;
        }
        std::string error;
        auto split = slopengine::splitBrushByPlane(
            d.brushes[static_cast<std::size_t>(index)],
            point0,
            planeNormal,
            allocateId,
            error);
        if (!split) {
            continue;
        }
        const bool keepFront =
            keepMode == ClipKeepMode::Front || keepMode == ClipKeepMode::Both;
        const bool keepBack = keepMode == ClipKeepMode::Back || keepMode == ClipKeepMode::Both;
        if (keepFront) {
            drawBrushSolidTint(split->front, Color{80, 220, 120, 70});
            drawBrushFaceOutlines(split->front, Color{80, 220, 120, 255}, eye, lineWidth);
        } else {
            drawBrushFaceOutlines(split->front, Color{220, 80, 80, 160}, eye, lineWidth);
        }
        if (keepBack) {
            drawBrushSolidTint(split->back, Color{80, 180, 255, 70});
            drawBrushFaceOutlines(split->back, Color{80, 180, 255, 255}, eye, lineWidth);
        } else {
            drawBrushFaceOutlines(split->back, Color{220, 80, 80, 160}, eye, lineWidth);
        }
    }
}

}
