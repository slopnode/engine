#include "brush_panel.hpp"

#include "handler_ui.hpp"
#include "map/brush.hpp"
#include "map/handler_binding.hpp"
#include "map/map_handler_registry.hpp"
#include "map/thing.hpp"

#include "imgui.h"

#include <algorithm>
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
    slopengine::BrushRole::Door,
    slopengine::BrushRole::Hint,
    slopengine::BrushRole::Trigger,
    slopengine::BrushRole::Water,
    slopengine::BrushRole::Window,
    slopengine::BrushRole::Transparent,
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
    editor.prepareEdit();
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
        editor.abortEdit();
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
    editor.prepareEdit();
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
        editor.abortEdit();
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

    const auto onUseCommon = commonFaceValue<slopengine::HandlerBinding>(
        doc, targets, [](const slopengine::BrushFace& f) { return f.onUse; });
    const auto onTouchCommon = commonFaceValue<slopengine::HandlerBinding>(
        doc, targets, [](const slopengine::BrushFace& f) { return f.onTouch; });

    if (drawHandlerBindingEditor(
            editor,
            "On use",
            "onuse",
            slopengine::MapHandlerKind::Use,
            onUseCommon,
            [&](slopengine::HandlerBinding& next) {
                forEachFace(editor, targets, [&](slopengine::BrushFace& face) {
                    face.onUse = next;
                });
            })) {
        result.changed = true;
    }

    if (drawHandlerBindingEditor(
            editor,
            "On touch",
            "ontouch",
            slopengine::MapHandlerKind::Touch,
            onTouchCommon,
            [&](slopengine::HandlerBinding& next) {
                forEachFace(editor, targets, [&](slopengine::BrushFace& face) {
                    face.onTouch = next;
                });
            })) {
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
        const int brushIndex = targets.front();
        const slopengine::Brush* brush = brushAt(doc, brushIndex);
        if (brush != nullptr) {
            char idBuf[128]{};
            std::snprintf(idBuf, sizeof(idBuf), "%s", brush->id.c_str());
            ImGui::InputText("Id", idBuf, sizeof(idBuf));
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                if (editor.renameBrush(brushIndex, idBuf)) {
                    result.changed = true;
                }
            }
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
                if (forEachBrush(editor, targets, [next](slopengine::Brush& brush, slopengine::BrushRole previous) {
                        brush.role = next;
                        slopengine::setBrushBlocks(brush, slopengine::brushRoleDefaultBlocks(next));
                        if (next == slopengine::BrushRole::Door &&
                            previous != slopengine::BrushRole::Door) {
                            brush.door = slopengine::BrushDoor{};
                            brush.door.motion = slopengine::DoorMotion::Raise;
                            brush.door.haveDuration = true;
                            brush.door.duration = 0.6f;
                            brush.door.havePrompt = true;
                            brush.door.prompt = "Open";
                        }
                    })) {
                    result.changed = true;
                    editor.markFacDirty();
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
                if (nocollide) {
                    slopengine::setBrushBlocks(brush, 0);
                } else {
                    slopengine::setBrushBlocks(brush, slopengine::brushRoleDefaultBlocks(brush.role));
                }
            })) {
            result.changed = true;
            editor.statusMessage = nocollide ? "Set brush nocollide" : "Cleared brush nocollide";
        }
    }

    ImGui::TextUnformatted("Blocking");
    const auto blockLosCommon = commonValue<bool>(
        doc,
        targets,
        [](const slopengine::Brush& b) { return (b.blocks & slopengine::BrushBlock::Los) != 0; });
    const auto blockLinescanCommon = commonValue<bool>(
        doc,
        targets,
        [](const slopengine::Brush& b) { return (b.blocks & slopengine::BrushBlock::Linescan) != 0; });
    const auto blockProjectileCommon = commonValue<bool>(
        doc,
        targets,
        [](const slopengine::Brush& b) { return (b.blocks & slopengine::BrushBlock::Projectile) != 0; });
    const auto blockPlayerCommon = commonValue<bool>(
        doc,
        targets,
        [](const slopengine::Brush& b) { return (b.blocks & slopengine::BrushBlock::Player) != 0; });
    const auto blockActorCommon = commonValue<bool>(
        doc,
        targets,
        [](const slopengine::Brush& b) { return (b.blocks & slopengine::BrushBlock::Actor) != 0; });

    auto drawBlockToggle = [&](const char* label,
                               const std::optional<bool>& common,
                               std::uint8_t bit,
                               const char* statusOn,
                               const char* statusOff) {
        bool value = common.value_or(false);
        if (checkboxMixed(label, &value, !common.has_value())) {
            if (forEachBrush(editor, targets, [bit, value](slopengine::Brush& brush, slopengine::BrushRole) {
                    if (value) {
                        brush.blocks = static_cast<std::uint8_t>(brush.blocks | bit);
                    } else {
                        brush.blocks = static_cast<std::uint8_t>(brush.blocks & ~bit);
                    }
                    slopengine::syncBrushNocollide(brush);
                })) {
                result.changed = true;
                editor.statusMessage = value ? statusOn : statusOff;
            }
        }
    };

    drawBlockToggle(
        "Blocks LOS",
        blockLosCommon,
        slopengine::BrushBlock::Los,
        "Set brush block LOS",
        "Cleared brush block LOS");
    drawBlockToggle(
        "Blocks Linescans",
        blockLinescanCommon,
        slopengine::BrushBlock::Linescan,
        "Set brush block linescans",
        "Cleared brush block linescans");
    drawBlockToggle(
        "Blocks Projectiles",
        blockProjectileCommon,
        slopengine::BrushBlock::Projectile,
        "Set brush block projectiles",
        "Cleared brush block projectiles");
    drawBlockToggle(
        "Blocks Player",
        blockPlayerCommon,
        slopengine::BrushBlock::Player,
        "Set brush block player",
        "Cleared brush block player");
    drawBlockToggle(
        "Blocks Actor",
        blockActorCommon,
        slopengine::BrushBlock::Actor,
        "Set brush block actor",
        "Cleared brush block actor");

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

    const auto roleIsDoor = commonValue<bool>(
        doc,
        targets,
        [](const slopengine::Brush& b) { return b.role == slopengine::BrushRole::Door; });
    const bool showDoorProps = roleIsDoor.value_or(false) ||
        (!roleIsDoor.has_value() &&
         std::any_of(targets.begin(), targets.end(), [&](int index) {
             const slopengine::Brush* b = brushAt(doc, index);
             return b != nullptr && b->role == slopengine::BrushRole::Door;
         }));

    if (showDoorProps) {
        ImGui::Separator();
        ImGui::TextUnformatted("Door");

        constexpr slopengine::DoorMotion kMotions[] = {
            slopengine::DoorMotion::Raise,
            slopengine::DoorMotion::Slide,
            slopengine::DoorMotion::Swing,
        };
        const auto motionCommon = commonValue<slopengine::DoorMotion>(
            doc, targets, [](const slopengine::Brush& b) { return b.door.motion; });
        const char* motionPreview =
            motionCommon.has_value() ? slopengine::doorMotionName(*motionCommon) : "—";
        if (ImGui::BeginCombo("Motion", motionPreview)) {
            for (slopengine::DoorMotion motion : kMotions) {
                const bool selected =
                    motionCommon.has_value() && *motionCommon == motion;
                if (ImGui::Selectable(slopengine::doorMotionName(motion), selected)) {
                    if (forEachBrush(editor, targets, [motion](slopengine::Brush& brush, slopengine::BrushRole) {
                            if (brush.role != slopengine::BrushRole::Door) {
                                return;
                            }
                            brush.door.motion = motion;
                        })) {
                        result.changed = true;
                    }
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        const auto floatEq = [](const float& a, const float& b) { return nearlyEqual(a, b); };
        const auto durationCommon = commonValue<float>(
            doc,
            targets,
            [](const slopengine::Brush& b) { return b.door.duration; },
            floatEq);
        float duration = durationCommon.value_or(0.6f);
        if (ImGui::DragFloat("Duration", &duration, 0.05f, 0.05f, 30.0f, "%.2f s")) {
            if (forEachBrush(editor, targets, [duration](slopengine::Brush& brush, slopengine::BrushRole) {
                    if (brush.role != slopengine::BrushRole::Door) {
                        return;
                    }
                    brush.door.duration = duration;
                    brush.door.haveDuration = true;
                })) {
                result.changed = true;
            }
        }

        const auto autoCloseCommon = commonValue<float>(
            doc,
            targets,
            [](const slopengine::Brush& b) { return b.door.autoClose; },
            floatEq);
        float autoClose = autoCloseCommon.value_or(0.0f);
        if (ImGui::DragFloat("Auto-close", &autoClose, 0.1f, 0.0f, 120.0f, "%.1f s")) {
            if (forEachBrush(editor, targets, [autoClose](slopengine::Brush& brush, slopengine::BrushRole) {
                    if (brush.role != slopengine::BrushRole::Door) {
                        return;
                    }
                    brush.door.autoClose = autoClose;
                    brush.door.haveAutoClose = true;
                })) {
                result.changed = true;
            }
        }

        const auto travelCommon = commonValue<float>(
            doc,
            targets,
            [](const slopengine::Brush& b) { return b.door.travel; },
            floatEq);
        float travel = travelCommon.value_or(0.0f);
        if (ImGui::DragFloat("Travel (0=auto)", &travel, 0.05f, 0.0f, 64.0f, "%.2f")) {
            if (forEachBrush(editor, targets, [travel](slopengine::Brush& brush, slopengine::BrushRole) {
                    if (brush.role != slopengine::BrushRole::Door) {
                        return;
                    }
                    brush.door.travel = travel;
                    brush.door.haveTravel = true;
                })) {
                result.changed = true;
            }
        }

        const auto angleCommon = commonValue<float>(
            doc,
            targets,
            [](const slopengine::Brush& b) { return b.door.angle; },
            floatEq);
        float angleDeg = (angleCommon.value_or(1.5707963267948966f)) * (180.0f / 3.14159265358979323846f);
        if (ImGui::DragFloat("Swing angle", &angleDeg, 1.0f, -180.0f, 180.0f, "%.1f deg")) {
            const float angleRad = angleDeg * (3.14159265358979323846f / 180.0f);
            if (forEachBrush(editor, targets, [angleRad](slopengine::Brush& brush, slopengine::BrushRole) {
                    if (brush.role != slopengine::BrushRole::Door) {
                        return;
                    }
                    brush.door.angle = angleRad;
                    brush.door.haveAngle = true;
                })) {
                result.changed = true;
            }
        }

        const auto hingeCommon = commonValue<std::string>(
            doc, targets, [](const slopengine::Brush& b) { return b.door.hingeThingId; });
        const char* hingePreview = hingeCommon.has_value()
            ? (hingeCommon->empty() ? "(center)" : hingeCommon->c_str())
            : "—";
        if (ImGui::BeginCombo("Hinge thing", hingePreview)) {
            if (ImGui::Selectable("(center)", hingeCommon.has_value() && hingeCommon->empty())) {
                if (forEachBrush(editor, targets, [](slopengine::Brush& brush, slopengine::BrushRole) {
                        if (brush.role != slopengine::BrushRole::Door) {
                            return;
                        }
                        brush.door.hingeThingId.clear();
                    })) {
                    result.changed = true;
                }
            }
            for (const slopengine::Thing& thing : doc.things) {
                if (thing.id.empty()) {
                    continue;
                }
                const bool selected =
                    hingeCommon.has_value() && *hingeCommon == thing.id;
                if (ImGui::Selectable(thing.id.c_str(), selected)) {
                    const std::string id = thing.id;
                    if (forEachBrush(editor, targets, [id](slopengine::Brush& brush, slopengine::BrushRole) {
                            if (brush.role != slopengine::BrushRole::Door) {
                                return;
                            }
                            brush.door.hingeThingId = id;
                        })) {
                        result.changed = true;
                    }
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        const auto groupCommon = commonValue<std::string>(
            doc, targets, [](const slopengine::Brush& b) { return b.door.group; });
        char groupBuf[128]{};
        if (groupCommon.has_value()) {
            std::snprintf(groupBuf, sizeof(groupBuf), "%s", groupCommon->c_str());
        }
        if (ImGui::InputText("Group", groupBuf, sizeof(groupBuf))) {
            const std::string group = groupBuf;
            if (forEachBrush(editor, targets, [group](slopengine::Brush& brush, slopengine::BrushRole) {
                    if (brush.role != slopengine::BrushRole::Door) {
                        return;
                    }
                    brush.door.group = group;
                })) {
                result.changed = true;
            }
        }

        const auto promptCommon = commonValue<std::string>(
            doc, targets, [](const slopengine::Brush& b) { return b.door.prompt; });
        char promptBuf[128]{};
        if (promptCommon.has_value()) {
            std::snprintf(promptBuf, sizeof(promptBuf), "%s", promptCommon->c_str());
        } else {
            std::snprintf(promptBuf, sizeof(promptBuf), "Open");
        }
        if (ImGui::InputText("Prompt", promptBuf, sizeof(promptBuf))) {
            const std::string prompt = promptBuf;
            if (forEachBrush(editor, targets, [prompt](slopengine::Brush& brush, slopengine::BrushRole) {
                    if (brush.role != slopengine::BrushRole::Door) {
                        return;
                    }
                    brush.door.prompt = prompt;
                    brush.door.havePrompt = true;
                })) {
                result.changed = true;
            }
        }

        const auto canUseCommon = commonValue<slopengine::HandlerBinding>(
            doc, targets, [](const slopengine::Brush& b) { return b.door.canUse; });
        if (drawHandlerBindingEditor(
                editor,
                "Can use",
                "doorcanuse",
                slopengine::MapHandlerKind::CanUse,
                canUseCommon,
                [&](slopengine::HandlerBinding& next) {
                    forEachBrush(editor, targets, [&](slopengine::Brush& brush, slopengine::BrushRole) {
                        if (brush.role != slopengine::BrushRole::Door) {
                            return;
                        }
                        brush.door.canUse = next;
                    });
                })) {
            result.changed = true;
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
