#include "thing_panel.hpp"

#include "map/thing.hpp"

#include "imgui.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
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

enum class ThingEditKind {
    None,
    Light,
    Sound,
    Mixed,
};

std::vector<int> collectEditableTargets(const EditorDocument& doc, ThingEditKind* outKind) {
    std::vector<int> targets;
    ThingEditKind kind = ThingEditKind::None;
    if (doc.selectionMode != SelectionMode::Entity) {
        if (outKind != nullptr) {
            *outKind = ThingEditKind::None;
        }
        return targets;
    }
    for (const EntityRef& ref : doc.selectedEntities) {
        if (ref.kind != EntityRef::Kind::Thing || ref.index < 0 ||
            ref.index >= static_cast<int>(doc.things.size())) {
            continue;
        }
        const slopengine::ThingKind thingKind =
            doc.things[static_cast<std::size_t>(ref.index)].kind;
        ThingEditKind entryKind = ThingEditKind::None;
        if (slopengine::thingKindIsLight(thingKind)) {
            entryKind = ThingEditKind::Light;
        } else if (thingKind == slopengine::ThingKind::SoundSource) {
            entryKind = ThingEditKind::Sound;
        } else {
            continue;
        }
        if (kind == ThingEditKind::None) {
            kind = entryKind;
        } else if (kind != entryKind) {
            kind = ThingEditKind::Mixed;
        }
        targets.push_back(ref.index);
    }
    if (outKind != nullptr) {
        *outKind = targets.empty() ? ThingEditKind::None : kind;
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

bool forEachTarget(
    Editor& editor,
    const std::vector<int>& targets,
    const std::function<bool(const slopengine::Thing&)>& pred,
    const std::function<void(slopengine::Thing&)>& fn) {
    EditorDocument& doc = editor.doc();
    int count = 0;
    for (int index : targets) {
        slopengine::Thing* thing = thingAt(doc, index);
        if (thing == nullptr || !pred(*thing)) {
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

bool forEachLight(
    Editor& editor,
    const std::vector<int>& targets,
    const std::function<void(slopengine::Thing&)>& fn) {
    return forEachTarget(
        editor,
        targets,
        [](const slopengine::Thing& thing) { return slopengine::thingKindIsLight(thing.kind); },
        fn);
}

bool forEachSound(
    Editor& editor,
    const std::vector<int>& targets,
    const std::function<void(slopengine::Thing&)>& fn) {
    return forEachTarget(
        editor,
        targets,
        [](const slopengine::Thing& thing) {
            return thing.kind == slopengine::ThingKind::SoundSource;
        },
        fn);
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

bool inputTextMixed(const char* label, char* buf, std::size_t bufSize, bool mixed) {
    if (mixed) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.65f);
    }
    const bool changed = ImGui::InputText(label, buf, bufSize);
    if (mixed) {
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("mixed values");
        }
    }
    return changed;
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

void copyToBuf(char* buf, std::size_t bufSize, const std::string& value) {
    if (bufSize == 0) {
        return;
    }
    std::snprintf(buf, bufSize, "%s", value.c_str());
}

bool drawLightSection(Editor& editor, const std::vector<int>& targets) {
    bool changed = false;
    const EditorDocument& doc = editor.doc();

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
            changed = true;
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
            changed = true;
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
                changed = true;
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
                changed = true;
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
                changed = true;
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
                changed = true;
                editor.statusMessage = "Set area light size";
            }
        }
    }

    return changed;
}

bool drawSoundSection(Editor& editor, const std::vector<int>& targets) {
    bool changed = false;
    const EditorDocument& doc = editor.doc();

    ImGui::Text("Kind: sound-source");
    ImGui::Text("%d sound(s)", static_cast<int>(targets.size()));
    ImGui::Separator();

    const auto audioCommon = commonValue<std::string>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.audio; });
    const auto clipCommon = commonValue<std::string>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.clip; });
    const auto volumeCommon = commonValue<float>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.volume; },
        [](float a, float b) { return nearlyEqual(a, b); });
    const auto minCommon = commonValue<float>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.minDistance; },
        [](float a, float b) { return nearlyEqual(a, b); });
    const auto maxCommon = commonValue<float>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.maxDistance; },
        [](float a, float b) { return nearlyEqual(a, b); });
    const auto loopingCommon = commonValue<bool>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.looping; });
    const auto spatialCommon = commonValue<bool>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.spatial; });

    char audioBuf[256];
    copyToBuf(audioBuf, sizeof(audioBuf), audioCommon.value_or(std::string{}));
    if (inputTextMixed("Audio", audioBuf, sizeof(audioBuf), !audioCommon.has_value())) {
        const std::string audio = audioBuf;
        if (forEachSound(editor, targets, [&audio](slopengine::Thing& thing) {
                thing.audio = audio;
            })) {
            changed = true;
            editor.statusMessage = "Set sound audio";
        }
    }

    char clipBuf[256];
    copyToBuf(clipBuf, sizeof(clipBuf), clipCommon.value_or(std::string{}));
    if (inputTextMixed("Clip", clipBuf, sizeof(clipBuf), !clipCommon.has_value())) {
        const std::string clip = clipBuf;
        if (forEachSound(editor, targets, [&clip](slopengine::Thing& thing) {
                thing.clip = clip;
            })) {
            changed = true;
            editor.statusMessage = "Set sound clip";
        }
    }

    float volume = volumeCommon.value_or(1.0f);
    if (dragFloatMixed("Volume", &volume, !volumeCommon.has_value(), 0.01f, 0.0f, 4.0f)) {
        if (volume < 0.0f) {
            volume = 0.0f;
        }
        if (forEachSound(editor, targets, [volume](slopengine::Thing& thing) {
                thing.volume = volume;
            })) {
            changed = true;
            editor.statusMessage = "Set sound volume";
        }
    }

    float minDistance = minCommon.value_or(1.0f);
    if (dragFloatMixed("Min distance", &minDistance, !minCommon.has_value(), 0.05f, 0.01f, 1000.0f)) {
        if (minDistance < 0.01f) {
            minDistance = 0.01f;
        }
        if (forEachSound(editor, targets, [minDistance](slopengine::Thing& thing) {
                thing.minDistance = minDistance;
                if (thing.maxDistance < thing.minDistance) {
                    thing.maxDistance = thing.minDistance;
                }
            })) {
            changed = true;
            editor.statusMessage = "Set sound min distance";
        }
    }

    float maxDistance = maxCommon.value_or(30.0f);
    if (dragFloatMixed("Max distance", &maxDistance, !maxCommon.has_value(), 0.1f, 0.01f, 1000.0f)) {
        if (maxDistance < 0.01f) {
            maxDistance = 0.01f;
        }
        if (forEachSound(editor, targets, [maxDistance](slopengine::Thing& thing) {
                thing.maxDistance = maxDistance;
                if (thing.minDistance > thing.maxDistance) {
                    thing.minDistance = thing.maxDistance;
                }
            })) {
            changed = true;
            editor.statusMessage = "Set sound max distance";
        }
    }

    bool looping = loopingCommon.value_or(true);
    if (checkboxMixed("Looping", &looping, !loopingCommon.has_value())) {
        if (forEachSound(editor, targets, [looping](slopengine::Thing& thing) {
                thing.looping = looping;
            })) {
            changed = true;
            editor.statusMessage = "Set sound looping";
        }
    }

    bool spatial = spatialCommon.value_or(true);
    if (checkboxMixed("Spatial", &spatial, !spatialCommon.has_value())) {
        if (forEachSound(editor, targets, [spatial](slopengine::Thing& thing) {
                thing.spatial = spatial;
            })) {
            changed = true;
            editor.statusMessage = "Set sound spatial";
        }
    }

    return changed;
}

} // namespace

ThingPanelResult ThingPanel::drawSection(Editor& editor, float bodyHeight) {
    ThingPanelResult result{};
    if (!ImGui::BeginChild("##thingsection", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return result;
    }

    ThingEditKind editKind = ThingEditKind::None;
    const std::vector<int> targets = collectEditableTargets(editor.doc(), &editKind);
    if (targets.empty() || editKind == ThingEditKind::None) {
        ImGui::TextDisabled("Select light or sound thing(s) to edit");
        ImGui::EndChild();
        return result;
    }
    if (editKind == ThingEditKind::Mixed) {
        ImGui::TextDisabled("Select only lights or only sounds");
        ImGui::EndChild();
        return result;
    }

    if (editKind == ThingEditKind::Light) {
        result.changed = drawLightSection(editor, targets);
    } else if (editKind == ThingEditKind::Sound) {
        result.changed = drawSoundSection(editor, targets);
    }

    ImGui::EndChild();
    return result;
}

}
