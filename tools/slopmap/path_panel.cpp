#include "path_panel.hpp"

#include "ui/icon_ui.hpp"
#include "ui/slopui_widgets.hpp"
#include "map/camera_path.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace slopmap {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;

const char* kInterpLabels[] = {"Linear", "Catmull-Rom"};
const char* kOrientLabels[] = {"Knots", "Along tangent", "Look at point"};
const char* kEaseLabels[] = {"Linear", "Smooth", "Ease in", "Ease out"};
const char* kKnotEaseLabels[] = {"Path default", "Linear", "Smooth", "Ease in", "Ease out"};

void copyToBuf(char* buf, std::size_t bufSize, const std::string& value) {
    std::snprintf(buf, bufSize, "%s", value.c_str());
}

std::string uniquePathId(const EditorDocument& doc, const std::string& wanted, int self) {
    std::string base = wanted.empty() ? std::string("path") : wanted;
    std::string candidate = base;
    int serial = 2;
    for (;;) {
        bool clash = false;
        for (std::size_t i = 0; i < doc.graphs.paths.size(); ++i) {
            if (static_cast<int>(i) != self && doc.graphs.paths[i].id == candidate) {
                clash = true;
                break;
            }
        }
        if (!clash) {
            return candidate;
        }
        candidate = base + "-" + std::to_string(serial++);
    }
}

bool drawPathOptions(Editor& editor, slopengine::CameraPath& path) {
    bool changed = false;

    int interp = static_cast<int>(path.interp);
    if (ImGui::Combo("Interpolation", &interp, kInterpLabels, 2)) {
        editor.prepareEdit();
        path.interp = static_cast<slopengine::PathInterp>(interp);
        editor.markDirty();
        editor.endEdit();
        changed = true;
    }
    int orient = static_cast<int>(path.orient);
    if (ImGui::Combo("Orientation", &orient, kOrientLabels, 3)) {
        editor.prepareEdit();
        path.orient = static_cast<slopengine::PathOrient>(orient);
        editor.markDirty();
        editor.endEdit();
        changed = true;
    }
    if (path.orient == slopengine::PathOrient::LookAt) {
        float lookAt[3] = {path.lookAt.x, path.lookAt.y, path.lookAt.z};
        if (ImGui::DragFloat3("Look at", lookAt, 0.05f)) {
            editor.prepareEdit();
            path.lookAt = {lookAt[0], lookAt[1], lookAt[2]};
            editor.markDirty();
            editor.endEdit();
            changed = true;
        }
        if (ImGui::SmallButton("Look at selection center")) {
            editor.prepareEdit();
            path.lookAt = editor.selectionCenter();
            editor.markDirty();
            editor.endEdit();
            changed = true;
        }
    }
    bool closed = path.closed;
    if (ImGui::Checkbox("Closed loop", &closed)) {
        editor.prepareEdit();
        path.closed = closed;
        editor.markDirty();
        editor.endEdit();
        changed = true;
    }
    bool constantSpeed = path.constantSpeed;
    if (ImGui::Checkbox("Constant speed", &constantSpeed)) {
        editor.prepareEdit();
        path.constantSpeed = constantSpeed;
        editor.markDirty();
        editor.endEdit();
        changed = true;
    }
    float duration = path.duration;
    if (ImGui::DragFloat("Segment seconds", &duration, 0.05f, 0.05f, 600.0f, "%.2f")) {
        editor.prepareEdit();
        path.duration = std::max(0.05f, duration);
        editor.markDirty();
        editor.endEdit();
        changed = true;
    }
    float fov = path.fov;
    if (ImGui::DragFloat("FOV (0 = keep)", &fov, 0.5f, 0.0f, 170.0f, "%.1f")) {
        editor.prepareEdit();
        path.fov = std::clamp(fov, 0.0f, 170.0f);
        editor.markDirty();
        editor.endEdit();
        changed = true;
    }
    int ease = static_cast<int>(path.ease);
    if (ImGui::Combo("Easing", &ease, kEaseLabels, 4)) {
        editor.prepareEdit();
        path.ease = static_cast<slopengine::PathEase>(ease);
        editor.markDirty();
        editor.endEdit();
        changed = true;
    }
    ImGui::TextDisabled(
        "Total %.2f s, length %.2f m",
        static_cast<double>(slopengine::cameraPathDuration(path)),
        static_cast<double>(slopengine::cameraPathLength(path)));
    return changed;
}

