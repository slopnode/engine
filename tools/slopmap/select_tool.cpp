#include "select_tool.hpp"

#include "placement_draw.hpp"

#include "assets/material_loader.hpp"
#include "map/brush.hpp"
#include "map/csg_compile.hpp"
#include "map/uv_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string_view>

namespace slopmap {

namespace {

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

Vector3 translateDragPlaneNormal(
    ViewPlane view,
    TranslateAxis axisLock,
    const Camera3D& camera) {
    const ViewPlane effective =
        view == ViewPlane::PerspectiveY0 ? ViewPlane::Top : view;
    const Vector3 forward = cameraForward(camera);

    if (axisLock == TranslateAxis::Y) {
        return dragPlaneNormalForAxis({0.0f, 1.0f, 0.0f}, forward);
    }
    if (axisLock == TranslateAxis::X && effective == ViewPlane::Side) {
        return dragPlaneNormalForAxis({1.0f, 0.0f, 0.0f}, forward);
    }
    if (axisLock == TranslateAxis::Z && effective == ViewPlane::Front) {
        return dragPlaneNormalForAxis({0.0f, 0.0f, 1.0f}, forward);
    }
    return constructionPlaneForView(effective).normal;
}

Vector3 currentTranslateDelta(const Editor& editor, const SelectTool& tool) {
    const EditorDocument& d = editor.doc();
    if (d.selection == SelectionTarget::Instance &&
        d.selectedInstance >= 0 &&
        d.selectedInstance < static_cast<int>(d.instances.size())) {
        return sub3(
            d.instances[static_cast<std::size_t>(d.selectedInstance)].at,
            tool.instanceAtSnapshot);
    }
    if (d.selection == SelectionTarget::Brush &&
        d.selectedBrush >= 0 &&
        d.selectedBrush < static_cast<int>(d.brushes.size()) &&
        !tool.brushSnapshot.empty()) {
        return sub3(
            d.brushes[static_cast<std::size_t>(d.selectedBrush)].mins,
            tool.brushSnapshot[0].mins);
    }
    return {};
}

void refreshTranslateGrab(SelectTool& tool, Editor& editor, const Camera3D& camera) {
    const Vector3 planeNormal =
        translateDragPlaneNormal(editor.viewPlane, tool.axisLock, camera);
    const Vector3 applied = currentTranslateDelta(editor, tool);
    Vector3 hit{};
    if (rayPlaneIntersection(
            mouseRay(camera, editor.contentViewport),
            tool.translateOrigin,
            planeNormal,
            hit)) {
        tool.mouseGrabWorld = sub3(hit, applied);
    }
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
                    face.uvShiftPixels.y != 0.0f || face.material != material ||
                    face.id != src.id + suffix) {
                    slopengine::BrushFace overrideFace;
                    overrideFace.id = face.id;
                    overrideFace.material = face.material;
                    overrideFace.uvShiftPixels = face.uvShiftPixels;
                    overrideFace.nodraw = face.nodraw;
                    overrideFace.uvLock = face.uvLock;
                    overrideFace.uvUAxis = face.uvUAxis;
                    overrideFace.uvVAxis = face.uvVAxis;
                    overrides.emplace_back(side, overrideFace);
                }
                break;
            }
        }
    }
    slopengine::Brush brush =
        slopengine::makeBrushBox(src.id, mins, maxs, material, overrides, src.role);
    brush.nocollide = src.nocollide;
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

    slopengine::Brush brush = src;
    slopengine::BrushFace& face = brush.faces[static_cast<std::size_t>(faceIndex)];
    const Vector3 oldRef = face.vertices.empty() ? Vector3{} : face.vertices[0];
    Vector3 oldU{};
    Vector3 oldV{};
    if (face.uvLock) {
        slopengine::ensureFaceUvAxes(face);
        oldU = face.uvUAxis;
        oldV = face.uvVAxis;
    }
    const Vector3 n = face.normal;
    for (Vector3& v : face.vertices) {
        v = add3(v, scale3(n, distance));
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
    slopengine::recomputeBrushBounds(brush);
    return brush;
}

} // namespace

