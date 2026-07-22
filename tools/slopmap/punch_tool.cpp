#include "punch_tool.hpp"

#include "map/brush.hpp"
#include "preview.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

namespace slopmap {

namespace {

float projectAxis(Vector3 point, Vector3 axis) {
    return point.x * axis.x + point.y * axis.y + point.z * axis.z;
}

struct FaceAxes {
    Vector3 origin{};
    Vector3 uAxis{};
    Vector3 vAxis{};
    Vector3 normal{};
    float uExtent = 0.0f;
    float vExtent = 0.0f;
    float depthExtent = 0.0f;
};

FaceAxes axesForBoxSide(slopengine::BrushBoxSide side, Vector3 mins, Vector3 maxs) {
    FaceAxes axes;
    switch (side) {
    case slopengine::BrushBoxSide::Top:
        axes.origin = {mins.x, maxs.y, mins.z};
        axes.uAxis = {1.0f, 0.0f, 0.0f};
        axes.vAxis = {0.0f, 0.0f, 1.0f};
        axes.normal = {0.0f, 1.0f, 0.0f};
        axes.uExtent = maxs.x - mins.x;
        axes.vExtent = maxs.z - mins.z;
        axes.depthExtent = maxs.y - mins.y;
        break;
    case slopengine::BrushBoxSide::Bottom:
        axes.origin = {mins.x, mins.y, maxs.z};
        axes.uAxis = {1.0f, 0.0f, 0.0f};
        axes.vAxis = {0.0f, 0.0f, -1.0f};
        axes.normal = {0.0f, -1.0f, 0.0f};
        axes.uExtent = maxs.x - mins.x;
        axes.vExtent = maxs.z - mins.z;
        axes.depthExtent = maxs.y - mins.y;
        break;
    case slopengine::BrushBoxSide::North:
        axes.origin = {maxs.x, mins.y, mins.z};
        axes.uAxis = {-1.0f, 0.0f, 0.0f};
        axes.vAxis = {0.0f, 1.0f, 0.0f};
        axes.normal = {0.0f, 0.0f, -1.0f};
        axes.uExtent = maxs.x - mins.x;
        axes.vExtent = maxs.y - mins.y;
        axes.depthExtent = maxs.z - mins.z;
        break;
    case slopengine::BrushBoxSide::South:
        axes.origin = {mins.x, mins.y, maxs.z};
        axes.uAxis = {1.0f, 0.0f, 0.0f};
        axes.vAxis = {0.0f, 1.0f, 0.0f};
        axes.normal = {0.0f, 0.0f, 1.0f};
        axes.uExtent = maxs.x - mins.x;
        axes.vExtent = maxs.y - mins.y;
        axes.depthExtent = maxs.z - mins.z;
        break;
    case slopengine::BrushBoxSide::East:
        axes.origin = {maxs.x, mins.y, maxs.z};
        axes.uAxis = {0.0f, 0.0f, -1.0f};
        axes.vAxis = {0.0f, 1.0f, 0.0f};
        axes.normal = {1.0f, 0.0f, 0.0f};
        axes.uExtent = maxs.z - mins.z;
        axes.vExtent = maxs.y - mins.y;
        axes.depthExtent = maxs.x - mins.x;
        break;
    case slopengine::BrushBoxSide::West:
        axes.origin = {mins.x, mins.y, mins.z};
        axes.uAxis = {0.0f, 0.0f, 1.0f};
        axes.vAxis = {0.0f, 1.0f, 0.0f};
        axes.normal = {-1.0f, 0.0f, 0.0f};
        axes.uExtent = maxs.z - mins.z;
        axes.vExtent = maxs.y - mins.y;
        axes.depthExtent = maxs.x - mins.x;
        break;
    }
    return axes;
}

Vector3 combineAxes(const FaceAxes& axes, float u, float v, float d) {
    return {
        axes.origin.x + axes.uAxis.x * u + axes.vAxis.x * v - axes.normal.x * d,
        axes.origin.y + axes.uAxis.y * u + axes.vAxis.y * v - axes.normal.y * d,
        axes.origin.z + axes.uAxis.z * u + axes.vAxis.z * v - axes.normal.z * d,
    };
}

} // namespace

void PunchTool::reset() {
    phase = PunchPhase::Idle;
    brushIndex = -1;
    corner0 = {};
    corner1 = {};
    depth = 0.0f;
    maxDepth = 0.0f;
    u0 = u1 = v0 = v1 = 0.0f;
    depthFromNumeric = false;
}

bool PunchTool::projectToFaceUV(Vector3 world, float& outU, float& outV) const {
    outU = projectAxis(sub3(world, plane.origin), plane.axisU);
    outV = projectAxis(sub3(world, plane.origin), plane.axisV);
    return true;
}

void PunchTool::beginFromSelection(Editor& editor) {
    reset();
    EditorDocument& d = editor.doc();
    FaceRef face = d.activeFace;
    if (!face.valid() && !d.selectedFaces.empty()) {
        face = d.selectedFaces.back();
    }
    if (!face.valid()) {
        editor.statusMessage = "Punch-out: select a face first (Face selection mode)";
        return;
    }
    if (face.brush < 0 || face.brush >= static_cast<int>(d.brushes.size())) {
        editor.statusMessage = "Punch-out: invalid face";
        return;
    }
    const slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(face.brush)];
    if (!brush.box) {
        editor.statusMessage = "Punch-out: only box brushes supported";
        return;
    }
    if (face.face < 0 || face.face >= static_cast<int>(brush.faces.size())) {
        editor.statusMessage = "Punch-out: invalid face";
        return;
    }

