#include "extrude_tool.hpp"

#include "map/brush.hpp"
#include "preview.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace slopmap {

namespace {

bool approxEqVertex(Vector3 a, Vector3 b) {
    return length3(sub3(a, b)) <= 1e-3f;
}

/** At @p vertex (a boundary vertex of the merged extrude polygon, with its two
 *  boundary-loop neighbors @p boundaryPrev / @p boundaryNext), finds the single
 *  brush edge leaving @p vertex that isn't one of those two boundary edges —
 *  the "next ring" direction a linear taper continues along. Searches every
 *  face of @p brush except @p excludedFaceIndices (the brush's own selected
 *  faces). False if no such edge exists or candidates disagree. */
bool findTaperDirection(
    const slopengine::Brush& brush,
    const std::vector<int>& excludedFaceIndices,
    Vector3 vertex,
    Vector3 boundaryPrev,
    Vector3 boundaryNext,
    Vector3& outDirection) {
    Vector3 candidate{};
    bool found = false;
    for (int fi = 0; fi < static_cast<int>(brush.faces.size()); ++fi) {
        if (std::find(excludedFaceIndices.begin(), excludedFaceIndices.end(), fi) !=
            excludedFaceIndices.end()) {
            continue;
        }
        const slopengine::BrushFace& face = brush.faces[static_cast<std::size_t>(fi)];
        const std::size_t n = face.vertices.size();
        for (std::size_t i = 0; i < n; ++i) {
            if (!approxEqVertex(face.vertices[i], vertex)) {
                continue;
            }
            const Vector3 candidates[2] = {
                face.vertices[(i + n - 1) % n],
                face.vertices[(i + 1) % n],
            };
            for (Vector3 other : candidates) {
                if (approxEqVertex(other, boundaryPrev) || approxEqVertex(other, boundaryNext) ||
                    approxEqVertex(other, vertex)) {
                    continue;
                }
                if (!found) {
                    candidate = other;
                    found = true;
                } else if (!approxEqVertex(other, candidate)) {
                    return false;
                }
            }
        }
    }
    if (!found) {
        return false;
    }
    outDirection = sub3(vertex, candidate);
    return length3(outDirection) > 1e-6f;
}

bool enterPressed() {
    return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
}

/** Same per-vertex extrapolation extrudeFacePolygon uses internally: how far
 *  to travel along @p dir so its component along @p normal equals @p depth.
 *  Zero vector if @p dir is too close to parallel with the face plane. */
Vector3 computeSweepDelta(Vector3 dir, Vector3 normal, float depth) {
    const Vector3 unit = normalize3(dir);
    const float denom = dot3(unit, normal);
    if (std::fabs(denom) < 0.05f) {
        return {};
    }
    return scale3(unit, depth / denom);
}

/** Builds, per source brush touched by @p groups, the seed vertices and their
 *  matching deltas moveBrushVertices needs to reshape that brush in place at
 *  the given @p sweepMode / @p depth. False (with @p errorOut) if any
 *  involved vertex has a taper direction too close to parallel with the face
 *  plane to extrapolate. */
bool buildInPlaceEdits(
    const std::vector<ExtrudeTool::Group>& groups,
    ExtrudeSweepMode sweepMode,
    Vector3 planeNormal,
    float depth,
    std::unordered_map<int, std::vector<Vector3>>& seedsByBrush,
    std::unordered_map<int, std::vector<Vector3>>& deltasByBrush,
    std::string& errorOut) {
    for (const ExtrudeTool::Group& group : groups) {
        const std::vector<Vector3>& dirs =
            (sweepMode == ExtrudeSweepMode::Profile && group.profileAvailable) ? group.profileDirs
                                                                                : group.normalDirs;
        for (std::size_t i = 0; i < group.polygon.size(); ++i) {
            if (group.vertexOwnerBrushes[i].empty()) {
                continue;
            }
            const Vector3 delta = computeSweepDelta(dirs[i], planeNormal, depth);
            if (length3(delta) < 1e-6f) {
                errorOut = "a taper direction is nearly parallel to the face plane";
                return false;
            }
            for (int brushIndex : group.vertexOwnerBrushes[i]) {
                seedsByBrush[brushIndex].push_back(group.polygon[i]);
                deltasByBrush[brushIndex].push_back(delta);
            }
        }
    }
    return true;
}

} // namespace

