#include "select_tool.hpp"

#include "map/brush.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace slopmap {

namespace {

float dot3(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 sub3(Vector3 a, Vector3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vector3 add3(Vector3 a, Vector3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 scale3(Vector3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
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

slopengine::Brush makeBoxAt(const slopengine::Brush& src, Vector3 mins, Vector3 maxs) {
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
                if (face.nodraw || face.uvShiftPixels.x != 0.0f || face.uvShiftPixels.y != 0.0f ||
                    face.material != material || face.id != src.id + suffix) {
                    slopengine::BrushFace overrideFace;
                    overrideFace.id = face.id;
                    overrideFace.material = face.material;
                    overrideFace.uvShiftPixels = face.uvShiftPixels;
                    overrideFace.nodraw = face.nodraw;
                    overrides.emplace_back(side, overrideFace);
                }
                break;
            }
        }
    }
    slopengine::Brush brush =
        slopengine::makeBrushBox(src.id, mins, maxs, material, overrides, src.role);
    brush.nocollide = src.nocollide;
    return brush;
}

slopengine::Brush translateBrush(const slopengine::Brush& src, Vector3 delta) {
    if (src.box) {
        return makeBoxAt(src, add3(src.mins, delta), add3(src.maxs, delta));
    }

    slopengine::Brush brush = src;
    for (slopengine::BrushFace& face : brush.faces) {
        for (Vector3& v : face.vertices) {
            v = add3(v, delta);
        }
        face.normal = slopengine::faceNormalFromVertices(face.vertices);
    }
    slopengine::recomputeBrushBounds(brush);
    return brush;
}

slopengine::Brush pushFace(const slopengine::Brush& src, int faceIndex, float distance) {
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
        return makeBoxAt(src, mins, maxs);
    }

    slopengine::Brush brush = src;
    slopengine::BrushFace& face = brush.faces[static_cast<std::size_t>(faceIndex)];
    const Vector3 n = face.normal;
    for (Vector3& v : face.vertices) {
        v = add3(v, scale3(n, distance));
    }
    face.normal = slopengine::faceNormalFromVertices(face.vertices);
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
    if (editor.doc.selectedBrush < 0 ||
        editor.doc.selectedBrush >= static_cast<int>(editor.doc.brushes.size())) {
        return;
    }
    translating = true;
    axisLock = TranslateAxis::None;
    numericActive = false;
    editor.numericBuffer.clear();
    brushSnapshot.clear();
    brushSnapshot.push_back(editor.doc.brushes[static_cast<std::size_t>(editor.doc.selectedBrush)]);
    translateOrigin = editor.selectionCenter();

    const ConstructionPlane movePlane = constructionPlaneForView(
        editor.viewPlane == ViewPlane::PerspectiveY0 ? ViewPlane::Top : editor.viewPlane);
    Vector3 hit{};
    if (rayPlaneIntersection(
            mouseRay(camera, editor.contentViewport),
            translateOrigin,
            movePlane.normal,
            hit)) {
        mouseGrabWorld = hit;
    } else {
        mouseGrabWorld = translateOrigin;
    }
    editor.statusMessage = "Translate (G): move, X/Y/Z lock, type units, Enter confirm, Esc cancel";
}

void SelectTool::applyTranslate(Editor& editor, Vector3 delta) {
    if (brushSnapshot.empty() || editor.doc.selectedBrush < 0) {
        return;
    }
    const slopengine::Brush& src = brushSnapshot[0];
    if (editor.doc.scope == SelectionScope::Face && editor.doc.selectedFace >= 0) {
        const slopengine::BrushFace& face =
            src.faces[static_cast<std::size_t>(editor.doc.selectedFace)];
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
                editor.doc.brushes[static_cast<std::size_t>(editor.doc.selectedBrush)] =
                    makeBoxAt(src, mins, maxs);
            }
        } else {
            editor.doc.brushes[static_cast<std::size_t>(editor.doc.selectedBrush)] =
                pushFace(src, editor.doc.selectedFace, distance);
        }
        return;
    }

    delta = snapToGrid(delta, editor.gridSize);
    editor.doc.brushes[static_cast<std::size_t>(editor.doc.selectedBrush)] = translateBrush(src, delta);
}

void SelectTool::confirmTranslate(Editor& editor) {
    if (!translating) {
        return;
    }
    translating = false;
    axisLock = TranslateAxis::None;
    numericActive = false;
    editor.numericBuffer.clear();
    brushSnapshot.clear();
    editor.markDirty();
    editor.statusMessage = "Translate confirmed";
}

void SelectTool::cancelTranslate(Editor& editor) {
    if (!translating || brushSnapshot.empty() || editor.doc.selectedBrush < 0) {
        translating = false;
        return;
    }
    editor.doc.brushes[static_cast<std::size_t>(editor.doc.selectedBrush)] = brushSnapshot[0];
    translating = false;
    axisLock = TranslateAxis::None;
    numericActive = false;
    editor.numericBuffer.clear();
    brushSnapshot.clear();
    editor.statusMessage = "Translate cancelled";
}

