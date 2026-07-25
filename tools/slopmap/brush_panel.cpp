#include "brush_panel.hpp"

#include "map/brush.hpp"

#include "imgui.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace slopmap {

namespace {

constexpr slopengine::BrushRole kRoles[] = {
    slopengine::BrushRole::Hull,
    slopengine::BrushRole::Detail,
    slopengine::BrushRole::Hint,
    slopengine::BrushRole::Trigger,
    slopengine::BrushRole::Water,
    slopengine::BrushRole::Window,
};
constexpr int kRoleCount = static_cast<int>(sizeof(kRoles) / sizeof(kRoles[0]));

int roleIndex(slopengine::BrushRole role) {
    for (int i = 0; i < kRoleCount; ++i) {
        if (kRoles[i] == role) {
            return i;
        }
    }
    return 0;
}

bool nearlyEqual(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

bool vec3Equal(Vector3 a, Vector3 b) {
    return nearlyEqual(a.x, b.x) && nearlyEqual(a.y, b.y) && nearlyEqual(a.z, b.z);
}

std::vector<int> collectBrushTargets(const EditorDocument& doc) {
    std::vector<int> targets;
    if (doc.selectionMode != SelectionMode::Brush) {
        return targets;
    }
    for (int index : doc.selectedBrushes) {
        if (index < 0 || index >= static_cast<int>(doc.brushes.size())) {
            continue;
        }
        targets.push_back(index);
    }
    return targets;
}

struct FaceTarget {
    int brush = -1;
    int face = -1;
};

std::vector<FaceTarget> collectFaceTargets(const EditorDocument& doc) {
    std::vector<FaceTarget> targets;
    if (doc.selectionMode != SelectionMode::Face) {
        return targets;
    }
    for (const FaceRef& ref : doc.selectedFaces) {
        if (!ref.valid() || ref.brush < 0 || ref.brush >= static_cast<int>(doc.brushes.size())) {
            continue;
        }
        const slopengine::Brush& brush = doc.brushes[static_cast<std::size_t>(ref.brush)];
        if (ref.face < 0 || ref.face >= static_cast<int>(brush.faces.size())) {
            continue;
        }
        targets.push_back({ref.brush, ref.face});
    }
    return targets;
}

slopengine::Brush* brushAt(EditorDocument& doc, int index) {
    if (index < 0 || index >= static_cast<int>(doc.brushes.size())) {
        return nullptr;
    }
    return &doc.brushes[static_cast<std::size_t>(index)];
}

const slopengine::Brush* brushAt(const EditorDocument& doc, int index) {
    if (index < 0 || index >= static_cast<int>(doc.brushes.size())) {
        return nullptr;
    }
    return &doc.brushes[static_cast<std::size_t>(index)];
}

slopengine::BrushFace* faceAt(EditorDocument& doc, const FaceTarget& target) {
    slopengine::Brush* brush = brushAt(doc, target.brush);
    if (brush == nullptr || target.face < 0 ||
        target.face >= static_cast<int>(brush->faces.size())) {
        return nullptr;
    }
    return &brush->faces[static_cast<std::size_t>(target.face)];
}

const slopengine::BrushFace* faceAt(const EditorDocument& doc, const FaceTarget& target) {
    const slopengine::Brush* brush = brushAt(doc, target.brush);
    if (brush == nullptr || target.face < 0 ||
        target.face >= static_cast<int>(brush->faces.size())) {
        return nullptr;
    }
    return &brush->faces[static_cast<std::size_t>(target.face)];
}

template <typename T>
std::optional<T> commonValue(
    const EditorDocument& doc,
    const std::vector<int>& targets,
    const std::function<T(const slopengine::Brush&)>& getter,
    const std::function<bool(const T&, const T&)>& equal) {
    std::optional<T> common;
    for (int index : targets) {
        const slopengine::Brush* brush = brushAt(doc, index);
        if (brush == nullptr) {
            continue;
        }
        const T value = getter(*brush);
        if (!common.has_value()) {
            common = value;
        } else if (!equal(*common, value)) {
            return std::nullopt;
        }
    }
    return common;
}

template <typename T>
std::optional<T> commonValue(
    const EditorDocument& doc,
    const std::vector<int>& targets,
    const std::function<T(const slopengine::Brush&)>& getter) {
    return commonValue<T>(doc, targets, getter, [](const T& a, const T& b) { return a == b; });
}

template <typename T>
std::optional<T> commonFaceValue(
    const EditorDocument& doc,
    const std::vector<FaceTarget>& targets,
    const std::function<T(const slopengine::BrushFace&)>& getter) {
    std::optional<T> common;
    for (const FaceTarget& target : targets) {
        const slopengine::BrushFace* face = faceAt(doc, target);
        if (face == nullptr) {
            continue;
        }
        const T value = getter(*face);
        if (!common.has_value()) {
            common = value;
        } else if (*common != value) {
            return std::nullopt;
        }
    }
    return common;
}

bool forEachBrush(
    Editor& editor,
    const std::vector<int>& targets,
    const std::function<void(slopengine::Brush&, slopengine::BrushRole previous)>& fn) {
    EditorDocument& doc = editor.doc();
    int count = 0;
    for (int index : targets) {
        slopengine::Brush* brush = brushAt(doc, index);
        if (brush == nullptr) {
            continue;
        }
        const slopengine::BrushRole previous = brush->role;
        fn(*brush, previous);
        editor.markBrushCompileDirty(previous);
        editor.markBrushCompileDirty(brush->role);
        ++count;
    }
    if (count == 0) {
        return false;
    }
    editor.markDirty();
    return true;
}

bool forEachFace(
    Editor& editor,
    const std::vector<FaceTarget>& targets,
    const std::function<void(slopengine::BrushFace&)>& fn) {
    EditorDocument& doc = editor.doc();
    int count = 0;
    for (const FaceTarget& target : targets) {
        slopengine::BrushFace* face = faceAt(doc, target);
        if (face == nullptr) {
            continue;
        }
        fn(*face);
        ++count;
    }
    if (count == 0) {
        return false;
    }
    editor.markDirty();
    return true;
}

bool checkboxMixed(const char* label, bool* value, bool mixed) {
    if (mixed) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.65f);
    }
    const bool changed = ImGui::Checkbox(label, value);
    if (mixed) {
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("mixed values");
        }
    }
    return changed;
}