void ExtrudeTool::reset() {
    phase = ExtrudePhase::Idle;
    sweepMode = ExtrudeSweepMode::Normal;
    targetMode = ExtrudeTargetMode::NewBrush;
    groups.clear();
    planeNormal = {};
    dragOrigin = {};
    material.clear();
    role = slopengine::BrushRole::Hull;
    depth = 0.0f;
    depthGrabScreen = {};
    depthAtGrab = 0.0f;
    depthFromNumeric = false;
}

void ExtrudeTool::setStatus(Editor& editor) const {
    const char* modeLabel = sweepMode == ExtrudeSweepMode::Normal ? "Normal" : "Profile";
    const char* targetLabel = targetMode == ExtrudeTargetMode::NewBrush ? "New" : "In-place";
    char buf[160];
    if (depthFromNumeric && !editor.numericBuffer.empty()) {
        std::snprintf(
            buf,
            sizeof(buf),
            "Extrude (%s, %s): depth %s (F sweep, M target, Enter commit)",
            modeLabel,
            targetLabel,
            editor.numericBuffer.c_str());
    } else {
        std::snprintf(
            buf,
            sizeof(buf),
            "Extrude (%s, %s): depth %.3f (F sweep, M target, Enter commit)",
            modeLabel,
            targetLabel,
            depth);
    }
    editor.statusMessage = buf;
}

void ExtrudeTool::beginFromSelection(Editor& editor) {
    reset();
    EditorDocument& d = editor.doc();

    if (d.selectionMode != SelectionMode::Face || d.selectedFaces.empty()) {
        editor.statusMessage = "Extrude: select one or more coplanar faces first (Face selection mode)";
        return;
    }

    std::vector<const slopengine::BrushFace*> faces;
    faces.reserve(d.selectedFaces.size());
    for (const FaceRef& ref : d.selectedFaces) {
        if (ref.brush < 0 || ref.brush >= static_cast<int>(d.brushes.size())) {
            editor.statusMessage = "Extrude: selection contains an invalid face";
            return;
        }
        const slopengine::Brush& b = d.brushes[static_cast<std::size_t>(ref.brush)];
        if (ref.face < 0 || ref.face >= static_cast<int>(b.faces.size())) {
            editor.statusMessage = "Extrude: selection contains an invalid face";
            return;
        }
        faces.push_back(&b.faces[static_cast<std::size_t>(ref.face)]);
    }

    std::string error;
    const std::vector<std::vector<Vector3>> loops = slopengine::traceCoplanarFaceBoundary(faces, error);
    if (loops.empty()) {
        editor.statusMessage = "Extrude: " + error;
        return;
    }

    planeNormal = faces.front()->normal;

    FaceRef anchor = d.activeFace;
    if (!anchor.valid() || anchor.brush >= static_cast<int>(d.brushes.size())) {
        anchor = d.selectedFaces.back();
    }
    const slopengine::Brush& anchorBrush = d.brushes[static_cast<std::size_t>(anchor.brush)];
    material = anchorBrush.faces[static_cast<std::size_t>(anchor.face)].material;
    role = anchorBrush.role;

    std::unordered_map<int, std::vector<int>> selectedFacesByBrush;
    for (const FaceRef& ref : d.selectedFaces) {
        selectedFacesByBrush[ref.brush].push_back(ref.face);
    }

    groups.clear();
    groups.reserve(loops.size());
    Vector3 dragSum{};
    int dragCount = 0;

    for (const std::vector<Vector3>& loop : loops) {
        Group group;
        group.polygon = loop;
        group.normalDirs.assign(loop.size(), planeNormal);
        group.profileDirs.resize(loop.size());
        group.profileAvailable = true;
        group.vertexOwnerBrushes.resize(loop.size());

        for (std::size_t i = 0; i < loop.size(); ++i) {
            const Vector3& v = loop[i];
            const Vector3& prevV = loop[(i + loop.size() - 1) % loop.size()];
            const Vector3& nextV = loop[(i + 1) % loop.size()];

            Vector3 consensus{};
            bool haveConsensus = false;
            bool ambiguous = false;
            for (const FaceRef& ref : d.selectedFaces) {
                const slopengine::Brush& b = d.brushes[static_cast<std::size_t>(ref.brush)];
                const slopengine::BrushFace& f = b.faces[static_cast<std::size_t>(ref.face)];
                bool owns = false;
                for (const Vector3& fv : f.vertices) {
                    if (approxEqVertex(fv, v)) {
                        owns = true;
                        break;
                    }
                }
                if (!owns) {
                    continue;
                }
                if (std::find(
                        group.vertexOwnerBrushes[i].begin(),
                        group.vertexOwnerBrushes[i].end(),
                        ref.brush) == group.vertexOwnerBrushes[i].end()) {
                    group.vertexOwnerBrushes[i].push_back(ref.brush);
                }
                Vector3 dir{};
                if (!findTaperDirection(b, selectedFacesByBrush[ref.brush], v, prevV, nextV, dir)) {
                    continue;
                }
                const Vector3 dirNorm = normalize3(dir);
                if (!haveConsensus) {
                    consensus = dirNorm;
                    haveConsensus = true;
                } else if (length3(sub3(dirNorm, consensus)) > 1e-3f) {
                    ambiguous = true;
                }
            }

            if (haveConsensus && !ambiguous) {
                group.profileDirs[i] = consensus;
            } else {
                group.profileAvailable = false;
                group.profileDirs[i] = planeNormal;
            }

            dragSum = add3(dragSum, v);
            ++dragCount;
        }

        groups.push_back(std::move(group));
    }

    dragOrigin = dragCount > 0 ? scale3(dragSum, 1.0f / static_cast<float>(dragCount)) : Vector3{};
    depth = editor.gridSize;
    depthFromNumeric = false;
    editor.numericBuffer.clear();
    sweepMode = ExtrudeSweepMode::Normal;

    const Vector2 grabScreen = GetMousePosition();
    phase = ExtrudePhase::Dragging;
    beginToolMouseCapture(editor, grabScreen);
    depthGrabScreen = toolMouseScreen(editor);
    depthAtGrab = depth;
    editor.mode = EditorMode::Select;
    setStatus(editor);
}

