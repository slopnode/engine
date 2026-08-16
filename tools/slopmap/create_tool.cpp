#include "create_tool.hpp"

#include "map/brush.hpp"
#include "preview.hpp"
#include "select_tool.hpp"

#include <cstddef>

#include <algorithm>
#include <cmath>
#include <cstdio>
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
    return snapOnConstructionPlane(point, plane, grid);
}

// Spiral stairs read a single radius off the footprint (see
// makeBrushSpiralStairs), so unlike Box/Cylinder the drag must stay square
// rather than let independent U/V extents draw an oval.
Vector3 squareFootprintCorner(
    Vector3 corner0,
    Vector3 candidate,
    const ConstructionPlane& plane,
    float grid) {
    const Vector3 rel0 = sub3(corner0, plane.origin);
    const Vector3 rel1 = sub3(candidate, plane.origin);
    const float u0 = projectAxis(rel0, plane.axisU);
    const float v0 = projectAxis(rel0, plane.axisV);
    const float u1 = projectAxis(rel1, plane.axisU);
    const float v1 = projectAxis(rel1, plane.axisV);
    const float side = std::max(std::fabs(u1 - u0), std::fabs(v1 - v0));
    const float su = u0 + std::copysign(side, u1 - u0);
    const float sv = v0 + std::copysign(side, v1 - v0);
    return snapOnConstructionPlane(combineAxes(plane, su, sv, 0.0f), plane, grid);
}

bool enterPressed() {
    return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
}

bool enterOrClick(bool uiWantsMouse) {
    if (enterPressed()) {
        return true;
    }
    return !uiWantsMouse && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

} // namespace

void CreateTool::reset() {
    phase = CreatePhase::Idle;
    corner0 = {};
    corner1 = {};
    thickness = 0.0f;
    thicknessFromNumeric = false;
    thicknessGrabScreen = {};
    thicknessAtGrab = 0.0f;
    hoverValid = false;
    pendingMins = {};
    pendingMaxs = {};
}

void CreateTool::setStatus(Editor& editor) const {
    switch (phase) {
    case CreatePhase::Idle:
        editor.statusMessage = "Create: set first corner (click or Enter)";
        break;
    case CreatePhase::DrawingBase:
        editor.statusMessage = "Create: set opposite corner (click or Enter)";
        break;
    case CreatePhase::Extruding: {
        char buf[96];
        if (thicknessFromNumeric && !editor.numericBuffer.empty()) {
            std::snprintf(
                buf,
                sizeof(buf),
                "Create: height %s (click or Enter commit)",
                editor.numericBuffer.c_str());
        } else {
            std::snprintf(
                buf,
                sizeof(buf),
                "Create: height %.3f (click or Enter commit)",
                thickness);
        }
        editor.statusMessage = buf;
        break;
    }
    case CreatePhase::AwaitingParams:
        break;
    }
}