    brushIndex = face.brush;
    faceSide = slopengine::brushBoxSideFromNormal(brush.faces[static_cast<std::size_t>(face.face)].normal);
    const FaceAxes axes = axesForBoxSide(faceSide, brush.mins, brush.maxs);
    plane.origin = axes.origin;
    plane.normal = axes.normal;
    plane.axisU = axes.uAxis;
    plane.axisV = axes.vAxis;
    maxDepth = axes.depthExtent;
    depth = maxDepth;
    phase = PunchPhase::DrawingRect;
    editor.mode = EditorMode::Select;
    editor.setSelectionMode(SelectionMode::Face);
    editor.selectFace(face, false);
    editor.statusMessage = "Punch-out: drag opening on face";
}

void PunchTool::commit(Editor& editor) {
    EditorDocument& d = editor.doc();
    if (brushIndex < 0 || brushIndex >= static_cast<int>(d.brushes.size())) {
        reset();
        return;
    }
    const slopengine::Brush source = d.brushes[static_cast<std::size_t>(brushIndex)];
    auto allocateId = [&]() { return editor.allocateBrushId(); };
    auto pieces = slopengine::punchOutBrushBox(
        source,
        faceSide,
        u0,
        u1,
        v0,
        v1,
        depth,
        allocateId);
    if (pieces.empty()) {
        editor.statusMessage = "Punch-out failed";
        reset();
        return;
    }
    const slopengine::BrushRole role = source.role;
    d.brushes.erase(d.brushes.begin() + brushIndex);
    std::vector<int> created;
    created.reserve(pieces.size());
    for (slopengine::Brush& piece : pieces) {
        d.brushes.push_back(std::move(piece));
        created.push_back(static_cast<int>(d.brushes.size()) - 1);
    }
    editor.selectBrushes(created, created.back());
    editor.markDirty();
    editor.markBrushCompileDirty(role);
    editor.statusMessage = "Punch-out created " + std::to_string(created.size()) + " brushes";
    editor.numericBuffer.clear();
    reset();
}

void PunchTool::handleNumeric(Editor& editor, bool uiWantsKeyboard) {
    if (uiWantsKeyboard || phase != PunchPhase::ExtrudingDepth) {
        return;
    }
    for (int key = KEY_ZERO; key <= KEY_NINE; ++key) {
        if (IsKeyPressed(key)) {
            editor.numericBuffer.push_back(static_cast<char>('0' + (key - KEY_ZERO)));
            depthFromNumeric = true;
        }
    }
    if (IsKeyPressed(KEY_PERIOD) || IsKeyPressed(KEY_KP_DECIMAL)) {
        if (editor.numericBuffer.find('.') == std::string::npos) {
            editor.numericBuffer.push_back('.');
            depthFromNumeric = true;
        }
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !editor.numericBuffer.empty()) {
        editor.numericBuffer.pop_back();
        depthFromNumeric = !editor.numericBuffer.empty();
    }
    if (depthFromNumeric && !editor.numericBuffer.empty() && editor.numericBuffer != ".") {
        char* end = nullptr;
        const float value = std::strtof(editor.numericBuffer.c_str(), &end);
        if (end != editor.numericBuffer.c_str()) {
            depth = std::clamp(snapToGrid(value, editor.gridSize), editor.gridSize, maxDepth);
        }
    }
}