void ExtrudeTool::commit(Editor& editor) {
    EditorDocument& d = editor.doc();
    if (groups.empty()) {
        reset();
        return;
    }

    if (targetMode == ExtrudeTargetMode::InPlace) {
        std::unordered_map<int, std::vector<Vector3>> seedsByBrush;
        std::unordered_map<int, std::vector<Vector3>> deltasByBrush;
        std::string error;
        if (!buildInPlaceEdits(
                groups, sweepMode, planeNormal, depth, seedsByBrush, deltasByBrush, error)) {
            editor.statusMessage = "Extrude failed: " + error;
            reset();
            return;
        }
        if (seedsByBrush.empty()) {
            editor.statusMessage = "Extrude produced no changes";
            reset();
            return;
        }

        editor.prepareEdit();
        std::vector<int> touched;
        for (const auto& [brushIndex, seeds] : seedsByBrush) {
            if (brushIndex < 0 || brushIndex >= static_cast<int>(d.brushes.size())) {
                continue;
            }
            slopengine::Brush moved = slopengine::moveBrushVertices(
                d.brushes[static_cast<std::size_t>(brushIndex)], seeds, deltasByBrush[brushIndex]);
            if (auto err = slopengine::validateBrushConvex(moved)) {
                editor.abortEdit();
                editor.statusMessage = "Extrude failed: " + err->message;
                reset();
                return;
            }
            slopengine::cleanupBrushGeometry(moved, editor.gridSize);
            slopengine::reclassifyBrushAsBox(moved);
            d.brushes[static_cast<std::size_t>(brushIndex)] = std::move(moved);
            touched.push_back(brushIndex);
        }
        if (touched.empty()) {
            editor.abortEdit();
            editor.statusMessage = "Extrude produced no changes";
            reset();
            return;
        }

        editor.selectBrushes(touched, touched.back());
        editor.markDirty();
        editor.markBrushCompileDirty(role);
        editor.endEdit();
        editor.statusMessage = "Extrude reshaped " + std::to_string(touched.size()) + " brush(es)";
        editor.numericBuffer.clear();
        reset();
        return;
    }

    editor.prepareEdit();
    std::vector<int> created;
    created.reserve(groups.size());

    for (const Group& group : groups) {
        const std::vector<Vector3>& dirs =
            (sweepMode == ExtrudeSweepMode::Profile && group.profileAvailable) ? group.profileDirs
                                                                                : group.normalDirs;
        std::string error;
        auto brush = slopengine::extrudeFacePolygon(
            editor.allocateBrushId(),
            group.polygon,
            dirs,
            planeNormal,
            depth,
            material,
            role,
            error);
        if (!brush) {
            editor.abortEdit();
            editor.statusMessage = "Extrude failed: " + error;
            reset();
            return;
        }
        slopengine::cleanupBrushGeometry(*brush, editor.gridSize);
        d.brushes.push_back(std::move(*brush));
        created.push_back(static_cast<int>(d.brushes.size()) - 1);
    }

    if (created.empty()) {
        editor.abortEdit();
        editor.statusMessage = "Extrude produced no brushes";
        reset();
        return;
    }

    editor.selectBrushes(created, created.back());
    editor.markDirty();
    editor.markBrushCompileDirty(role);
    editor.endEdit();
    editor.statusMessage = "Extrude created " + std::to_string(created.size()) + " brush(es)";
    editor.numericBuffer.clear();
    reset();
}