std::optional<float> rayBrushHitDistance(Ray ray, const slopengine::Brush& brush) {
    float best = std::numeric_limits<float>::max();
    bool hit = false;
    for (const slopengine::BrushFace& face : brush.faces) {
        if (face.vertices.size() < 3) {
            continue;
        }
        for (std::size_t i = 1; i + 1 < face.vertices.size(); ++i) {
            float t = 0.0f;
            if (rayTriangle(ray, face.vertices[0], face.vertices[i], face.vertices[i + 1], t) && t < best) {
                best = t;
                hit = true;
            }
        }
    }
    if (!hit) {
        float t = 0.0f;
        if (rayAabb(ray, brush.mins, brush.maxs, t)) {
            return t;
        }
        return std::nullopt;
    }
    return best;
}

std::optional<int> rayBrushFaceIndex(Ray ray, const slopengine::Brush& brush, float* outDistance) {
    float best = std::numeric_limits<float>::max();
    int bestFace = -1;
    for (std::size_t fi = 0; fi < brush.faces.size(); ++fi) {
        const slopengine::BrushFace& face = brush.faces[fi];
        if (face.vertices.size() < 3) {
            continue;
        }
        for (std::size_t i = 1; i + 1 < face.vertices.size(); ++i) {
            float t = 0.0f;
            if (rayTriangle(ray, face.vertices[0], face.vertices[i], face.vertices[i + 1], t) && t < best) {
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
    EditorDocument& d = editor.doc();
    if (d.selection == SelectionTarget::Placement) {
        if (d.selectedPlacement < 0 ||
            d.selectedPlacement >= static_cast<int>(d.placements.size())) {
            return;
        }
        translating = true;
        axisLock = TranslateAxis::None;
        numericActive = false;
        editor.numericBuffer.clear();
        brushSnapshot.clear();
        placementAtSnapshot = d.placements[static_cast<std::size_t>(d.selectedPlacement)].haveAt
            ? d.placements[static_cast<std::size_t>(d.selectedPlacement)].at
            : Vector3{};
        translateOrigin = placementAtSnapshot;
    } else if (d.selection == SelectionTarget::Instance) {
        if (d.selectedInstance < 0 || d.selectedInstance >= static_cast<int>(d.instances.size())) {
            return;
        }
        translating = true;
        axisLock = TranslateAxis::None;
        numericActive = false;
        editor.numericBuffer.clear();
        brushSnapshot.clear();
        instanceAtSnapshot = d.instances[static_cast<std::size_t>(d.selectedInstance)].at;
        translateOrigin = instanceAtSnapshot;
    } else if (d.selection == SelectionTarget::Brush) {
        if (d.selectedBrush < 0 || d.selectedBrush >= static_cast<int>(d.brushes.size())) {
            return;
        }
        translating = true;
        axisLock = TranslateAxis::None;
        numericActive = false;
        editor.numericBuffer.clear();
        brushSnapshot.clear();
        brushSnapshot.push_back(d.brushes[static_cast<std::size_t>(d.selectedBrush)]);
        translateOrigin = editor.selectionCenter();
    } else {
        return;
    }

    const Vector3 planeNormal = translateDragPlaneNormal(editor.viewPlane, axisLock, camera);
    Vector3 hit{};
    if (rayPlaneIntersection(
            mouseRay(camera, editor.contentViewport),
            translateOrigin,
            planeNormal,
            hit)) {
        mouseGrabWorld = hit;
    } else {
        mouseGrabWorld = translateOrigin;
    }
    editor.statusMessage = "Translate (G): move, X/Y/Z lock, type units, Enter confirm, Esc cancel";
}

void SelectTool::applyTranslate(Editor& editor, slopengine::AssetStore& assets, Vector3 delta) {
    EditorDocument& d = editor.doc();
    if (d.selection == SelectionTarget::Placement) {
        if (d.selectedPlacement < 0 ||
            d.selectedPlacement >= static_cast<int>(d.placements.size())) {
            return;
        }
        delta = snapToGrid(delta, editor.gridSize);
        slopengine::Placement& placement =
            d.placements[static_cast<std::size_t>(d.selectedPlacement)];
        placement.at = add3(placementAtSnapshot, delta);
        placement.haveAt = true;
        return;
    }
    if (d.selection == SelectionTarget::Instance) {
        if (d.selectedInstance < 0 || d.selectedInstance >= static_cast<int>(d.instances.size())) {
            return;
        }
        delta = snapToGrid(delta, editor.gridSize);
        d.instances[static_cast<std::size_t>(d.selectedInstance)].at = add3(instanceAtSnapshot, delta);
        return;
    }

    if (brushSnapshot.empty() || d.selectedBrush < 0) {
        return;
    }
    const slopengine::Brush& src = brushSnapshot[0];
    if (d.scope == SelectionScope::Face && d.selectedFace >= 0) {
        const slopengine::BrushFace& face =
            src.faces[static_cast<std::size_t>(d.selectedFace)];
        float distance = dot3(delta, face.normal);
        distance = snapToGrid(distance, editor.gridSize);

        if (src.box) {
            Vector3 mins = src.mins;
            Vector3 maxs = src.maxs;
            const Vector3 n = face.normal;
            if (std::fabs(n.x) > 0.9f) {
                if (n.x > 0.0f) {
                    maxs.x = src.maxs.x + distance;
                } else {
                    mins.x = src.mins.x - distance;
                }
            } else if (std::fabs(n.y) > 0.9f) {
                if (n.y > 0.0f) {
                    maxs.y = src.maxs.y + distance;
                } else {
                    mins.y = src.mins.y - distance;
                }
            } else if (std::fabs(n.z) > 0.9f) {
                if (n.z > 0.0f) {
                    maxs.z = src.maxs.z + distance;
                } else {
                    mins.z = src.mins.z - distance;
                }
            }
            if (mins.x < maxs.x && mins.y < maxs.y && mins.z < maxs.z) {
                d.brushes[static_cast<std::size_t>(d.selectedBrush)] =
                    makeBoxAt(src, mins, maxs, assets);
            }
        } else {
            d.brushes[static_cast<std::size_t>(d.selectedBrush)] =
                pushFace(src, d.selectedFace, distance, assets);
        }
        return;
    }

    delta = snapToGrid(delta, editor.gridSize);
    d.brushes[static_cast<std::size_t>(d.selectedBrush)] = translateBrush(src, delta, assets);
}

void SelectTool::confirmTranslate(Editor& editor, slopengine::AssetStore& assets) {
    if (!translating) {
        return;
    }
    const bool wasInstance = editor.doc().selection == SelectionTarget::Instance;
    translating = false;
    axisLock = TranslateAxis::None;
    numericActive = false;
    editor.numericBuffer.clear();
    brushSnapshot.clear();
    editor.markDirty();
    if (wasInstance) {
        editor.rebuildPreview(assets);
    }
    editor.statusMessage = "Translate confirmed";
}

void SelectTool::cancelTranslate(Editor& editor) {
    if (!translating) {
        return;
    }
    EditorDocument& d = editor.doc();
    if (d.selection == SelectionTarget::Placement && d.selectedPlacement >= 0 &&
        d.selectedPlacement < static_cast<int>(d.placements.size())) {
        d.placements[static_cast<std::size_t>(d.selectedPlacement)].at = placementAtSnapshot;
        d.placements[static_cast<std::size_t>(d.selectedPlacement)].haveAt = true;
    } else if (d.selection == SelectionTarget::Instance && d.selectedInstance >= 0 &&
        d.selectedInstance < static_cast<int>(d.instances.size())) {
        d.instances[static_cast<std::size_t>(d.selectedInstance)].at = instanceAtSnapshot;
    } else if (!brushSnapshot.empty() && d.selectedBrush >= 0 &&
               d.selectedBrush < static_cast<int>(d.brushes.size())) {
        d.brushes[static_cast<std::size_t>(d.selectedBrush)] = brushSnapshot[0];
    }
    translating = false;
    axisLock = TranslateAxis::None;
    numericActive = false;
    editor.numericBuffer.clear();
    brushSnapshot.clear();
    editor.statusMessage = "Translate cancelled";
}

void SelectTool::handleNumeric(
    Editor& editor,
    slopengine::AssetStore& assets,
    bool uiWantsKeyboard) {
    if (uiWantsKeyboard || !translating) {
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
            numericActive = true;
        }
    }
    if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) {
        if (editor.numericBuffer.empty()) {
            editor.numericBuffer.push_back('-');
            numericActive = true;
        }
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !editor.numericBuffer.empty()) {
        editor.numericBuffer.pop_back();
        numericActive = !editor.numericBuffer.empty();
    }

    if (!numericActive || editor.numericBuffer.empty() || editor.numericBuffer == "-" ||
        editor.numericBuffer == "." || editor.numericBuffer == "-.") {
        return;
    }

    char* end = nullptr;
    const float value = std::strtof(editor.numericBuffer.c_str(), &end);
    if (end == editor.numericBuffer.c_str()) {
        return;
    }

    EditorDocument& d = editor.doc();
    Vector3 delta{};
    if (d.selection == SelectionTarget::Brush && d.scope == SelectionScope::Face &&
        d.selectedFace >= 0 && !brushSnapshot.empty()) {
        const auto& face = brushSnapshot[0].faces[static_cast<std::size_t>(d.selectedFace)];
        delta = scale3(face.normal, value);
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
    if (d.selection != SelectionTarget::Brush || d.selectedBrush < 0 ||
        d.selectedBrush >= static_cast<int>(d.brushes.size())) {
        return;
    }
    slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(d.selectedBrush)];
    if (brush.faces.empty()) {
        return;
    }

    if (d.scope == SelectionScope::Face && d.selectedFace >= 0 &&
        d.selectedFace < static_cast<int>(brush.faces.size())) {
        slopengine::BrushFace& face = brush.faces[static_cast<std::size_t>(d.selectedFace)];
        face.uvLock = !face.uvLock;
        if (face.uvLock) {
            face.uvUAxis = {};
            face.uvVAxis = {};
            slopengine::ensureFaceUvAxes(face);
        }
        editor.markDirty();
        editor.statusMessage =
            face.uvLock ? "UV lock on " + face.id : "UV lock off " + face.id;
        return;
    }

    bool allLocked = true;
    for (const slopengine::BrushFace& face : brush.faces) {
        if (!face.uvLock) {
            allLocked = false;
            break;
        }
    }
    const bool next = !allLocked;
    for (slopengine::BrushFace& face : brush.faces) {
        face.uvLock = next;
        if (next) {
            face.uvUAxis = {};
            face.uvVAxis = {};
            slopengine::ensureFaceUvAxes(face);
        }
    }
    editor.markDirty();
    editor.statusMessage = next ? "UV lock on " + brush.id : "UV lock off " + brush.id;
}

void SelectTool::pick(Editor& editor, const Camera3D& camera) {
    EditorDocument& d = editor.doc();
    const Ray ray = mouseRay(camera, editor.contentViewport);
    float bestT = std::numeric_limits<float>::max();
    int bestBrush = -1;
    int bestFace = -1;
    int bestInstance = -1;
    int bestPlacement = -1;

    for (std::size_t i = 0; i < d.brushes.size(); ++i) {
        float faceT = 0.0f;
        const auto face = rayBrushFaceIndex(ray, d.brushes[i], &faceT);
        if (face && faceT < bestT) {
            bestT = faceT;
            bestBrush = static_cast<int>(i);
            bestFace = *face;
            bestInstance = -1;
            bestPlacement = -1;
            continue;
        }
        const auto hit = rayBrushHitDistance(ray, d.brushes[i]);
        if (hit && *hit < bestT) {
            bestT = *hit;
            bestBrush = static_cast<int>(i);
            bestFace = -1;
            bestInstance = -1;
            bestPlacement = -1;
        }
    }

    for (std::size_t i = 0; i < editor.expandedInstanceBrushes.size(); ++i) {
        const auto hit = rayBrushHitDistance(ray, editor.expandedInstanceBrushes[i]);
        if (hit && *hit < bestT) {
            bestT = *hit;
            bestBrush = -1;
            bestFace = -1;
            bestInstance = editor.expandedInstanceOwners[i];
            bestPlacement = -1;
        }
    }

    float placementT = 0.0f;
    const auto placementHit = pickPlacement(d.placements, ray, &placementT);
    if (placementHit && placementT < bestT) {
        bestT = placementT;
        bestBrush = -1;
        bestFace = -1;
        bestInstance = -1;
        bestPlacement = *placementHit;
    }

    if (bestPlacement >= 0) {
        d.selection = SelectionTarget::Placement;
        d.selectedPlacement = bestPlacement;
        d.selectedBrush = -1;
        d.selectedFace = -1;
        d.selectedInstance = -1;
        editor.statusMessage =
            "Selected " + d.placements[static_cast<std::size_t>(bestPlacement)].id;
    } else if (bestInstance >= 0) {
        d.selection = SelectionTarget::Instance;
        d.selectedInstance = bestInstance;
        d.selectedBrush = -1;
        d.selectedFace = -1;
        d.selectedPlacement = -1;
        editor.statusMessage =
            "Selected instance " + d.instances[static_cast<std::size_t>(bestInstance)].id;
    } else if (bestBrush >= 0) {
        d.selection = SelectionTarget::Brush;
        d.selectedBrush = bestBrush;
        d.selectedFace = bestFace;
        d.selectedInstance = -1;
        d.selectedPlacement = -1;
        editor.statusMessage = "Selected " + d.brushes[static_cast<std::size_t>(bestBrush)].id;
    } else {
        editor.clearSelection();
        editor.statusMessage = "Selection cleared";
    }
}

void SelectTool::deleteSelected(Editor& editor, slopengine::AssetStore& assets) {
    EditorDocument& d = editor.doc();
    if (d.selection == SelectionTarget::Placement) {
        if (d.selectedPlacement < 0 ||
            d.selectedPlacement >= static_cast<int>(d.placements.size())) {
            return;
        }
        const std::string id = d.placements[static_cast<std::size_t>(d.selectedPlacement)].id;
        d.placements.erase(d.placements.begin() + d.selectedPlacement);
        editor.clearSelection();
        editor.markDirty();
        editor.statusMessage = "Deleted " + id;
        return;
    }
    if (d.selection == SelectionTarget::Instance) {
        if (d.selectedInstance < 0 || d.selectedInstance >= static_cast<int>(d.instances.size())) {
            return;
        }
        const std::string id = d.instances[static_cast<std::size_t>(d.selectedInstance)].id;
        d.instances.erase(d.instances.begin() + d.selectedInstance);
        editor.clearSelection();
        editor.markDirty();
        editor.rebuildPreview(assets);
        editor.statusMessage = "Deleted instance " + id;
        return;
    }
    if (d.selection != SelectionTarget::Brush || d.selectedBrush < 0 ||
        d.selectedBrush >= static_cast<int>(d.brushes.size())) {
        return;
    }
    const std::string id = d.brushes[static_cast<std::size_t>(d.selectedBrush)].id;
    d.brushes.erase(d.brushes.begin() + d.selectedBrush);
    editor.clearSelection();
    editor.markDirty();
    editor.statusMessage = "Deleted " + id;
}

void SelectTool::duplicateSelected(Editor& editor, const Camera3D& camera) {
    EditorDocument& d = editor.doc();
    if (d.selection == SelectionTarget::Placement) {
        if (d.selectedPlacement < 0 ||
            d.selectedPlacement >= static_cast<int>(d.placements.size())) {
            return;
        }
        slopengine::Placement copy = d.placements[static_cast<std::size_t>(d.selectedPlacement)];
        copy.id = editor.allocatePlacementId(slopengine::placementKindName(copy.kind));
        d.placements.push_back(std::move(copy));
        d.selection = SelectionTarget::Placement;
        d.selectedPlacement = static_cast<int>(d.placements.size()) - 1;
        d.selectedBrush = -1;
        d.selectedFace = -1;
        d.selectedInstance = -1;
        editor.markDirty();
        beginTranslate(editor, camera);
        return;
    }
    if (d.selection == SelectionTarget::Instance) {
        if (d.selectedInstance < 0 || d.selectedInstance >= static_cast<int>(d.instances.size())) {
            return;
        }
        slopengine::PrefabInstance copy = d.instances[static_cast<std::size_t>(d.selectedInstance)];
        copy.id = editor.allocatePrefabId();
        d.instances.push_back(std::move(copy));
        d.selectedInstance = static_cast<int>(d.instances.size()) - 1;
        d.selectedBrush = -1;
        d.selectedFace = -1;
        d.selectedPlacement = -1;
        editor.markDirty();
        beginTranslate(editor, camera);
        return;
    }
    if (d.selection != SelectionTarget::Brush || d.selectedBrush < 0 ||
        d.selectedBrush >= static_cast<int>(d.brushes.size())) {
        return;
    }
    slopengine::Brush copy = d.brushes[static_cast<std::size_t>(d.selectedBrush)];
    copy.id = editor.allocateBrushId();
    if (copy.box) {
        for (slopengine::BrushFace& face : copy.faces) {
            const auto slash = face.id.rfind('/');
            if (slash != std::string::npos) {
                face.id = copy.id + face.id.substr(slash);
            }
        }
    }
    d.brushes.push_back(std::move(copy));
    d.selection = SelectionTarget::Brush;
    d.selectedBrush = static_cast<int>(d.brushes.size()) - 1;
    d.selectedFace = -1;
    d.selectedInstance = -1;
    d.selectedPlacement = -1;
    d.scope = SelectionScope::Brush;
    editor.markDirty();
    beginTranslate(editor, camera);
}

void SelectTool::rotateSelected(Editor& editor, slopengine::AssetStore& assets) {
    EditorDocument& d = editor.doc();
    constexpr float kQuarter = 1.5707963267948966f;
    if (d.selection == SelectionTarget::Placement) {
        if (d.selectedPlacement < 0 ||
            d.selectedPlacement >= static_cast<int>(d.placements.size())) {
            return;
        }
        slopengine::Placement& placement =
            d.placements[static_cast<std::size_t>(d.selectedPlacement)];
        placement.yaw += kQuarter;
        if (placement.yaw >= 6.283185307179586f) {
            placement.yaw -= 6.283185307179586f;
        }
        if (placement.haveAngles) {
            placement.angles.y = placement.yaw;
        }
        editor.markDirty();
        editor.statusMessage = "Rotated " + placement.id;
        return;
    }
    if (d.selection != SelectionTarget::Instance || d.selectedInstance < 0 ||
        d.selectedInstance >= static_cast<int>(d.instances.size())) {
        return;
    }
    slopengine::PrefabInstance& instance = d.instances[static_cast<std::size_t>(d.selectedInstance)];
    instance.angles.y += kQuarter;
    if (instance.angles.y >= 6.283185307179586f) {
        instance.angles.y -= 6.283185307179586f;
    }
    editor.markDirty();
    editor.rebuildPreview(assets);
    editor.statusMessage = "Rotated " + instance.id;
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
        return;
    }

    if (translating) {
        handleNumeric(editor, assets, uiWantsKeyboard);

        if (!uiWantsKeyboard) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                cancelTranslate(editor);
                return;
            }
            if (IsKeyPressed(KEY_ENTER)) {
                confirmTranslate(editor, assets);
                return;
            }
            const TranslateAxis previousLock = axisLock;
            if (IsKeyPressed(KEY_X)) {
                axisLock = TranslateAxis::X;
                numericActive = false;
                editor.numericBuffer.clear();
            }
            if (IsKeyPressed(KEY_Y)) {
                axisLock = TranslateAxis::Y;
                numericActive = false;
                editor.numericBuffer.clear();
            }
            if (IsKeyPressed(KEY_Z)) {
                axisLock = TranslateAxis::Z;
                numericActive = false;
                editor.numericBuffer.clear();
            }
            if (axisLock != previousLock) {
                refreshTranslateGrab(*this, editor, camera);
            }
        }

        if (!uiWantsMouse && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            confirmTranslate(editor, assets);
            return;
        }

        const bool canDrag =
            !numericActive &&
            (editor.doc().selection == SelectionTarget::Instance ||
             editor.doc().selection == SelectionTarget::Placement || !brushSnapshot.empty());
        if (canDrag) {
            const Vector3 planeNormal =
                translateDragPlaneNormal(editor.viewPlane, axisLock, camera);
            Vector3 hit{};
            if (rayPlaneIntersection(
                    mouseRay(camera, editor.contentViewport),
                    translateOrigin,
                    planeNormal,
                    hit)) {
                Vector3 delta = sub3(hit, mouseGrabWorld);
                if (axisLock == TranslateAxis::X) {
                    delta = {delta.x, 0.0f, 0.0f};
                } else if (axisLock == TranslateAxis::Y) {
                    delta = {0.0f, delta.y, 0.0f};
                } else if (axisLock == TranslateAxis::Z) {
                    delta = {0.0f, 0.0f, delta.z};
                } else if (editor.viewPlane == ViewPlane::Top ||
                           editor.viewPlane == ViewPlane::PerspectiveY0) {
                    delta.y = 0.0f;
                } else if (editor.viewPlane == ViewPlane::Front) {
                    delta.z = 0.0f;
                } else if (editor.viewPlane == ViewPlane::Side) {
                    delta.x = 0.0f;
                }
                applyTranslate(editor, assets, delta);
            }
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
        rotateSelected(editor, assets);
        return;
    }
    if (IsKeyPressed(KEY_F) && editor.doc().selection == SelectionTarget::Brush) {
        editor.doc().scope =
            editor.doc().scope == SelectionScope::Brush ? SelectionScope::Face : SelectionScope::Brush;
        editor.statusMessage =
            editor.doc().scope == SelectionScope::Face ? "Selection scope: Face" : "Selection scope: Brush";
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
