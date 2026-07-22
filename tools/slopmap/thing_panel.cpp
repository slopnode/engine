#include "thing_panel.hpp"

#include "map/thing.hpp"

#include "imgui.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <vector>

namespace slopmap {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;

bool nearlyEqual(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

bool colorEqual(Vector3 a, Vector3 b) {
    return nearlyEqual(a.x, b.x) && nearlyEqual(a.y, b.y) && nearlyEqual(a.z, b.z);
}

bool sizeEqual(Vector2 a, Vector2 b) {
    return nearlyEqual(a.x, b.x) && nearlyEqual(a.y, b.y);
}

std::vector<int> collectLightTargets(const EditorDocument& doc) {
    std::vector<int> targets;
    if (doc.selectionMode != SelectionMode::Entity) {
        return targets;
    }
    for (const EntityRef& ref : doc.selectedEntities) {
        if (ref.kind != EntityRef::Kind::Thing || ref.index < 0 ||
            ref.index >= static_cast<int>(doc.things.size())) {
            continue;
        }
        if (!slopengine::thingKindIsLight(doc.things[static_cast<std::size_t>(ref.index)].kind)) {
            continue;
        }
        targets.push_back(ref.index);
    }
    return targets;
}

slopengine::Thing* thingAt(EditorDocument& doc, int index) {
    if (index < 0 || index >= static_cast<int>(doc.things.size())) {
        return nullptr;
    }
    return &doc.things[static_cast<std::size_t>(index)];
}

const slopengine::Thing* thingAt(const EditorDocument& doc, int index) {
    if (index < 0 || index >= static_cast<int>(doc.things.size())) {
        return nullptr;
    }
    return &doc.things[static_cast<std::size_t>(index)];
}

template <typename T>
std::optional<T> commonValue(
    const EditorDocument& doc,
    const std::vector<int>& targets,
    const std::function<T(const slopengine::Thing&)>& getter,
    const std::function<bool(const T&, const T&)>& equal) {
    std::optional<T> common;
    for (int index : targets) {
        const slopengine::Thing* thing = thingAt(doc, index);
        if (thing == nullptr) {
            continue;
        }
        const T value = getter(*thing);
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
    const std::function<T(const slopengine::Thing&)>& getter) {
    return commonValue<T>(doc, targets, getter, [](const T& a, const T& b) { return a == b; });
}

bool forEachLight(
    Editor& editor,
    const std::vector<int>& targets,
    const std::function<void(slopengine::Thing&)>& fn) {
    EditorDocument& doc = editor.doc();
    int count = 0;
    for (int index : targets) {
        slopengine::Thing* thing = thingAt(doc, index);
        if (thing == nullptr || !slopengine::thingKindIsLight(thing->kind)) {
            continue;
        }
        fn(*thing);
        editor.markThingCompileDirty(thing->kind);
        ++count;
    }
    if (count == 0) {
        return false;
    }
    editor.markDirty();
    return true;
}

bool dragFloatMixed(const char* label, float* value, bool mixed, float speed, float minV, float maxV) {
    if (mixed) {
        char overlay[32];
        std::snprintf(overlay, sizeof(overlay), "—");
        const bool changed = ImGui::DragFloat(label, value, speed, minV, maxV, overlay);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("mixed values");
        }
        return changed;
    }
    return ImGui::DragFloat(label, value, speed, minV, maxV);
}

bool colorEdit3Mixed(const char* label, float color[3], bool mixed) {
    if (mixed) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.65f);
    }
    const bool changed = ImGui::ColorEdit3(label, color, ImGuiColorEditFlags_Float);
    if (mixed) {
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("mixed values");
        }
    }
    return changed;
}

bool allKindsMatch(
    const EditorDocument& doc,
    const std::vector<int>& targets,
    const std::function<bool(slopengine::ThingKind)>& pred) {
    for (int index : targets) {
        const slopengine::Thing* thing = thingAt(doc, index);
        if (thing == nullptr || !pred(thing->kind)) {
            return false;
        }
    }
    return !targets.empty();
}

} // namespace