bool CreateTool::formatCreateMetrics(char* buf, std::size_t bufSize) const {
    if (buf == nullptr || bufSize == 0) {
        return false;
    }
    buf[0] = '\0';

    if (phase == CreatePhase::Idle) {
        if (!hoverValid) {
            return false;
        }
        std::snprintf(
            buf,
            bufSize,
            "P1: %.3f, %.3f, %.3f",
            corner0.x,
            corner0.y,
            corner0.z);
        return true;
    }

    if (phase == CreatePhase::DrawingBase || phase == CreatePhase::Extruding) {
        Vector3 mins{};
        Vector3 maxs{};
        const bool haveBounds = phase == CreatePhase::Extruding ? finalBounds(mins, maxs)
                                                                : footprintBounds(mins, maxs);
        if (haveBounds) {
            const Vector3 size{
                maxs.x - mins.x,
                maxs.y - mins.y,
                maxs.z - mins.z,
            };
            std::snprintf(
                buf,
                bufSize,
                "P1: %.3f, %.3f, %.3f  P2: %.3f, %.3f, %.3f  Size: %.3f, %.3f, %.3f",
                corner0.x,
                corner0.y,
                corner0.z,
                corner1.x,
                corner1.y,
                corner1.z,
                size.x,
                size.y,
                size.z);
        } else {
            std::snprintf(
                buf,
                bufSize,
                "P1: %.3f, %.3f, %.3f  P2: %.3f, %.3f, %.3f",
                corner0.x,
                corner0.y,
                corner0.z,
                corner1.x,
                corner1.y,
                corner1.z);
        }
        return true;
    }

    return false;
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

    if (editor.createPrimitive == CreatePrimitive::Cylinder) {
        std::string error;
        auto brush = slopengine::makeBrushCylinder(
            editor.allocateBrushId(),
            mins,
            maxs,
            editor.createCylinderSides,
            d.defaultMaterial,
            editor.createBrushRole,
            error,
            plane.normal);
        if (!brush) {
            editor.abortEdit();
            editor.statusMessage = error;
            reset();
            editor.showPrimitiveParamsModal = false;
            return;
        }
        brush->blocks = slopengine::brushRoleDefaultBlocks(editor.createBrushRole);
        slopengine::syncBrushNocollide(*brush);
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
            brush.blocks = slopengine::brushRoleDefaultBlocks(editor.createBrushRole);
            slopengine::syncBrushNocollide(brush);
            d.brushes.push_back(std::move(brush));
            created.push_back(static_cast<int>(d.brushes.size()) - 1);
        }
    } else if (editor.createPrimitive == CreatePrimitive::SpiralStairs) {
        const std::string prefix = editor.allocateBrushId();
        auto stairs = slopengine::makeBrushSpiralStairs(
            prefix,
            mins,
            maxs,
            editor.createSpiralInnerRadius,
            editor.createSpiralStepHeight,
            editor.createSpiralSides,
            d.defaultMaterial,
            editor.createBrushRole,
            plane.normal);
        for (slopengine::Brush& brush : stairs) {
            brush.blocks = slopengine::brushRoleDefaultBlocks(editor.createBrushRole);
            slopengine::syncBrushNocollide(brush);
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
        brush.blocks = slopengine::brushRoleDefaultBlocks(editor.createBrushRole);
        slopengine::syncBrushNocollide(brush);
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
    if (thicknessFromNumeric) {
        setStatus(editor);
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

    const Ray ray = toolMouseRay(editor, camera, editor.contentViewport);

    if (phase == CreatePhase::Idle) {
        hoverValid = false;
        if (!uiWantsMouse) {
            Vector3 hit{};
            ConstructionPlane hoverPlane{};
            if (pickConstructionPlane(editor, ray, hoverPlane, hit)) {
                plane = hoverPlane;
                corner0 = hit;
                corner1 = hit;
                hoverValid = true;
            }
        }
        if (!uiWantsKeyboard && enterOrClick(uiWantsMouse)) {
            if (!hoverValid) {
                editor.statusMessage = "Create: aim at the grid or a brush face";
                return;
            }
            const Vector2 grabScreen = GetMousePosition();
            phase = CreatePhase::DrawingBase;
            beginToolMouseCapture(editor, grabScreen);
            setStatus(editor);
        }
        return;
    }

    if (phase == CreatePhase::DrawingBase) {
        if (!uiWantsMouse) {
            Vector3 hit{};
            if (rayPlaneIntersection(ray, plane.origin, plane.normal, hit)) {
                corner1 = snapOnPlane(hit, plane, editor.gridSize);
                if (editor.createPrimitive == CreatePrimitive::SpiralStairs) {
                    corner1 = squareFootprintCorner(corner0, corner1, plane, editor.gridSize);
                }
            }
        }
        if (!uiWantsKeyboard && enterOrClick(uiWantsMouse)) {
            Vector3 mins{};
            Vector3 maxs{};
            if (!footprintBounds(mins, maxs)) {
                editor.statusMessage = "Create: opposite corner too close (click or Enter)";
                return;
            }
            thickness = editor.gridSize;
            thicknessFromNumeric = false;
            editor.numericBuffer.clear();
            thicknessGrabScreen = toolMouseScreen(editor);
            thicknessAtGrab = thickness;
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
            const float amount = screenDeltaAlongAxis(
                plane.normal,
                mid,
                thicknessGrabScreen,
                editor,
                camera,
                editor.contentViewport,
                &plane);
            thickness = snapToGrid(thicknessAtGrab - amount, editor.gridSize);
            if (std::fabs(thickness) < editor.gridSize * 0.5f) {
                thickness = thickness < 0.0f ? -editor.gridSize : editor.gridSize;
            }
            setStatus(editor);
        }

        if (!uiWantsKeyboard && enterOrClick(uiWantsMouse)) {
            beginCommit(editor);
        }
    }
}

void CreateTool::drawPreview(Vector3 eye, float lineWidth) const {
    if (phase == CreatePhase::AwaitingParams) {
        return;
    }

    constexpr Color kUCross{255, 90, 90, 255};
    constexpr Color kVCross{90, 255, 90, 255};
    constexpr Color kNormalArrow{80, 160, 255, 255};
    const float crossHalf = std::max(0.12f, lineWidth * 10.0f);
    const float normalStub = std::max(0.25f, lineWidth * 14.0f);

    if (phase == CreatePhase::Idle) {
        if (hoverValid) {
            drawConstructionPlaneGizmo(
                corner0, plane.axisU, plane.axisV, plane.normal, eye, lineWidth, normalStub);
        }
        return;
    }

    Vector3 mins{};
    Vector3 maxs{};
    if (phase == CreatePhase::DrawingBase) {
        drawConstructionPlaneGizmo(
            corner0, plane.axisU, plane.axisV, plane.normal, eye, lineWidth, normalStub);
        drawPlaneCrosshair(corner1, plane.axisU, plane.axisV, crossHalf, kUCross, kVCross, eye, lineWidth);
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

        const Vector3 baseCenter{
            0.5f * (corner0.x + corner1.x),
            0.5f * (corner0.y + corner1.y),
            0.5f * (corner0.z + corner1.z),
        };
        const float arrowLen = std::max(std::fabs(thickness), normalStub);
        const Vector3 extrudeDir =
            thickness >= 0.0f ? plane.normal : scale3(plane.normal, -1.0f);
        drawDirectionArrow(baseCenter, extrudeDir, arrowLen, kNormalArrow, eye, lineWidth);
    }
}

}