bool drawKnotFields(Editor& editor, EntityRef ref) {
    EditorDocument& d = editor.doc();
    slopengine::PathKnot* knot = d.knotAt(ref);
    if (knot == nullptr) {
        return false;
    }
    bool changed = false;

    ImGui::TextDisabled("Position");
    float at[3] = {knot->at.x, knot->at.y, knot->at.z};
    if (ImGui::DragFloat3("##knotAt", at, 0.05f)) {
        editor.prepareEdit();
        knot = d.knotAt(ref);
        knot->at = {at[0], at[1], at[2]};
        editor.markDirty();
        editor.endEdit();
        changed = true;
    }
    ImGui::TextDisabled("Look (degrees)");
    float yawDeg = knot->yaw * kRadToDeg;
    if (ImGui::DragFloat("Yaw", &yawDeg, 0.5f, -360.0f, 360.0f, "%.1f")) {
        editor.prepareEdit();
        knot = d.knotAt(ref);
        knot->yaw = yawDeg * kDegToRad;
        editor.markDirty();
        editor.endEdit();
        changed = true;
    }
    float pitchDeg = knot->pitch * kRadToDeg;
    if (ImGui::DragFloat("Pitch", &pitchDeg, 0.5f, -89.0f, 89.0f, "%.1f")) {
        editor.prepareEdit();
        knot = d.knotAt(ref);
        knot->pitch = pitchDeg * kDegToRad;
        editor.markDirty();
        editor.endEdit();
        changed = true;
    }
    float fov = knot->fov;
    if (ImGui::DragFloat("FOV (0 = path)", &fov, 0.5f, 0.0f, 170.0f, "%.1f")) {
        editor.prepareEdit();
        knot = d.knotAt(ref);
        knot->fov = std::clamp(fov, 0.0f, 170.0f);
        editor.markDirty();
        editor.endEdit();
        changed = true;
    }
    float duration = knot->duration;
    if (ImGui::DragFloat("Seconds to next (0 = path)", &duration, 0.05f, 0.0f, 600.0f, "%.2f")) {
        editor.prepareEdit();
        knot = d.knotAt(ref);
        knot->duration = std::max(0.0f, duration);
        editor.markDirty();
        editor.endEdit();
        changed = true;
    }
    int ease = knot->haveEase ? static_cast<int>(knot->ease) + 1 : 0;
    if (ImGui::Combo("Easing##knot", &ease, kKnotEaseLabels, 5)) {
        editor.prepareEdit();
        knot = d.knotAt(ref);
        knot->haveEase = ease > 0;
        knot->ease = ease > 0 ? static_cast<slopengine::PathEase>(ease - 1) : slopengine::PathEase::Linear;
        editor.markDirty();
        editor.endEdit();
        changed = true;
    }

    ImGui::Separator();
    if (ImGui::Button("Set from camera")) {
        slopengine::PathKnot updated = *knot;
        if (editor.knotPoseFromCamera(updated)) {
            editor.prepareEdit();
            *d.knotAt(ref) = updated;
            editor.markDirty();
            editor.endEdit();
            editor.statusMessage = "Knot set from viewport camera";
            changed = true;
        } else {
            editor.statusMessage = "Switch to the perspective viewport to capture the camera";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("View from knot")) {
        editor.endPathPreview();
        editor.applyKnotPoseToCamera(*d.knotAt(ref));
        editor.statusMessage = "Viewport camera moved to knot";
    }
    return changed;
}

std::optional<float> knotStartTime(const slopengine::CameraPath& path, int knot) {
    const int segments = slopengine::cameraPathSegmentCount(path);
    if (segments == 0) {
        return std::nullopt;
    }
    float t = 0.0f;
    for (int i = 0; i < std::min(knot, segments); ++i) {
        t += slopengine::cameraPathSegmentDuration(path, i);
    }
    return t;
}

} // namespace