void ExtrudeTool::handleNumeric(Editor& editor, bool uiWantsKeyboard) {
    if (uiWantsKeyboard || phase != ExtrudePhase::Dragging) {
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
            depth = std::max(editor.gridSize, snapToGrid(value, editor.gridSize));
        }
    }
    if (depthFromNumeric) {
        setStatus(editor);
    }
}

void ExtrudeTool::update(
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
        editor.statusMessage = "Extrude cancelled";
        return;
    }

    if (!uiWantsKeyboard && IsKeyPressed(KEY_F)) {
        bool anyProfile = false;
        for (const Group& group : groups) {
            anyProfile = anyProfile || group.profileAvailable;
        }
        if (sweepMode == ExtrudeSweepMode::Normal) {
            if (anyProfile) {
                sweepMode = ExtrudeSweepMode::Profile;
            } else {
                editor.statusMessage = "Extrude: profile direction unavailable for this selection";
            }
        } else {
            sweepMode = ExtrudeSweepMode::Normal;
        }
        setStatus(editor);
    }

    if (!uiWantsKeyboard && IsKeyPressed(KEY_M)) {
        targetMode = targetMode == ExtrudeTargetMode::NewBrush ? ExtrudeTargetMode::InPlace
                                                                : ExtrudeTargetMode::NewBrush;
        setStatus(editor);
    }

    if (!uiWantsKeyboard && enterPressed()) {
        commit(editor);
        return;
    }

    if (uiWantsMouse) {
        return;
    }

    if (!depthFromNumeric) {
        const float amount = screenDeltaAlongAxis(
            planeNormal, dragOrigin, depthGrabScreen, editor, camera, editor.contentViewport);
        depth = std::max(editor.gridSize, snapToGrid(depthAtGrab + amount, editor.gridSize));
        setStatus(editor);
    }
}

void ExtrudeTool::drawPreview(const Editor& editor, Vector3 eye, float lineWidth) const {
    if (!active()) {
        return;
    }

    auto drawTinted = [&](const slopengine::Brush& brush) {
        for (const slopengine::BrushFace& face : brush.faces) {
            const auto tris = slopengine::triangulateFace(face.vertices);
            for (const auto& tri : tris) {
                DrawTriangle3D(tri[0], tri[1], tri[2], Color{255, 170, 60, 70});
                DrawTriangle3D(tri[0], tri[2], tri[1], Color{255, 170, 60, 70});
            }
        }
        drawBrushFaceOutlines(brush, Color{255, 170, 60, 255}, eye, lineWidth);
    };

    if (targetMode == ExtrudeTargetMode::InPlace) {
        std::unordered_map<int, std::vector<Vector3>> seedsByBrush;
        std::unordered_map<int, std::vector<Vector3>> deltasByBrush;
        std::string error;
        if (!buildInPlaceEdits(
                groups, sweepMode, planeNormal, depth, seedsByBrush, deltasByBrush, error)) {
            return;
        }
        const EditorDocument& d = editor.doc();
        for (const auto& [brushIndex, seeds] : seedsByBrush) {
            if (brushIndex < 0 || brushIndex >= static_cast<int>(d.brushes.size())) {
                continue;
            }
            const slopengine::Brush moved = slopengine::moveBrushVertices(
                d.brushes[static_cast<std::size_t>(brushIndex)], seeds, deltasByBrush[brushIndex]);
            drawTinted(moved);
        }
        return;
    }

    for (const Group& group : groups) {
        const std::vector<Vector3>& dirs =
            (sweepMode == ExtrudeSweepMode::Profile && group.profileAvailable) ? group.profileDirs
                                                                                : group.normalDirs;
        std::string error;
        auto brush = slopengine::extrudeFacePolygon(
            "extrude-preview", group.polygon, dirs, planeNormal, depth, material, role, error);
        if (!brush) {
            continue;
        }
        drawTinted(*brush);
    }
}

}