void SelectTool::handleNumeric(Editor& editor, bool uiWantsKeyboard) {
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

    Vector3 delta{};
    if (editor.doc.scope == SelectionScope::Face && editor.doc.selectedFace >= 0 && !brushSnapshot.empty()) {
        const auto& face =
            brushSnapshot[0].faces[static_cast<std::size_t>(editor.doc.selectedFace)];
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
    applyTranslate(editor, delta);
}

void SelectTool::pick(Editor& editor, const Camera3D& camera) {
    const Ray ray = mouseRay(camera, editor.contentViewport);
    float bestT = std::numeric_limits<float>::max();
    int bestBrush = -1;
    int bestFace = -1;
    for (std::size_t i = 0; i < editor.doc.brushes.size(); ++i) {
        float faceT = 0.0f;
        const auto face = rayBrushFaceIndex(ray, editor.doc.brushes[i], &faceT);
        if (face && faceT < bestT) {
            bestT = faceT;
            bestBrush = static_cast<int>(i);
            bestFace = *face;
            continue;
        }
        const auto hit = rayBrushHitDistance(ray, editor.doc.brushes[i]);
        if (hit && *hit < bestT) {
            bestT = *hit;
            bestBrush = static_cast<int>(i);
            bestFace = -1;
        }
    }

    editor.doc.selectedBrush = bestBrush;
    editor.doc.selectedFace = bestFace;
    if (bestBrush >= 0) {
        editor.statusMessage = "Selected " + editor.doc.brushes[static_cast<std::size_t>(bestBrush)].id;
    } else {
        editor.statusMessage = "Selection cleared";
    }
}

void SelectTool::deleteSelected(Editor& editor) {
    if (editor.doc.selectedBrush < 0 ||
        editor.doc.selectedBrush >= static_cast<int>(editor.doc.brushes.size())) {
        return;
    }
    const std::string id = editor.doc.brushes[static_cast<std::size_t>(editor.doc.selectedBrush)].id;
    editor.doc.brushes.erase(editor.doc.brushes.begin() + editor.doc.selectedBrush);
    editor.doc.selectedBrush = -1;
    editor.doc.selectedFace = -1;
    editor.markDirty();
    editor.statusMessage = "Deleted " + id;
}

void SelectTool::duplicateSelected(Editor& editor, const Camera3D& camera) {
    if (editor.doc.selectedBrush < 0 ||
        editor.doc.selectedBrush >= static_cast<int>(editor.doc.brushes.size())) {
        return;
    }
    slopengine::Brush copy = editor.doc.brushes[static_cast<std::size_t>(editor.doc.selectedBrush)];
    copy.id = editor.allocateBrushId();
    if (copy.box) {
        for (slopengine::BrushFace& face : copy.faces) {
            const auto slash = face.id.rfind('/');
            if (slash != std::string::npos) {
                face.id = copy.id + face.id.substr(slash);
            }
        }
    }
    editor.doc.brushes.push_back(std::move(copy));
    editor.doc.selectedBrush = static_cast<int>(editor.doc.brushes.size()) - 1;
    editor.doc.selectedFace = -1;
    editor.doc.scope = SelectionScope::Brush;
    editor.markDirty();
    beginTranslate(editor, camera);
}

void SelectTool::update(Editor& editor, const Camera3D& camera, bool uiWantsMouse, bool uiWantsKeyboard) {
    if (editor.mode != EditorMode::Select) {
        if (translating) {
            cancelTranslate(editor);
        }
        return;
    }

    if (translating) {
        handleNumeric(editor, uiWantsKeyboard);

        if (!uiWantsKeyboard) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                cancelTranslate(editor);
                return;
            }
            if (IsKeyPressed(KEY_ENTER)) {
                confirmTranslate(editor);
                return;
            }
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
        }

        if (!uiWantsMouse && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            confirmTranslate(editor);
            return;
        }

        if (!numericActive && !brushSnapshot.empty()) {
            ConstructionPlane movePlane = constructionPlaneForView(
                editor.viewPlane == ViewPlane::PerspectiveY0 ? ViewPlane::Top : editor.viewPlane);
            Vector3 hit{};
            if (rayPlaneIntersection(
                    mouseRay(camera, editor.contentViewport),
                    translateOrigin,
                    movePlane.normal,
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
                applyTranslate(editor, delta);
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
    if (IsKeyPressed(KEY_F)) {
        editor.doc.scope =
            editor.doc.scope == SelectionScope::Brush ? SelectionScope::Face : SelectionScope::Brush;
        editor.statusMessage =
            editor.doc.scope == SelectionScope::Face ? "Selection scope: Face" : "Selection scope: Brush";
        return;
    }
    if (IsKeyPressed(KEY_DELETE)) {
        deleteSelected(editor);
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