bool drawHandlerField(
    Editor& editor,
    const std::vector<FaceTarget>& targets,
    const char* label,
    const char* id,
    const std::optional<std::string>& common,
    const std::function<std::string&(slopengine::BrushFace&)>& field) {
    char buffer[256];
    if (common.has_value()) {
        std::snprintf(buffer, sizeof(buffer), "%s", common->c_str());
    } else {
        buffer[0] = '\0';
    }
    ImGui::PushID(id);
    if (!common.has_value()) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.65f);
    }
    const bool changed = ImGui::InputText(label, buffer, sizeof(buffer));
    if (!common.has_value()) {
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("mixed values");
        }
    }
    ImGui::PopID();
    if (!changed) {
        return false;
    }
    const std::string next(buffer);
    if (forEachFace(editor, targets, [&](slopengine::BrushFace& face) { field(face) = next; })) {
        editor.statusMessage = next.empty() ? std::string("Cleared ") + label
                                            : std::string("Set ") + label + ": " + next;
        return true;
    }
    return false;
}

BrushPanelResult drawFaceSection(Editor& editor, float bodyHeight) {
    BrushPanelResult result{};
    if (!ImGui::BeginChild("##brushsection", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return result;
    }

    const EditorDocument& doc = editor.doc();
    const std::vector<FaceTarget> targets = collectFaceTargets(doc);
    if (targets.empty()) {
        ImGui::TextDisabled("Select face(s) to edit");
        ImGui::EndChild();
        return result;
    }

    const int count = static_cast<int>(targets.size());
    if (count == 1) {
        const slopengine::BrushFace* face = faceAt(doc, targets.front());
        const slopengine::Brush* brush = brushAt(doc, targets.front().brush);
        if (face != nullptr && brush != nullptr) {
            ImGui::Text("Brush: %s", brush->id.c_str());
            ImGui::Text("Face: %s", face->id.empty() ? "(unnamed)" : face->id.c_str());
        }
    } else {
        ImGui::Text("%d faces selected", count);
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Use (package handler name)");
    ImGui::TextDisabled("Interact calls Scheme (handler face-id)");

    const auto onUseCommon = commonFaceValue<std::string>(
        doc, targets, [](const slopengine::BrushFace& f) { return f.onUse; });

    if (drawHandlerField(
            editor,
            targets,
            "On use",
            "onuse",
            onUseCommon,
            [](slopengine::BrushFace& f) -> std::string& { return f.onUse; })) {
        result.changed = true;
    }

    ImGui::EndChild();
    return result;
}

BrushPanelResult drawBrushSection(Editor& editor, float bodyHeight) {
    BrushPanelResult result{};
    if (!ImGui::BeginChild("##brushsection", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return result;
    }

    const EditorDocument& doc = editor.doc();
    const std::vector<int> targets = collectBrushTargets(doc);
    if (targets.empty()) {
        ImGui::TextDisabled("Select brush(es) to edit");
        ImGui::EndChild();
        return result;
    }

    const int count = static_cast<int>(targets.size());
    if (count == 1) {
        const slopengine::Brush* brush = brushAt(doc, targets.front());
        if (brush != nullptr) {
            ImGui::Text("Brush: %s", brush->id.c_str());
        }
    } else {
        ImGui::Text("%d brushes selected", count);
        if (doc.activeBrush >= 0 && doc.activeBrush < static_cast<int>(doc.brushes.size())) {
            const slopengine::Brush& active = doc.brushes[static_cast<std::size_t>(doc.activeBrush)];
            ImGui::Text("Active: %s", active.id.c_str());
        } else {
            ImGui::TextDisabled("Active: none");
        }
    }
    ImGui::Separator();

    const auto roleCommon = commonValue<slopengine::BrushRole>(
        doc,
        targets,
        [](const slopengine::Brush& b) { return b.role; });
    const auto nocollideCommon = commonValue<bool>(
        doc,
        targets,
        [](const slopengine::Brush& b) { return b.nocollide; });
    const auto boxCommon = commonValue<bool>(
        doc,
        targets,
        [](const slopengine::Brush& b) { return b.box; });
    const auto minsCommon = commonValue<Vector3>(
        doc,
        targets,
        [](const slopengine::Brush& b) { return b.mins; },
        vec3Equal);
    const auto maxsCommon = commonValue<Vector3>(
        doc,
        targets,
        [](const slopengine::Brush& b) { return b.maxs; },
        vec3Equal);

    int roleIdx = roleCommon.has_value() ? roleIndex(*roleCommon) : -1;
    const char* preview = roleCommon.has_value() ? slopengine::brushRoleName(*roleCommon) : "—";
    if (ImGui::BeginCombo("Role", preview)) {
        for (int i = 0; i < kRoleCount; ++i) {
            const bool selected = roleIdx == i;
            if (ImGui::Selectable(slopengine::brushRoleName(kRoles[i]), selected)) {
                const slopengine::BrushRole next = kRoles[i];
                if (forEachBrush(editor, targets, [next](slopengine::Brush& brush, slopengine::BrushRole) {
                        brush.role = next;
                        brush.nocollide = slopengine::brushRoleDefaultNocollide(next);
                    })) {
                    result.changed = true;
                    editor.statusMessage = std::string("Set brush role: ") + slopengine::brushRoleName(next);
                }
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (!roleCommon.has_value() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("mixed values");
    }

    bool nocollide = nocollideCommon.value_or(false);
    if (checkboxMixed("Nocollide", &nocollide, !nocollideCommon.has_value())) {
        if (forEachBrush(editor, targets, [nocollide](slopengine::Brush& brush, slopengine::BrushRole) {
                brush.nocollide = nocollide;
            })) {
            result.changed = true;
            editor.statusMessage = nocollide ? "Set brush nocollide" : "Cleared brush nocollide";
        }
    }

    ImGui::Separator();
    if (boxCommon.has_value()) {
        ImGui::Text("Shape: %s", *boxCommon ? "box" : "convex");
    } else {
        ImGui::TextDisabled("Shape: mixed");
    }

    if (minsCommon.has_value() && maxsCommon.has_value()) {
        const Vector3 size = {
            maxsCommon->x - minsCommon->x,
            maxsCommon->y - minsCommon->y,
            maxsCommon->z - minsCommon->z,
        };
        ImGui::Text("Size: %.2f × %.2f × %.2f", size.x, size.y, size.z);
    } else {
        ImGui::TextDisabled("Size: mixed");
    }

    if (count == 1) {
        const slopengine::Brush* brush = brushAt(doc, targets.front());
        if (brush != nullptr) {
            ImGui::Text("Faces: %d", static_cast<int>(brush->faces.size()));
        }
    }

    ImGui::EndChild();
    return result;
}

} // namespace

BrushPanelResult BrushPanel::drawSection(Editor& editor, float bodyHeight) {
    if (editor.doc().selectionMode == SelectionMode::Face) {
        return drawFaceSection(editor, bodyHeight);
    }
    return drawBrushSection(editor, bodyHeight);
}

}