void PunchTool::update(
    Editor& editor,
    const Camera3D& camera,
    bool uiWantsMouse,
    bool uiWantsKeyboard) {
    if (!active()) {
        return;
    }

    handleNumeric(editor, uiWantsKeyboard);

    if (!uiWantsKeyboard && IsKeyPressed(KEY_ESCAPE)) {
        reset();
        editor.numericBuffer.clear();
        editor.statusMessage = "Punch-out cancelled";
        return;
    }
    if (!uiWantsKeyboard && phase == PunchPhase::ExtrudingDepth && IsKeyPressed(KEY_ENTER)) {
        commit(editor);
        return;
    }
    if (uiWantsMouse) {
        return;
    }

    const Ray ray = mouseRay(camera, editor.contentViewport);
    EditorDocument& d = editor.doc();
    if (brushIndex < 0 || brushIndex >= static_cast<int>(d.brushes.size())) {
        reset();
        return;
    }
    const slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(brushIndex)];
    const FaceAxes axes = axesForBoxSide(faceSide, brush.mins, brush.maxs);

    if (phase == PunchPhase::DrawingRect) {
        Vector3 hit{};
        if (rayPlaneIntersection(ray, plane.origin, plane.normal, hit)) {
            hit = snapToGrid(hit, editor.gridSize);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                corner0 = hit;
                corner1 = hit;
            }
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                corner1 = hit;
            }
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                float pu0 = 0.0f;
                float pv0 = 0.0f;
                float pu1 = 0.0f;
                float pv1 = 0.0f;
                projectToFaceUV(corner0, pu0, pv0);
                projectToFaceUV(corner1, pu1, pv1);
                u0 = std::clamp(std::min(pu0, pu1), 0.0f, axes.uExtent);
                u1 = std::clamp(std::max(pu0, pu1), 0.0f, axes.uExtent);
                v0 = std::clamp(std::min(pv0, pv1), 0.0f, axes.vExtent);
                v1 = std::clamp(std::max(pv0, pv1), 0.0f, axes.vExtent);
                if (u1 - u0 < editor.gridSize * 0.5f || v1 - v0 < editor.gridSize * 0.5f) {
                    editor.statusMessage = "Punch-out: opening too small";
                    return;
                }
                depth = maxDepth;
                depthFromNumeric = false;
                editor.numericBuffer.clear();
                phase = PunchPhase::ExtrudingDepth;
                editor.statusMessage = "Punch-out: set depth (Enter commit)";
            }
        }
        return;
    }

    if (phase == PunchPhase::ExtrudingDepth) {
        if (!depthFromNumeric) {
            const Vector3 mid = combineAxes(axes, 0.5f * (u0 + u1), 0.5f * (v0 + v1), 0.0f);
            const Vector3 dragNormal = dragPlaneNormalForAxis(plane.normal, cameraForward(camera));
            Vector3 hit{};
            if (rayPlaneIntersection(ray, mid, dragNormal, hit)) {
                const float along = projectAxis(sub3(hit, plane.origin), scale3(plane.normal, -1.0f));
                depth = std::clamp(snapToGrid(along, editor.gridSize), editor.gridSize, maxDepth);
            }
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            commit(editor);
        }
    }
}

void PunchTool::drawPreview() const {
    if (!active() || brushIndex < 0) {
        return;
    }
    if (phase == PunchPhase::DrawingRect) {
        float pu0 = 0.0f;
        float pv0 = 0.0f;
        float pu1 = 0.0f;
        float pv1 = 0.0f;
        projectToFaceUV(corner0, pu0, pv0);
        projectToFaceUV(corner1, pu1, pv1);
        const float a = std::min(pu0, pu1);
        const float b = std::max(pu0, pu1);
        const float c = std::min(pv0, pv1);
        const float d = std::max(pv0, pv1);
        FaceAxes axes;
        axes.origin = plane.origin;
        axes.uAxis = plane.axisU;
        axes.vAxis = plane.axisV;
        axes.normal = plane.normal;
        Vector3 mins = combineAxes(axes, a, c, 0.0f);
        Vector3 maxs = combineAxes(axes, b, d, 0.01f);
        Vector3 aabbMins{
            std::min(mins.x, maxs.x),
            std::min(mins.y, maxs.y),
            std::min(mins.z, maxs.z),
        };
        Vector3 aabbMaxs{
            std::max(mins.x, maxs.x),
            std::max(mins.y, maxs.y),
            std::max(mins.z, maxs.z),
        };
        drawAabbWires(aabbMins, aabbMaxs, Color{255, 120, 80, 255});
        return;
    }

    if (phase == PunchPhase::ExtrudingDepth) {
        FaceAxes axes;
        axes.origin = plane.origin;
        axes.uAxis = plane.axisU;
        axes.vAxis = plane.axisV;
        axes.normal = plane.normal;
        Vector3 a = combineAxes(axes, u0, v0, 0.0f);
        Vector3 b = combineAxes(axes, u1, v1, depth);
        Vector3 aabbMins{
            std::min(a.x, b.x),
            std::min(a.y, b.y),
            std::min(a.z, b.z),
        };
        Vector3 aabbMaxs{
            std::max(a.x, b.x),
            std::max(a.y, b.y),
            std::max(a.z, b.z),
        };
        drawAabbWires(aabbMins, aabbMaxs, Color{255, 120, 80, 255});
        drawAabbSolid(aabbMins, aabbMaxs, Color{255, 120, 80, 50});
    }
}

}