PathPanelResult PathPanel::drawKnotSection(Editor& editor, float bodyHeight) {
    PathPanelResult result{};
    if (!slopengine::slopuiBeginChild("##knotsection", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return result;
    }
    EditorDocument& d = editor.doc();
    const EntityRef ref = d.activeEntity;
    const slopengine::CameraPath* path = d.pathAt(ref.path);
    if (path == nullptr || d.knotAt(ref) == nullptr) {
        ImGui::TextDisabled("Select a camera path knot");
        ImGui::EndChild();
        return result;
    }
    ImGui::Text("%s, knot %d of %d", path->id.c_str(), ref.index + 1, static_cast<int>(path->knots.size()));
    int knotCount = 0;
    for (const EntityRef& sel : d.selectedEntities) {
        if (sel.kind == EntityRef::Kind::Knot) {
            ++knotCount;
        }
    }
    if (knotCount > 1) {
        ImGui::TextDisabled("%d knots selected; fields edit the active one", knotCount);
    }
    ImGui::Separator();
    result.changed = drawKnotFields(editor, ref);
    ImGui::EndChild();
    return result;
}

PathPanelResult PathPanel::drawSection(Editor& editor, slopengine::AssetStore& assets, float bodyHeight) {
    (void)assets;
    PathPanelResult result{};
    if (!slopengine::slopuiBeginChild("##pathsection", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return result;
    }
    EditorDocument& d = editor.doc();
    if (editor.scene != EditorScene::Level) {
        ImGui::TextDisabled("Camera paths are saved with the level, not prefabs");
        ImGui::EndChild();
        return result;
    }

    if (ImGui::Button("New path")) {
        editor.endPathPreview();
        editor.prepareEdit();
        slopengine::CameraPath path{};
        path.id = editor.allocatePathId();
        d.graphs.paths.push_back(std::move(path));
        d.activePath = static_cast<int>(d.graphs.paths.size()) - 1;
        editor.clearSelection();
        editor.markDirty();
        editor.endEdit();
        result.changed = true;
        editor.statusMessage = "Created " + d.graphs.paths.back().id;
    }
    ImGui::SameLine();
    const bool showPaths = editor.showCameraPaths;
    if (ImGui::Checkbox("Show in viewport", &editor.showCameraPaths) && !editor.showCameraPaths && showPaths) {
        editor.endPathPreview();
    }

    if (d.graphs.paths.empty()) {
        ImGui::TextDisabled("No camera paths. Create one, then place knots or add them from the camera.");
        ImGui::EndChild();
        return result;
    }

    if (d.pathAt(d.activePath) == nullptr) {
        d.activePath = 0;
    }
    std::string activeLabel = d.graphs.paths[static_cast<std::size_t>(d.activePath)].id;
    if (ImGui::BeginCombo("Path", activeLabel.c_str())) {
        for (std::size_t i = 0; i < d.graphs.paths.size(); ++i) {
            const bool selected = static_cast<int>(i) == d.activePath;
            if (ImGui::Selectable(d.graphs.paths[i].id.c_str(), selected)) {
                if (!selected) {
                    editor.endPathPreview();
                    d.activePath = static_cast<int>(i);
                    editor.clearSelection();
                }
            }
        }
        ImGui::EndCombo();
    }

    slopengine::CameraPath* path = d.pathAt(d.activePath);
    const int pathIndex = d.activePath;

    static char idBuf[128];
    static int idBufPath = -1;
    static std::string idBufSource;
    if (idBufPath != pathIndex || idBufSource != path->id) {
        copyToBuf(idBuf, sizeof(idBuf), path->id);
        idBufPath = pathIndex;
        idBufSource = path->id;
    }
    if (ImGui::InputText("Id", idBuf, sizeof(idBuf), ImGuiInputTextFlags_EnterReturnsTrue) ||
        (ImGui::IsItemDeactivatedAfterEdit() && std::strcmp(idBuf, path->id.c_str()) != 0)) {
        const std::string wanted = uniquePathId(d, idBuf, pathIndex);
        if (wanted != path->id) {
            editor.prepareEdit();
            path = d.pathAt(pathIndex);
            path->id = wanted;
            editor.markDirty();
            editor.endEdit();
            result.changed = true;
        }
        copyToBuf(idBuf, sizeof(idBuf), path->id);
        idBufSource = path->id;
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.2f, 0.2f, 1.0f));
    const bool deletePath = ImGui::SmallButton("Delete path");
    ImGui::PopStyleColor(2);
    if (deletePath) {
        editor.endPathPreview();
        editor.prepareEdit();
        d.graphs.paths.erase(d.graphs.paths.begin() + pathIndex);
        d.activePath = d.graphs.paths.empty() ? -1 : std::min(pathIndex, static_cast<int>(d.graphs.paths.size()) - 1);
        editor.clearSelection();
        editor.markDirty();
        editor.endEdit();
        result.changed = true;
        editor.statusMessage = "Deleted path";
        ImGui::EndChild();
        return result;
    }

    ImGui::Separator();
    if (drawPathOptions(editor, *path)) {
        result.changed = true;
        path = d.pathAt(pathIndex);
    }

    ImGui::Separator();
    ImGui::TextDisabled("Knots");
    const bool placing = editor.mode == EditorMode::Place && editor.placeTarget == PlaceTarget::PathKnot;
    if (ImGui::Button(placing ? "Stop placing" : "Place knots")) {
        if (placing) {
            editor.mode = EditorMode::Select;
            editor.statusMessage = "Stopped placing knots";
        } else {
            editor.endPathPreview();
            editor.placeTarget = PlaceTarget::PathKnot;
            editor.mode = EditorMode::Place;
            editor.statusMessage = "Place knot: click the grid or a brush face, Esc to finish";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Add from camera")) {
        slopengine::PathKnot knot{};
        if (editor.knotPoseFromCamera(knot)) {
            editor.endPathPreview();
            editor.knotPoseFromCamera(knot);
            editor.prepareEdit();
            path = d.pathAt(pathIndex);
            int insertAt = static_cast<int>(path->knots.size());
            if (d.activeEntity.kind == EntityRef::Kind::Knot && d.activeEntity.path == pathIndex &&
                d.knotAt(d.activeEntity) != nullptr) {
                insertAt = d.activeEntity.index + 1;
            }
            path->knots.insert(path->knots.begin() + insertAt, knot);
            editor.selectEntity(knotRef(pathIndex, insertAt), false);
            editor.markDirty();
            editor.endEdit();
            result.changed = true;
            editor.statusMessage = "Added knot from viewport camera";
        } else {
            editor.statusMessage = "Switch to the perspective viewport to capture the camera";
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(Shift-click list to multi-select)");

    const float listH = std::min(ImGui::GetFrameHeightWithSpacing() * 6.0f, std::max(ImGui::GetFrameHeightWithSpacing() * 2.0f, ImGui::GetContentRegionAvail().y * 0.35f));
    if (slopengine::slopuiBeginChild("##knotlist", ImVec2(0.0f, listH), ImGuiChildFlags_Borders)) {
        int moveFrom = -1;
        int moveTo = -1;
        int deleteKnot = -1;
        for (std::size_t k = 0; k < path->knots.size(); ++k) {
            ImGui::PushID(static_cast<int>(k));
            const EntityRef ref = knotRef(pathIndex, static_cast<int>(k));
            const bool selected = d.isEntitySelected(ref);
            const slopengine::PathKnot& knot = path->knots[k];
            char label[128];
            std::snprintf(
                label,
                sizeof(label),
                "%d  (%.2f, %.2f, %.2f)  yaw %.0f%s",
                static_cast<int>(k) + 1,
                static_cast<double>(knot.at.x),
                static_cast<double>(knot.at.y),
                static_cast<double>(knot.at.z),
                static_cast<double>(knot.yaw * kRadToDeg),
                knot.duration > 0.0f ? "  *" : "");
            const float rowW = ImGui::GetContentRegionAvail().x;
            const float btnW = ImGui::GetFrameHeight();
            if (ImGui::Selectable(label, selected, 0, ImVec2(std::max(0.0f, rowW - btnW * 3.0f - ImGui::GetStyle().ItemSpacing.x * 3.0f), 0.0f))) {
                editor.selectEntity(ref, ImGui::GetIO().KeyShift);
                if (editor.pathPreview.active) {
                    if (auto t = knotStartTime(*path, static_cast<int>(k))) {
                        editor.pathPreview.time = *t;
                        editor.pathPreview.playing = false;
                    }
                }
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                editor.endPathPreview();
                editor.applyKnotPoseToCamera(knot);
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(k == 0);
            if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
                moveFrom = static_cast<int>(k);
                moveTo = static_cast<int>(k) - 1;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(k + 1 >= path->knots.size());
            if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
                moveFrom = static_cast<int>(k);
                moveTo = static_cast<int>(k) + 1;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("x", ImVec2(btnW, 0.0f))) {
                deleteKnot = static_cast<int>(k);
            }
            ImGui::PopStyleColor(2);
            ImGui::PopID();
        }
        if (path->knots.empty()) {
            ImGui::TextDisabled("No knots yet");
        }
        if (moveFrom >= 0 && moveTo >= 0 && moveTo < static_cast<int>(path->knots.size())) {
            editor.prepareEdit();
            path = d.pathAt(pathIndex);
            std::swap(path->knots[static_cast<std::size_t>(moveFrom)], path->knots[static_cast<std::size_t>(moveTo)]);
            editor.selectEntity(knotRef(pathIndex, moveTo), false);
            editor.markDirty();
            editor.endEdit();
            result.changed = true;
        }
        if (deleteKnot >= 0) {
            editor.prepareEdit();
            path = d.pathAt(pathIndex);
            path->knots.erase(path->knots.begin() + deleteKnot);
            editor.clearSelection();
            d.selectionMode = SelectionMode::Entity;
            editor.markDirty();
            editor.endEdit();
            result.changed = true;
        }
    }
    ImGui::EndChild();

    if (d.activeEntity.kind == EntityRef::Kind::Knot && d.activeEntity.path == pathIndex &&
        d.knotAt(d.activeEntity) != nullptr) {
        ImGui::Separator();
        ImGui::TextDisabled("Knot %d", d.activeEntity.index + 1);
        if (drawKnotFields(editor, d.activeEntity)) {
            result.changed = true;
            path = d.pathAt(pathIndex);
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Preview");
    const float total = slopengine::cameraPathDuration(*path);
    const bool canPreview = slopengine::cameraPathSegmentCount(*path) > 0;
    ImGui::BeginDisabled(!canPreview);
    bool previewActive = editor.pathPreview.active;
    if (ImGui::Checkbox("Fly viewport along path", &previewActive)) {
        if (previewActive) {
            editor.beginPathPreview();
            editor.pathPreview.time = std::clamp(editor.pathPreview.time, 0.0f, total);
        } else {
            editor.endPathPreview();
        }
    }
    if (editor.pathPreview.active) {
        ImGui::SameLine();
        if (ImGui::Button(editor.pathPreview.playing ? "Pause" : "Play")) {
            editor.pathPreview.playing = !editor.pathPreview.playing;
        }
    }
    float time = std::clamp(editor.pathPreview.time, 0.0f, total);
    if (ImGui::SliderFloat("Time", &time, 0.0f, std::max(total, 0.01f), "%.2f s")) {
        editor.pathPreview.time = time;
        editor.pathPreview.playing = false;
        if (!editor.pathPreview.active) {
            editor.beginPathPreview();
        }
    }
    ImGui::EndDisabled();

    ImGui::EndChild();
    return result;
}

void updatePathPreview(Editor& editor) {
    if (!editor.pathPreview.active) {
        return;
    }
    EditorDocument& d = editor.doc();
    const slopengine::CameraPath* path = d.pathAt(d.activePath);
    if (path == nullptr || slopengine::cameraPathSegmentCount(*path) == 0 ||
        editor.scene != EditorScene::Level) {
        editor.endPathPreview();
        return;
    }
    const float total = slopengine::cameraPathDuration(*path);
    if (editor.pathPreview.playing) {
        editor.pathPreview.time += GetFrameTime();
        if (editor.pathPreview.time > total) {
            editor.pathPreview.time = std::fmod(editor.pathPreview.time, std::max(total, 1e-3f));
        }
    }
    editor.pathPreview.time = std::clamp(editor.pathPreview.time, 0.0f, total);
    editor.applyPoseToCamera(slopengine::sampleCameraPath(*path, editor.pathPreview.time));
}

}
