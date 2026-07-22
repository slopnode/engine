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

bool pickCreatePlane(Editor& editor, const Ray& ray, ConstructionPlane& outPlane, Vector3& outHit) {
    float bestFaceT = std::numeric_limits<float>::max();
    int bestBrush = -1;
    int bestFace = -1;
    const EditorDocument& d = editor.doc();
    for (std::size_t i = 0; i < d.brushes.size(); ++i) {
        float faceT = 0.0f;
        const auto face = rayBrushFaceIndex(ray, d.brushes[i], &faceT);
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
        outHit = snapToGrid(hit, editor.gridSize);
        outPlane = constructionPlaneFromFace(face, outHit);
        return true;
    }

    outPlane = constructionPlaneForView(editor.viewPlane);
    if (!rayPlaneIntersection(ray, outPlane.origin, outPlane.normal, outHit)) {
        return false;
    }
    outHit = snapToGrid(outHit, editor.gridSize);
    return true;
}

} // namespace

void CreateTool::reset() {
    phase = CreatePhase::Idle;
    corner0 = {};
    corner1 = {};
    thickness = 0.0f;
    thicknessFromNumeric = false;
    pendingMins = {};
    pendingMaxs = {};
}

bool CreateTool::footprintBounds(Vector3& mins, Vector3& maxs) const {
    const float u0 = projectAxis(corner0, plane.axisU);
    const float v0 = projectAxis(corner0, plane.axisV);
    const float u1 = projectAxis(corner1, plane.axisU);
    const float v1 = projectAxis(corner1, plane.axisV);
    const float uMin = std::min(u0, u1);
    const float uMax = std::max(u0, u1);
    const float vMin = std::min(v0, v1);
    const float vMax = std::max(v0, v1);
    if (uMax - uMin < 1e-5f || vMax - vMin < 1e-5f) {
        return false;
    }

    const float n0 = projectAxis(plane.origin, plane.normal);
    Vector3 a = combineAxes(plane, uMin, vMin, n0);
    Vector3 b = combineAxes(plane, uMax, vMax, n0);
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
    const float u0 = projectAxis(corner0, plane.axisU);
    const float v0 = projectAxis(corner0, plane.axisV);
    const float u1 = projectAxis(corner1, plane.axisU);
    const float v1 = projectAxis(corner1, plane.axisV);
    const float uMin = std::min(u0, u1);
    const float uMax = std::max(u0, u1);
    const float vMin = std::min(v0, v1);
    const float vMax = std::max(v0, v1);
    if (uMax - uMin < 1e-5f || vMax - vMin < 1e-5f) {
        return false;
    }

    const float n0 = projectAxis(plane.origin, plane.normal);
    const float n1 = n0 + thickness;
    if (std::fabs(n1 - n0) < 1e-5f) {
        return false;
    }

    Vector3 a = combineAxes(plane, uMin, vMin, std::min(n0, n1));
    Vector3 b = combineAxes(plane, uMax, vMax, std::max(n0, n1));
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
        return;
    }

    EditorDocument& d = editor.doc();
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
        editor.statusMessage = "Create failed";
        reset();
        editor.showPrimitiveParamsModal = false;
        return;
    }

    editor.selectBrushes(created, created.back());
    editor.markDirty();
    editor.markBrushCompileDirty(editor.createBrushRole);
    editor.statusMessage = "Created " + std::to_string(created.size()) + " brush(es)";
    editor.numericBuffer.clear();
    editor.showPrimitiveParamsModal = false;
    reset();
}

void CreateTool::beginCommit(Editor& editor) {
    Vector3 mins{};
    Vector3 maxs{};
    if (!finalBounds(mins, maxs)) {
        reset();
        editor.numericBuffer.clear();
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
        if (active() || phase == CreatePhase::AwaitingParams) {
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
        return;
    }

    if (!uiWantsKeyboard && phase == CreatePhase::Extruding && IsKeyPressed(KEY_ENTER)) {
        beginCommit(editor);
        return;
    }

    if (uiWantsMouse) {
        return;
    }

    const Ray ray = mouseRay(camera, editor.contentViewport);

    if (phase == CreatePhase::Idle) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector3 hit{};
            if (pickCreatePlane(editor, ray, plane, hit)) {
                corner0 = hit;
                corner1 = corner0;
                phase = CreatePhase::DrawingBase;
                editor.numericBuffer.clear();
            }
        }
        return;
    }

    if (phase == CreatePhase::DrawingBase) {
        Vector3 hit{};
        if (rayPlaneIntersection(ray, plane.origin, plane.normal, hit)) {
            corner1 = snapToGrid(hit, editor.gridSize);
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            Vector3 mins{};
            Vector3 maxs{};
            if (!footprintBounds(mins, maxs)) {
                reset();
                return;
            }
            thickness = editor.gridSize;
            thicknessFromNumeric = false;
            editor.numericBuffer.clear();
            phase = CreatePhase::Extruding;
        }
        return;
    }

    if (phase == CreatePhase::Extruding) {
        if (!thicknessFromNumeric) {
            const Vector3 mid{
                0.5f * (corner0.x + corner1.x),
                0.5f * (corner0.y + corner1.y),
                0.5f * (corner0.z + corner1.z),
            };
            const float n0 = projectAxis(plane.origin, plane.normal);
            const Vector3 dragNormal = dragPlaneNormalForAxis(plane.normal, cameraForward(camera));
            Vector3 hit{};
            if (rayPlaneIntersection(ray, mid, dragNormal, hit)) {
                thickness = snapToGrid(projectAxis(hit, plane.normal) - n0, editor.gridSize);
                if (std::fabs(thickness) < editor.gridSize * 0.5f) {
                    thickness = thickness < 0.0f ? -editor.gridSize : editor.gridSize;
                }
            }
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            beginCommit(editor);
        }
    }
}

void CreateTool::drawPreview() const {
    if (phase == CreatePhase::Idle || phase == CreatePhase::AwaitingParams) {
        return;
    }

    Vector3 mins{};
    Vector3 maxs{};
    if (phase == CreatePhase::DrawingBase) {
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
