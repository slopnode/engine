#include "create_tool.hpp"

#include "map/brush.hpp"
#include "select_tool.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

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

bool pickCreatePlane(Editor& editor, const Ray& ray, ConstructionPlane& outPlane, Vector3& outHit) {
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
        outPlane = constructionPlaneFromFace(face, hit);
        outHit = snapOnPlane(hit, outPlane, editor.gridSize);
        outPlane.origin = outHit;
        return true;
    }

    outPlane = constructionPlaneForView(editor.viewPlane, editor.gridPlane);
    if (!rayPlaneIntersection(ray, outPlane.origin, outPlane.normal, outHit)) {
        return false;
    }
    outHit = snapOnPlane(outHit, outPlane, editor.gridSize);
    outPlane.origin = outHit;
    return true;
}

bool enterPressed() {
    return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
}

} // namespace

void CreateTool::reset() {
    phase = CreatePhase::Idle;
    corner0 = {};
    corner1 = {};
    thickness = 0.0f;
    thicknessFromNumeric = false;
    hoverValid = false;
    pendingMins = {};
    pendingMaxs = {};
}

void CreateTool::setStatus(Editor& editor) const {
    switch (phase) {
    case CreatePhase::Idle:
        editor.statusMessage = "Create: set first corner (Enter)";
        break;
    case CreatePhase::DrawingBase:
        editor.statusMessage = "Create: set opposite corner (Enter)";
        break;
    case CreatePhase::Extruding:
        editor.statusMessage = "Create: set height (Enter commit)";
        break;
    case CreatePhase::AwaitingParams:
        break;
    }
}

bool CreateTool::footprintBounds(Vector3& mins, Vector3& maxs) const {
    const Vector3 rel0 = sub3(corner0, plane.origin);
    const Vector3 rel1 = sub3(corner1, plane.origin);
    const float u0 = projectAxis(rel0, plane.axisU);
    const float v0 = projectAxis(rel0, plane.axisV);
    const float u1 = projectAxis(rel1, plane.axisU);
    const float v1 = projectAxis(rel1, plane.axisV);
    const float uMin = std::min(u0, u1);
    const float uMax = std::max(u0, u1);
    const float vMin = std::min(v0, v1);
    const float vMax = std::max(v0, v1);
    if (uMax - uMin < 1e-5f || vMax - vMin < 1e-5f) {
        return false;
    }

    Vector3 a = combineAxes(plane, uMin, vMin, 0.0f);
    Vector3 b = combineAxes(plane, uMax, vMax, 0.0f);
    mins = {
        std::min(a.x, b.x),
        std::min(a.y, b.y),
        std::min(a.z, b.z),
    };
    maxs = {
        std::max(a.x, b.x),
        std::max(a.y, b.y),
        std::max(a.z, b.z),
    };
    return true;
}

bool CreateTool::finalBounds(Vector3& mins, Vector3& maxs) const {
    const Vector3 rel0 = sub3(corner0, plane.origin);
    const Vector3 rel1 = sub3(corner1, plane.origin);
    const float u0 = projectAxis(rel0, plane.axisU);
    const float v0 = projectAxis(rel0, plane.axisV);
    const float u1 = projectAxis(rel1, plane.axisU);
    const float v1 = projectAxis(rel1, plane.axisV);
    const float uMin = std::min(u0, u1);
    const float uMax = std::max(u0, u1);
    const float vMin = std::min(v0, v1);
    const float vMax = std::max(v0, v1);
    if (uMax - uMin < 1e-5f || vMax - vMin < 1e-5f) {
        return false;
    }
    if (std::fabs(thickness) < 1e-5f) {
        return false;
    }

    const float nMin = std::min(0.0f, thickness);
    const float nMax = std::max(0.0f, thickness);
    Vector3 a = combineAxes(plane, uMin, vMin, nMin);
    Vector3 b = combineAxes(plane, uMax, vMax, nMax);
    mins = {
        std::min(a.x, b.x),
        std::min(a.y, b.y),
        std::min(a.z, b.z),
    };
    maxs = {
        std::max(a.x, b.x),
        std::max(a.y, b.y),
        std::max(a.z, b.z),
    };
    return true;
}

