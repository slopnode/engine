#include "select_tool.hpp"

#include "assets/material_loader.hpp"
#include "map/brush.hpp"
#include "map/csg_compile.hpp"
#include "map/uv_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
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
    GridPlane gridPlane,
    TranslateAxis axisLock,
    const Camera3D& camera) {
    const Vector3 forward = cameraForward(camera);

    if (axisLock == TranslateAxis::Y) {
        return dragPlaneNormalForAxis({0.0f, 1.0f, 0.0f}, forward);
    }
    if (axisLock == TranslateAxis::X && view == ViewPlane::Side) {
        return dragPlaneNormalForAxis({1.0f, 0.0f, 0.0f}, forward);
    }
    if (axisLock == TranslateAxis::Z && view == ViewPlane::Front) {
        return dragPlaneNormalForAxis({0.0f, 0.0f, 1.0f}, forward);
    }
    return constructionPlaneForView(view, gridPlane).normal;
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

void refreshTranslateGrab(SelectTool& tool, Editor& editor, const Camera3D& camera) {
    const Vector3 planeNormal =
        translateDragPlaneNormal(editor.viewPlane, editor.gridPlane, tool.axisLock, camera);
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

const char* translateTargetName(const Editor& editor) {
    const EditorDocument& d = editor.doc();
    if (d.selectionMode == SelectionMode::Entity) {
        return "Entity";
    }
    if (d.selectionMode == SelectionMode::Face) {
        return "Face";
    }
    return "Brush";
}

const char* translateAxisName(const Editor& editor, TranslateAxis axisLock) {
    if (editor.doc().selectionMode == SelectionMode::Face) {
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

void updateTranslateStatus(Editor& editor, const SelectTool& tool) {
    std::string message = "Translate ";
    message += translateTargetName(editor);
    message += " | ";
    message += translateAxisName(editor, tool.axisLock);
    message += " | ";
    if (tool.numericActive && !editor.numericBuffer.empty()) {
        message += editor.numericBuffer;
    } else {
        message += "drag";
    }
    message += "  (Enter confirm, Esc cancel)";
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
                    face.id != src.id + suffix) {
                    slopengine::BrushFace overrideFace;
                    overrideFace.id = face.id;
                    overrideFace.material = face.material;
                    overrideFace.uvShiftPixels = face.uvShiftPixels;
                    overrideFace.uvScale = face.uvScale;
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

bool faceFacesRay(const slopengine::BrushFace& face, Ray ray, bool ignoreBackfaces) {
    if (!ignoreBackfaces) {
        return true;
    }
    return dot3(face.normal, ray.direction) < 0.0f;
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
    EditorDocument& d = editor.doc();
    brushSnapshot.clear();
    brushSnapshotIndices.clear();
    entityAtSnapshots.clear();
    entitySnapshotRefs.clear();
    faceTranslate = {};

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
        if (!d.activeFace.valid() ||
            d.activeFace.brush >= static_cast<int>(d.brushes.size())) {
            return;
        }
        faceTranslate = d.activeFace;
        translating = true;
        axisLock = TranslateAxis::None;
        numericActive = false;
        editor.numericBuffer.clear();
        brushSnapshotIndices.push_back(faceTranslate.brush);
        brushSnapshot.push_back(d.brushes[static_cast<std::size_t>(faceTranslate.brush)]);
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
        translateOrigin = editor.selectionCenter();
    } else {
        return;
    }

    const Vector3 planeNormal =
        translateDragPlaneNormal(editor.viewPlane, editor.gridPlane, axisLock, camera);
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
    updateTranslateStatus(editor, *this);
}

void SelectTool::applyTranslate(Editor& editor, slopengine::AssetStore& assets, Vector3 delta) {
    EditorDocument& d = editor.doc();
    if (d.selectionMode == SelectionMode::Entity) {
        delta = snapToGrid(delta, editor.gridSize);
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

    if (d.selectionMode == SelectionMode::Face && faceTranslate.valid()) {
        const slopengine::Brush& src = brushSnapshot[0];
        if (faceTranslate.face < 0 ||
            faceTranslate.face >= static_cast<int>(src.faces.size())) {
            return;
        }
        const slopengine::BrushFace& face =
            src.faces[static_cast<std::size_t>(faceTranslate.face)];
        float distance = snapToGrid(dot3(delta, face.normal), editor.gridSize);
        d.brushes[static_cast<std::size_t>(brushSnapshotIndices[0])] =
            pushFace(src, faceTranslate.face, distance, assets);
        return;
    }

    delta = snapToGrid(delta, editor.gridSize);
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
    const bool wasBrush =
        d.selectionMode == SelectionMode::Brush || d.selectionMode == SelectionMode::Face;
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
    numericActive = false;
    editor.numericBuffer.clear();
    brushSnapshot.clear();
    brushSnapshotIndices.clear();
    entityAtSnapshots.clear();
    entitySnapshotRefs.clear();
    faceTranslate = {};
    editor.markDirty();
    if (anyInstance) {
        editor.markBspDirty();
        editor.rebuildPreview(assets);
    } else if (wasBrush) {
        editor.markBrushCompileDirty(brushRole);
    } else if (wasEntity) {
        editor.markThingCompileDirty(thingKind);
    }
    editor.statusMessage = "Translate confirmed";
}

void SelectTool::cancelTranslate(Editor& editor) {
    if (!translating) {
        return;
    }
    EditorDocument& d = editor.doc();
    for (std::size_t i = 0; i < entitySnapshotRefs.size(); ++i) {
        const EntityRef& ref = entitySnapshotRefs[i];
        if (ref.kind == EntityRef::Kind::Thing &&
            ref.index >= 0 && ref.index < static_cast<int>(d.things.size())) {
            d.things[static_cast<std::size_t>(ref.index)].at = entityAtSnapshots[i];
            d.things[static_cast<std::size_t>(ref.index)].haveAt = true;
        } else if (
            ref.kind == EntityRef::Kind::Instance && ref.index >= 0 &&
            ref.index < static_cast<int>(d.instances.size())) {
            d.instances[static_cast<std::size_t>(ref.index)].at = entityAtSnapshots[i];
        }
    }
    for (std::size_t i = 0; i < brushSnapshot.size(); ++i) {
        const int index = brushSnapshotIndices[i];
        if (index >= 0 && index < static_cast<int>(d.brushes.size())) {
            d.brushes[static_cast<std::size_t>(index)] = brushSnapshot[i];
        }
    }
    translating = false;
    axisLock = TranslateAxis::None;
    numericActive = false;
    editor.numericBuffer.clear();
    brushSnapshot.clear();
    brushSnapshotIndices.clear();
    entityAtSnapshots.clear();
    entitySnapshotRefs.clear();
    faceTranslate = {};
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

    updateTranslateStatus(editor, *this);

    if (!numericActive || editor.numericBuffer.empty() || editor.numericBuffer == "-" ||
        editor.numericBuffer == "." || editor.numericBuffer == "-.") {
        return;
    }

    char* end = nullptr;
    const float value = std::strtof(editor.numericBuffer.c_str(), &end);
    if (end == editor.numericBuffer.c_str()) {
        return;
    }

    Vector3 delta{};
    if (editor.doc().selectionMode == SelectionMode::Face && faceTranslate.valid() &&
        !brushSnapshot.empty() &&
        faceTranslate.face < static_cast<int>(brushSnapshot[0].faces.size())) {
        const auto& face =
            brushSnapshot[0].faces[static_cast<std::size_t>(faceTranslate.face)];
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
    if (d.selectionMode == SelectionMode::Face) {
        if (d.selectedFaces.empty()) {
            return;
        }
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
            return;
        }
        editor.markDirty();
        editor.markVisDirty();
        editor.statusMessage = lastLock ? "UV lock on " + lastId : "UV lock off " + lastId;
        return;
    }

    if (d.selectionMode != SelectionMode::Brush || d.selectedBrushes.empty()) {
        return;
    }
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
    editor.markVisDirty();
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
        pickCycleEntities.clear();
        pickCycleIndex = 0;
    }

    if (d.selectionMode == SelectionMode::Brush) {
        struct BrushHit {
            int brush = -1;
            float t = 0.0f;
        };
        std::vector<BrushHit> hits;
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
    } else {
        brushIndices = d.selectedBrushes;
    }
    if (brushIndices.empty()) {
        return;
    }
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
    editor.statusMessage = "Deleted selection";
}

void SelectTool::duplicateSelected(Editor& editor, const Camera3D& camera) {
    EditorDocument& d = editor.doc();
    if (d.selectionMode == SelectionMode::Entity) {
        if (d.selectedEntities.empty()) {
            return;
        }
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
    std::vector<int> created;
    slopengine::BrushRole role = slopengine::BrushRole::Hull;
    for (int index : d.selectedBrushes) {
        if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
            continue;
        }
        slopengine::Brush copy = d.brushes[static_cast<std::size_t>(index)];
        copy.id = editor.allocateBrushId();
        if (copy.box) {
            for (slopengine::BrushFace& face : copy.faces) {
                const auto slash = face.id.rfind('/');
                if (slash != std::string::npos) {
                    face.id = copy.id + face.id.substr(slash);
                }
            }
        }
        role = copy.role;
        d.brushes.push_back(std::move(copy));
        created.push_back(static_cast<int>(d.brushes.size()) - 1);
    }
    if (created.empty()) {
        return;
    }
    editor.selectBrushes(created, created.back());
    editor.markDirty();
    editor.markBrushCompileDirty(role);
    beginTranslate(editor, camera);
}

void SelectTool::rotateSelected(Editor& editor, slopengine::AssetStore& assets) {
    EditorDocument& d = editor.doc();
    if (d.selectionMode != SelectionMode::Entity || d.selectedEntities.empty()) {
        return;
    }
    constexpr float kQuarter = 1.5707963267948966f;
    bool anyInstance = false;
    std::string lastId;
    for (const EntityRef& ref : d.selectedEntities) {
        if (ref.kind == EntityRef::Kind::Thing &&
            ref.index >= 0 && ref.index < static_cast<int>(d.things.size())) {
            slopengine::Thing& thing = d.things[static_cast<std::size_t>(ref.index)];
            thing.yaw += kQuarter;
            if (thing.yaw >= 6.283185307179586f) {
                thing.yaw -= 6.283185307179586f;
            }
            if (thing.haveAngles) {
                thing.angles.y = thing.yaw;
            }
            editor.markThingCompileDirty(thing.kind);
            lastId = thing.id;
        } else if (
            ref.kind == EntityRef::Kind::Instance && ref.index >= 0 &&
            ref.index < static_cast<int>(d.instances.size())) {
            slopengine::PrefabInstance& instance =
                d.instances[static_cast<std::size_t>(ref.index)];
            instance.angles.y += kQuarter;
            if (instance.angles.y >= 6.283185307179586f) {
                instance.angles.y -= 6.283185307179586f;
            }
            anyInstance = true;
            lastId = instance.id;
        }
    }
    editor.markDirty();
    if (anyInstance) {
        editor.markBspDirty();
        editor.rebuildPreview(assets);
    }
    editor.statusMessage = "Rotated " + lastId;
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
                updateTranslateStatus(editor, *this);
            }
        }

        if (!uiWantsMouse && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            confirmTranslate(editor, assets);
            return;
        }

        const bool canDrag =
            !numericActive && (!entitySnapshotRefs.empty() || !brushSnapshot.empty());
        if (canDrag) {
            const Vector3 planeNormal =
                translateDragPlaneNormal(editor.viewPlane, editor.gridPlane, axisLock, camera);
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
                updateTranslateStatus(editor, *this);
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