ThingPanelResult ThingPanel::drawSection(Editor& editor, float bodyHeight) {
    ThingPanelResult result{};
    if (!ImGui::BeginChild("##thingsection", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return result;
    }

    const EditorDocument& doc = editor.doc();
    const std::vector<int> targets = collectLightTargets(doc);
    if (targets.empty()) {
        ImGui::TextDisabled("Select light thing(s) to edit");
        ImGui::EndChild();
        return result;
    }

    const auto kindCommon = commonValue<slopengine::ThingKind>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.kind; });
    if (kindCommon.has_value()) {
        ImGui::Text("Kind: %s", slopengine::thingKindName(*kindCommon));
    } else {
        ImGui::TextDisabled("Kind: mixed");
    }
    ImGui::Text("%d light(s)", static_cast<int>(targets.size()));
    ImGui::Separator();

    const auto colorCommon = commonValue<Vector3>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.color; },
        colorEqual);
    const auto intensityCommon = commonValue<float>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.intensity; },
        [](float a, float b) { return nearlyEqual(a, b); });

    float color[3] = {
        colorCommon.value_or(Vector3{1.0f, 1.0f, 1.0f}).x,
        colorCommon.value_or(Vector3{1.0f, 1.0f, 1.0f}).y,
        colorCommon.value_or(Vector3{1.0f, 1.0f, 1.0f}).z,
    };
    if (colorEdit3Mixed("Color", color, !colorCommon.has_value())) {
        if (forEachLight(editor, targets, [color](slopengine::Thing& thing) {
                thing.color = {color[0], color[1], color[2]};
            })) {
            result.changed = true;
            editor.statusMessage = "Set light color";
        }
    }

    float intensity = intensityCommon.value_or(1.0f);
    if (dragFloatMixed("Intensity", &intensity, !intensityCommon.has_value(), 0.05f, 0.0f, 100.0f)) {
        if (intensity < 0.0f) {
            intensity = 0.0f;
        }
        if (forEachLight(editor, targets, [intensity](slopengine::Thing& thing) {
                thing.intensity = intensity;
            })) {
            result.changed = true;
            editor.statusMessage = "Set light intensity";
        }
    }

    const bool showRange = allKindsMatch(doc, targets, [](slopengine::ThingKind kind) {
        return kind == slopengine::ThingKind::PointLight || kind == slopengine::ThingKind::SpotLight;
    });
    if (showRange) {
        const auto rangeCommon = commonValue<float>(
            doc,
            targets,
            [](const slopengine::Thing& t) { return t.range; },
            [](float a, float b) { return nearlyEqual(a, b); });
        float range = rangeCommon.value_or(8.0f);
        if (dragFloatMixed("Range", &range, !rangeCommon.has_value(), 0.1f, 0.01f, 1000.0f)) {
            if (range < 0.01f) {
                range = 0.01f;
            }
            if (forEachLight(editor, targets, [range](slopengine::Thing& thing) {
                    thing.range = range;
                })) {
                result.changed = true;
                editor.statusMessage = "Set light range";
            }
        }
    }

    const bool showCone = allKindsMatch(doc, targets, [](slopengine::ThingKind kind) {
        return kind == slopengine::ThingKind::SpotLight;
    });
    if (showCone) {
        const auto coneCommon = commonValue<float>(
            doc,
            targets,
            [](const slopengine::Thing& t) { return t.coneAngle; },
            [](float a, float b) { return nearlyEqual(a, b); });
        float coneDegrees = coneCommon.value_or(0.7f) * kRadToDeg;
        if (dragFloatMixed(
                "Cone (deg)",
                &coneDegrees,
                !coneCommon.has_value(),
                0.5f,
                1.0f,
                179.0f)) {
            if (coneDegrees < 1.0f) {
                coneDegrees = 1.0f;
            }
            if (coneDegrees > 179.0f) {
                coneDegrees = 179.0f;
            }
            const float coneRadians = coneDegrees * kDegToRad;
            if (forEachLight(editor, targets, [coneRadians](slopengine::Thing& thing) {
                    thing.coneAngle = coneRadians;
                })) {
                result.changed = true;
                editor.statusMessage = "Set spot cone";
            }
        }
    }

    const bool showSize = allKindsMatch(doc, targets, [](slopengine::ThingKind kind) {
        return kind == slopengine::ThingKind::AreaLight;
    });
    if (showSize) {
        const auto sizeCommon = commonValue<Vector2>(
            doc,
            targets,
            [](const slopengine::Thing& t) { return t.size; },
            sizeEqual);
        float sizeW = sizeCommon.value_or(Vector2{1.0f, 1.0f}).x;
        float sizeH = sizeCommon.value_or(Vector2{1.0f, 1.0f}).y;
        if (dragFloatMixed("Size W", &sizeW, !sizeCommon.has_value(), 0.05f, 0.01f, 100.0f)) {
            if (sizeW < 0.01f) {
                sizeW = 0.01f;
            }
            if (forEachLight(editor, targets, [sizeW](slopengine::Thing& thing) {
                    thing.size.x = sizeW;
                })) {
                result.changed = true;
                editor.statusMessage = "Set area light size";
            }
        }
        if (dragFloatMixed("Size H", &sizeH, !sizeCommon.has_value(), 0.05f, 0.01f, 100.0f)) {
            if (sizeH < 0.01f) {
                sizeH = 0.01f;
            }
            if (forEachLight(editor, targets, [sizeH](slopengine::Thing& thing) {
                    thing.size.y = sizeH;
                })) {
                result.changed = true;
                editor.statusMessage = "Set area light size";
            }
        }
    }

    ImGui::EndChild();
    return result;
}

}