void CreateTool::commitPending(Editor& editor) {
    Vector3 mins = pendingMins;
    Vector3 maxs = pendingMaxs;
    if (maxs.x <= mins.x || maxs.y <= mins.y || maxs.z <= mins.z) {
        reset();
        editor.showPrimitiveParamsModal = false;
        setStatus(editor);
        return;
    }

    EditorDocument& d = editor.doc();
    editor.prepareEdit();
    std::vector<int> created;
    const auto nocollide = slopengine::brushRoleDefaultNocollide(editor.createBrushRole);

    if (editor.createPrimitive == CreatePrimitive::Cylinder) {
        std::string error;
        auto brush = slopengine::makeBrushCylinder(
            editor.allocateBrushId(),
            mins,
            maxs,
            editor.createCylinderSides,
            d.defaultMaterial,
            editor.createBrushRole,
            error);
        if (!brush) {
            editor.abortEdit();
            editor.statusMessage = error;
            reset();
            editor.showPrimitiveParamsModal = false;
            return;
        }
        brush->nocollide = nocollide;
        d.brushes.push_back(std::move(*brush));
        created.push_back(static_cast<int>(d.brushes.size()) - 1);
    } else if (editor.createPrimitive == CreatePrimitive::Stairs) {
        const std::string prefix = editor.allocateBrushId();
        auto stairs = slopengine::makeBrushStairs(
            prefix,
            mins,
            maxs,
            editor.createStairsSteps,
            d.defaultMaterial,
            editor.createBrushRole);
        for (slopengine::Brush& brush : stairs) {
            brush.nocollide = nocollide;
            d.brushes.push_back(std::move(brush));
            created.push_back(static_cast<int>(d.brushes.size()) - 1);
        }
    } else {
        slopengine::Brush brush = slopengine::makeBrushBox(
            editor.allocateBrushId(),
            mins,
            maxs,
            d.defaultMaterial,
            {},
            editor.createBrushRole);
        brush.nocollide = nocollide;
        d.brushes.push_back(std::move(brush));
        created.push_back(static_cast<int>(d.brushes.size()) - 1);
    }

    if (created.empty()) {
        editor.abortEdit();
        editor.statusMessage = "Create failed";
        reset();
        editor.showPrimitiveParamsModal = false;
        return;
    }

    editor.selectBrushes(created, created.back());
    editor.markDirty();
    editor.markBrushCompileDirty(editor.createBrushRole);
    editor.endEdit();
    const std::string createdMsg = "Created " + std::to_string(created.size()) + " brush(es)";
    editor.numericBuffer.clear();
    editor.showPrimitiveParamsModal = false;
    reset();
    setStatus(editor);
    editor.statusMessage = createdMsg + ". " + editor.statusMessage;
}

void CreateTool::beginCommit(Editor& editor) {
    Vector3 mins{};
    Vector3 maxs{};
    if (!finalBounds(mins, maxs)) {
        editor.statusMessage = "Create: height too small";
        return;
    }
    pendingMins = mins;
    pendingMaxs = maxs;
    if (editor.createPrimitive == CreatePrimitive::Box) {
        commitPending(editor);
        return;
    }
    phase = CreatePhase::AwaitingParams;
    editor.showPrimitiveParamsModal = true;
}

void CreateTool::handleNumeric(Editor& editor, bool uiWantsKeyboard) {
    if (uiWantsKeyboard || phase != CreatePhase::Extruding) {
        return;
    }

    for (int key = KEY_ZERO; key <= KEY_NINE; ++key) {
        if (IsKeyPressed(key)) {
            editor.numericBuffer.push_back(static_cast<char>('0' + (key - KEY_ZERO)));
            thicknessFromNumeric = true;
        }
    }
    if (IsKeyPressed(KEY_PERIOD) || IsKeyPressed(KEY_KP_DECIMAL)) {
        if (editor.numericBuffer.find('.') == std::string::npos) {
            editor.numericBuffer.push_back('.');
            thicknessFromNumeric = true;
        }
    }
    if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) {
        if (editor.numericBuffer.empty()) {
            editor.numericBuffer.push_back('-');
            thicknessFromNumeric = true;
        }
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !editor.numericBuffer.empty()) {
        editor.numericBuffer.pop_back();
        thicknessFromNumeric = !editor.numericBuffer.empty();
    }

    if (thicknessFromNumeric && !editor.numericBuffer.empty() && editor.numericBuffer != "-" &&
        editor.numericBuffer != "." && editor.numericBuffer != "-.") {
        char* end = nullptr;
        const float value = std::strtof(editor.numericBuffer.c_str(), &end);
        if (end != editor.numericBuffer.c_str()) {
            thickness = snapToGrid(value, editor.gridSize);
        }
    }
}

void CreateTool::update(Editor& editor, const Camera3D& camera, bool uiWantsMouse, bool uiWantsKeyboard) {
    if (editor.mode != EditorMode::Create) {
        if (active() || phase == CreatePhase::AwaitingParams || hoverValid) {
            reset();
            editor.numericBuffer.clear();
            editor.showPrimitiveParamsModal = false;
        }
        return;
    }

    if (phase == CreatePhase::AwaitingParams) {
        return;
    }

    handleNumeric(editor, uiWantsKeyboard);

    if (!uiWantsKeyboard && IsKeyPressed(KEY_ESCAPE)) {
        reset();
        editor.numericBuffer.clear();
        editor.statusMessage = "Create cancelled";
        setStatus(editor);
        return;
    }

    const Ray ray = mouseRay(camera, editor.contentViewport);

    if (phase == CreatePhase::Idle) {
        hoverValid = false;
        if (!uiWantsMouse) {
            Vector3 hit{};
            ConstructionPlane hoverPlane{};
            if (pickCreatePlane(editor, ray, hoverPlane, hit)) {
                plane = hoverPlane;
                corner0 = hit;
                corner1 = hit;
                hoverValid = true;
            }
        }
        if (!uiWantsKeyboard && enterPressed()) {
            if (!hoverValid) {
                editor.statusMessage = "Create: aim at the grid or a brush face";
                return;
            }
            phase = CreatePhase::DrawingBase;
            setStatus(editor);
        }
        return;
    }

    if (phase == CreatePhase::DrawingBase) {
        if (!uiWantsMouse) {
            Vector3 hit{};
            if (rayPlaneIntersection(ray, plane.origin, plane.normal, hit)) {
                corner1 = snapOnPlane(hit, plane, editor.gridSize);
            }
        }
        if (!uiWantsKeyboard && enterPressed()) {
            Vector3 mins{};
            Vector3 maxs{};
            if (!footprintBounds(mins, maxs)) {
                editor.statusMessage = "Create: opposite corner too close (Enter)";
                return;
            }
            thickness = editor.gridSize;
            thicknessFromNumeric = false;
            editor.numericBuffer.clear();
            phase = CreatePhase::Extruding;
            setStatus(editor);
        }
        return;
    }

    if (phase == CreatePhase::Extruding) {
        if (!uiWantsMouse && !thicknessFromNumeric) {
            const Vector3 mid{
                0.5f * (corner0.x + corner1.x),
                0.5f * (corner0.y + corner1.y),
                0.5f * (corner0.z + corner1.z),
            };
            const Vector3 dragNormal = dragPlaneNormalForAxis(plane.normal, cameraForward(camera));
            Vector3 hit{};
            if (rayPlaneIntersection(ray, mid, dragNormal, hit)) {
                thickness =
                    snapToGrid(projectAxis(sub3(hit, plane.origin), plane.normal), editor.gridSize);
                if (std::fabs(thickness) < editor.gridSize * 0.5f) {
                    thickness = thickness < 0.0f ? -editor.gridSize : editor.gridSize;
                }
            }
        }

        if (!uiWantsKeyboard && enterPressed()) {
            beginCommit(editor);
        }
    }
}

void CreateTool::drawPreview() const {
    if (phase == CreatePhase::AwaitingParams) {
        return;
    }

    if (phase == CreatePhase::Idle) {
        if (hoverValid) {
            DrawSphere(corner0, 0.08f, Color{255, 220, 80, 255});
        }
        return;
    }

    Vector3 mins{};
    Vector3 maxs{};
    if (phase == CreatePhase::DrawingBase) {
        DrawSphere(corner0, 0.08f, Color{255, 220, 80, 255});
        DrawSphere(corner1, 0.08f, Color{255, 180, 60, 255});
        if (footprintBounds(mins, maxs)) {
            maxs.x = std::max(maxs.x, mins.x + 0.01f);
            maxs.y = std::max(maxs.y, mins.y + 0.01f);
            maxs.z = std::max(maxs.z, mins.z + 0.01f);
            drawAabbWires(mins, maxs, YELLOW);
            drawAabbSolid(mins, maxs, Color{255, 255, 0, 40});
        }
        return;
    }

    if (phase == CreatePhase::Extruding && finalBounds(mins, maxs)) {
        drawAabbWires(mins, maxs, Color{80, 200, 255, 255});
        drawAabbSolid(mins, maxs, Color{80, 200, 255, 50});
    }
}

}
