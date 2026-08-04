#include "thing_panel.hpp"

#include "handler_ui.hpp"
#include "map/handler_binding.hpp"
#include "map/map_handler_registry.hpp"
#include "map/thing.hpp"
#include "map/thing_def_registry.hpp"
#include "render/skybox.hpp"

#include "core/package.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
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
    Prop,
    Actor,
    Trigger,
    Usable,
    Pickup,
    Mover,
    Particle,
    Skybox,
    Mixed,
};

enum class PresentationChannel {
    Sprite,
    Geo,
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
        } else if (thingKind == slopengine::ThingKind::Prop) {
            entryKind = ThingEditKind::Prop;
        } else if (thingKind == slopengine::ThingKind::Actor) {
            entryKind = ThingEditKind::Actor;
        } else if (thingKind == slopengine::ThingKind::Trigger) {
            entryKind = ThingEditKind::Trigger;
        } else if (thingKind == slopengine::ThingKind::Usable) {
            entryKind = ThingEditKind::Usable;
        } else if (thingKind == slopengine::ThingKind::Pickup) {
            entryKind = ThingEditKind::Pickup;
        } else if (thingKind == slopengine::ThingKind::Mover) {
            entryKind = ThingEditKind::Mover;
        } else if (thingKind == slopengine::ThingKind::Particle) {
            entryKind = ThingEditKind::Particle;
        } else if (thingKind == slopengine::ThingKind::Skybox) {
            entryKind = ThingEditKind::Skybox;
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

std::vector<std::string> scanPackageAssets(
    const slopengine::AssetStore& assets,
    const char* folder,
    const char* extension) {
    std::unordered_map<std::string, bool> seen;
    for (const slopengine::Package& package : assets.packages()) {
        const std::filesystem::path root = package.root() / folder;
        if (!std::filesystem::exists(root)) {
            continue;
        }
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec;
             it.increment(ec)) {
            if (ec || !it->is_regular_file()) {
                continue;
            }
            if (it->path().extension() != extension) {
                continue;
            }
            std::error_code relEc;
            std::filesystem::path relative = std::filesystem::relative(it->path(), root, relEc);
            if (relEc) {
                continue;
            }
            relative.replace_extension();
            seen[relative.generic_string()] = true;
        }
    }
    std::vector<std::string> paths;
    paths.reserve(seen.size());
    for (const auto& [path, _] : seen) {
        paths.push_back(path);
    }
    std::sort(paths.begin(), paths.end());
    return paths;
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
    editor.prepareEdit();
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
        editor.abortEdit();
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

bool forEachParticle(
    Editor& editor,
    const std::vector<int>& targets,
    const std::function<void(slopengine::Thing&)>& fn) {
    return forEachTarget(
        editor,
        targets,
        [](const slopengine::Thing& thing) {
            return thing.kind == slopengine::ThingKind::Particle;
        },
        fn);
}

bool forEachSkybox(
    Editor& editor,
    const std::vector<int>& targets,
    const std::function<void(slopengine::Thing&)>& fn) {
    EditorDocument& doc = editor.doc();
    editor.prepareEdit();
    int count = 0;
    for (int index : targets) {
        slopengine::Thing* thing = thingAt(doc, index);
        if (thing == nullptr || thing->kind != slopengine::ThingKind::Skybox) {
            continue;
        }
        fn(*thing);
        ++count;
    }
    if (count == 0) {
        editor.abortEdit();
        return false;
    }
    editor.markDirty();
    return true;
}

bool forEachActor(
    Editor& editor,
    const std::vector<int>& targets,
    const std::function<void(slopengine::Thing&)>& fn) {
    return forEachTarget(
        editor,
        targets,
        [](const slopengine::Thing& thing) { return thing.kind == slopengine::ThingKind::Actor; },
        fn);
}

bool forEachTrigger(
    Editor& editor,
    const std::vector<int>& targets,
    const std::function<void(slopengine::Thing&)>& fn) {
    return forEachTarget(
        editor,
        targets,
        [](const slopengine::Thing& thing) { return thing.kind == slopengine::ThingKind::Trigger; },
        fn);
}

bool forEachPickup(
    Editor& editor,
    const std::vector<int>& targets,
    const std::function<void(slopengine::Thing&)>& fn) {
    return forEachTarget(
        editor,
        targets,
        [](const slopengine::Thing& thing) { return thing.kind == slopengine::ThingKind::Pickup; },
        fn);
}

bool forEachPresentation(
    Editor& editor,
    const std::vector<int>& targets,
    const std::function<void(slopengine::Thing&)>& fn) {
    return forEachTarget(
        editor,
        targets,
        [](const slopengine::Thing& thing) {
            return slopengine::thingKindNeedsPresentation(thing.kind);
        },
        fn);
}

PresentationChannel channelForThing(const Editor& editor, const slopengine::Thing& thing) {
    if (!thing.geo.empty()) {
        return PresentationChannel::Geo;
    }
    if (!thing.sprite.empty()) {
        return PresentationChannel::Sprite;
    }
    if (thing.kind == slopengine::ThingKind::Prop) {
        const auto it = editor.propChannelLock.find(thing.id);
        if (it != editor.propChannelLock.end() && it->second == PlacePresentation::Geo) {
            return PresentationChannel::Geo;
        }
        if (it != editor.propChannelLock.end() && it->second == PlacePresentation::Sprite) {
            return PresentationChannel::Sprite;
        }
    }
    if (editor.placePresentation == PlacePresentation::Geo) {
        return PresentationChannel::Geo;
    }
    return PresentationChannel::Sprite;
}

PresentationChannel inferPresentationChannel(
    const Editor& editor,
    const EditorDocument& doc,
    const std::vector<int>& targets,
    bool* mixedOut) {
    std::optional<PresentationChannel> common;
    for (int index : targets) {
        const slopengine::Thing* thing = thingAt(doc, index);
        if (thing == nullptr) {
            continue;
        }
        const PresentationChannel channel = channelForThing(editor, *thing);
        if (!common.has_value()) {
            common = channel;
        } else if (*common != channel) {
            if (mixedOut != nullptr) {
                *mixedOut = true;
            }
            return PresentationChannel::Sprite;
        }
    }
    if (mixedOut != nullptr) {
        *mixedOut = false;
    }
    return common.value_or(PresentationChannel::Sprite);
}

void lockPropChannel(Editor& editor, const std::vector<int>& targets, PlacePresentation channel) {
    for (int index : targets) {
        slopengine::Thing* thing = thingAt(editor.doc(), index);
        if (thing == nullptr || thing->kind != slopengine::ThingKind::Prop) {
            continue;
        }
        editor.propChannelLock[thing->id] = channel;
    }
}

bool drawPresentationSection(
    Editor& editor,
    slopengine::AssetStore& assets,
    const std::vector<int>& targets,
    bool lockChannel) {
    bool changed = false;
    const EditorDocument& doc = editor.doc();

    bool mixedChannel = false;
    PresentationChannel channel = inferPresentationChannel(editor, doc, targets, &mixedChannel);

    if (lockChannel) {
        ImGui::TextUnformatted(channel == PresentationChannel::Geo ? "Geo" : "Sprite");
    } else {
        ImGui::TextUnformatted("Presentation");
        if (mixedChannel) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.65f);
        }
        if (ImGui::RadioButton(
                "Sprite##presChannel",
                !mixedChannel && channel == PresentationChannel::Sprite)) {
            if (forEachPresentation(editor, targets, [](slopengine::Thing& thing) {
                    thing.geo.clear();
                    thing.brush.clear();
                    if (thing.frame.empty()) {
                        thing.frame = "A";
                    }
                })) {
                changed = true;
                editor.placePresentation = PlacePresentation::Sprite;
                editor.statusMessage = "Set presentation to sprite";
            }
            channel = PresentationChannel::Sprite;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton(
                "Geo##presChannel", !mixedChannel && channel == PresentationChannel::Geo)) {
            if (forEachPresentation(editor, targets, [](slopengine::Thing& thing) {
                    thing.sprite.clear();
                    thing.brush.clear();
                })) {
                changed = true;
                editor.placePresentation = PlacePresentation::Geo;
                editor.statusMessage = "Set presentation to geo";
            }
            channel = PresentationChannel::Geo;
        }
        if (mixedChannel) {
            ImGui::PopStyleVar();
        }
    }

    static std::vector<std::string> spritePaths;
    static std::vector<std::string> geoPaths;
    static bool assetListsReady = false;
    if (!assetListsReady) {
        spritePaths = scanPackageAssets(assets, "sprites", ".spr");
        geoPaths = scanPackageAssets(assets, "geometry", ".geo");
        assetListsReady = true;
    }
    if (ImGui::Button("Refresh assets")) {
        spritePaths = scanPackageAssets(assets, "sprites", ".spr");
        geoPaths = scanPackageAssets(assets, "geometry", ".geo");
    }

    const auto spriteCommon = commonValue<std::string>(
        doc, targets, [](const slopengine::Thing& t) { return t.sprite; });
    const auto geoCommon = commonValue<std::string>(
        doc, targets, [](const slopengine::Thing& t) { return t.geo; });

    bool anyBrush = false;
    bool allBrush = !targets.empty();
    for (int index : targets) {
        const slopengine::Thing* thing = thingAt(doc, index);
        if (thing == nullptr) {
            allBrush = false;
            continue;
        }
        if (!thing->brush.empty()) {
            anyBrush = true;
        } else {
            allBrush = false;
        }
    }

    if (channel == PresentationChannel::Sprite) {
        const bool mixedSprite = !spriteCommon.has_value();
        const std::string spriteValue = spriteCommon.value_or(std::string{});
        const char* preview = mixedSprite         ? "(mixed)"
            : spriteValue.empty()                 ? "(none)"
                                                  : spriteValue.c_str();
        if (mixedSprite) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.65f);
        }
        if (ImGui::BeginCombo("Sprite##asset", preview)) {
            if (ImGui::Selectable("(none)", !mixedSprite && spriteValue.empty())) {
                if (forEachPresentation(editor, targets, [](slopengine::Thing& thing) {
                        thing.sprite.clear();
                        thing.geo.clear();
                        thing.brush.clear();
                    })) {
                    changed = true;
                    editor.placePresentation = PlacePresentation::Sprite;
                    lockPropChannel(editor, targets, PlacePresentation::Sprite);
                    editor.statusMessage = "Cleared sprite";
                }
            }
            for (const std::string& path : spritePaths) {
                ImGui::PushID(path.c_str());
                const bool selected = !mixedSprite && spriteValue == path;
                if (ImGui::Selectable(path.c_str(), selected)) {
                    if (forEachPresentation(editor, targets, [&path](slopengine::Thing& thing) {
                            thing.sprite = path;
                            thing.geo.clear();
                            thing.brush.clear();
                            if (thing.frame.empty()) {
                                thing.frame = "A";
                            }
                        })) {
                        changed = true;
                        editor.placePresentation = PlacePresentation::Sprite;
                        lockPropChannel(editor, targets, PlacePresentation::Sprite);
                        editor.statusMessage = "Set sprite " + path;
                    }
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (mixedSprite) {
            ImGui::PopStyleVar();
        }
        if (allBrush) {
            ImGui::TextDisabled("Using brush leaf (see below)");
        } else if (!mixedSprite && spriteValue.empty() && !anyBrush) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
                "Pick a sprite (required to play)");
        }
    } else {
        const bool mixedGeo = !geoCommon.has_value();
        const std::string geoValue = geoCommon.value_or(std::string{});
        const char* preview = mixedGeo         ? "(mixed)"
            : geoValue.empty()                 ? "(none)"
                                               : geoValue.c_str();
        if (mixedGeo) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.65f);
        }
        if (ImGui::BeginCombo("Geo##asset", preview)) {
            if (ImGui::Selectable("(none)", !mixedGeo && geoValue.empty())) {
                if (forEachPresentation(editor, targets, [](slopengine::Thing& thing) {
                        thing.sprite.clear();
                        thing.geo.clear();
                        thing.brush.clear();
                    })) {
                    changed = true;
                    editor.placePresentation = PlacePresentation::Geo;
                    lockPropChannel(editor, targets, PlacePresentation::Geo);
                    editor.statusMessage = "Cleared geo";
                }
            }
            for (const std::string& path : geoPaths) {
                ImGui::PushID(path.c_str());
                const bool selected = !mixedGeo && geoValue == path;
                if (ImGui::Selectable(path.c_str(), selected)) {
                    if (forEachPresentation(editor, targets, [&path](slopengine::Thing& thing) {
                            thing.geo = path;
                            thing.sprite.clear();
                            thing.brush.clear();
                        })) {
                        changed = true;
                        editor.placePresentation = PlacePresentation::Geo;
                        lockPropChannel(editor, targets, PlacePresentation::Geo);
                        editor.statusMessage = "Set geo " + path;
                    }
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (mixedGeo) {
            ImGui::PopStyleVar();
        }
        if (allBrush) {
            ImGui::TextDisabled("Using brush leaf (see below)");
        } else if (!mixedGeo && geoValue.empty() && !anyBrush) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
                "Pick a geo (required to play)");
        }
    }

    ImGui::Separator();
    return changed;
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

Vector3 thingWorldAngles(const slopengine::Thing& thing) {
    if (thing.haveAngles) {
        return thing.angles;
    }
    return {
        thing.havePitch ? thing.pitch : 0.0f,
        thing.yaw,
        0.0f,
    };
}

void setThingWorldAngles(slopengine::Thing& thing, Vector3 anglesRadians) {
    thing.haveAngles = true;
    thing.angles = anglesRadians;
    thing.yaw = anglesRadians.y;
    thing.pitch = anglesRadians.x;
    thing.havePitch = true;
}

bool drawWorldPoseSection(Editor& editor, const std::vector<int>& targets) {
    bool changed = false;
    const EditorDocument& doc = editor.doc();

    ImGui::TextDisabled("Position (world)");
    const auto posCommon = commonValue<Vector3>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.haveAt ? t.at : Vector3{}; },
        [](Vector3 a, Vector3 b) {
            return nearlyEqual(a.x, b.x) && nearlyEqual(a.y, b.y) && nearlyEqual(a.z, b.z);
        });
    float position[3] = {
        posCommon.value_or(Vector3{}).x,
        posCommon.value_or(Vector3{}).y,
        posCommon.value_or(Vector3{}).z,
    };
    if (dragFloatMixed("X", &position[0], !posCommon.has_value(), 0.05f, -10000.0f, 10000.0f)) {
        const float x = position[0];
        if (forEachTarget(
                editor,
                targets,
                [](const slopengine::Thing&) { return true; },
                [x](slopengine::Thing& thing) {
                    if (!thing.haveAt) {
                        thing.at = {};
                        thing.haveAt = true;
                    }
                    thing.at.x = x;
                })) {
            changed = true;
            editor.statusMessage = "Set world X";
        }
    }
    if (dragFloatMixed("Y", &position[1], !posCommon.has_value(), 0.05f, -10000.0f, 10000.0f)) {
        const float y = position[1];
        if (forEachTarget(
                editor,
                targets,
                [](const slopengine::Thing&) { return true; },
                [y](slopengine::Thing& thing) {
                    if (!thing.haveAt) {
                        thing.at = {};
                        thing.haveAt = true;
                    }
                    thing.at.y = y;
                })) {
            changed = true;
            editor.statusMessage = "Set world Y";
        }
    }
    if (dragFloatMixed("Z", &position[2], !posCommon.has_value(), 0.05f, -10000.0f, 10000.0f)) {
        const float z = position[2];
        if (forEachTarget(
                editor,
                targets,
                [](const slopengine::Thing&) { return true; },
                [z](slopengine::Thing& thing) {
                    if (!thing.haveAt) {
                        thing.at = {};
                        thing.haveAt = true;
                    }
                    thing.at.z = z;
                })) {
            changed = true;
            editor.statusMessage = "Set world Z";
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Rotation (world)");
    const auto anglesCommon = commonValue<Vector3>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return thingWorldAngles(t); },
        [](Vector3 a, Vector3 b) {
            return nearlyEqual(a.x, b.x) && nearlyEqual(a.y, b.y) && nearlyEqual(a.z, b.z);
        });
    float degrees[3] = {
        anglesCommon.value_or(Vector3{}).x * kRadToDeg,
        anglesCommon.value_or(Vector3{}).y * kRadToDeg,
        anglesCommon.value_or(Vector3{}).z * kRadToDeg,
    };
    if (dragFloatMixed("Pitch (deg)", &degrees[0], !anglesCommon.has_value(), 0.5f, -180.0f, 180.0f)) {
        const float pitch = degrees[0] * kDegToRad;
        if (forEachTarget(
                editor,
                targets,
                [](const slopengine::Thing&) { return true; },
                [pitch](slopengine::Thing& thing) {
                    Vector3 next = thingWorldAngles(thing);
                    next.x = pitch;
                    setThingWorldAngles(thing, next);
                })) {
            changed = true;
            editor.statusMessage = "Set world pitch";
        }
    }
    if (dragFloatMixed("Yaw (deg)", &degrees[1], !anglesCommon.has_value(), 0.5f, -180.0f, 180.0f)) {
        const float yaw = degrees[1] * kDegToRad;
        if (forEachTarget(
                editor,
                targets,
                [](const slopengine::Thing&) { return true; },
                [yaw](slopengine::Thing& thing) {
                    Vector3 next = thingWorldAngles(thing);
                    next.y = yaw;
                    setThingWorldAngles(thing, next);
                })) {
            changed = true;
            editor.statusMessage = "Set world yaw";
        }
    }
    if (dragFloatMixed("Roll (deg)", &degrees[2], !anglesCommon.has_value(), 0.5f, -180.0f, 180.0f)) {
        const float roll = degrees[2] * kDegToRad;
        if (forEachTarget(
                editor,
                targets,
                [](const slopengine::Thing&) { return true; },
                [roll](slopengine::Thing& thing) {
                    Vector3 next = thingWorldAngles(thing);
                    next.z = roll;
                    setThingWorldAngles(thing, next);
                })) {
            changed = true;
            editor.statusMessage = "Set world roll";
        }
    }
    if (ImGui::Button("Reset Rotation")) {
        if (forEachTarget(
                editor,
                targets,
                [](const slopengine::Thing&) { return true; },
                [](slopengine::Thing& thing) {
                    setThingWorldAngles(thing, {});
                })) {
            changed = true;
            editor.statusMessage = "Reset thing rotation to world identity";
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("Set pitch/yaw/roll to 0 in world space");
    }
    ImGui::Separator();
    return changed;
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

bool drawParticleSection(
    Editor& editor,
    slopengine::AssetStore& assets,
    const std::vector<int>& targets) {
    bool changed = false;
    const EditorDocument& doc = editor.doc();

    ImGui::Text("Kind: particle");
    ImGui::Text("%d particle system(s)", static_cast<int>(targets.size()));
    ImGui::Separator();

    const auto systemCommon = commonValue<std::string>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.particleSystem; });
    const auto playCommon = commonValue<bool>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.particlePlay; });

    const std::vector<std::string> systemPaths =
        scanPackageAssets(assets, "particles", ".prt");
    const char* systemPreview = !systemCommon.has_value()
        ? "(mixed)"
        : (systemCommon->empty() ? "(none)" : systemCommon->c_str());
    if (ImGui::BeginCombo("System##particle", systemPreview)) {
        if (ImGui::Selectable("(none)", systemCommon.has_value() && systemCommon->empty())) {
            if (forEachParticle(editor, targets, [](slopengine::Thing& thing) {
                    thing.particleSystem.clear();
                })) {
                changed = true;
                editor.statusMessage = "Cleared particle system";
            }
        }
        for (const std::string& path : systemPaths) {
            const bool selected = systemCommon.has_value() && *systemCommon == path;
            if (ImGui::Selectable(path.c_str(), selected)) {
                if (forEachParticle(editor, targets, [&path](slopengine::Thing& thing) {
                        thing.particleSystem = path;
                    })) {
                    changed = true;
                    editor.statusMessage = "Set particle system";
                }
            }
        }
        ImGui::EndCombo();
    }

    char systemBuf[256];
    copyToBuf(systemBuf, sizeof(systemBuf), systemCommon.value_or(std::string{}));
    if (inputTextMixed("System path", systemBuf, sizeof(systemBuf), !systemCommon.has_value())) {
        const std::string system = systemBuf;
        if (forEachParticle(editor, targets, [&system](slopengine::Thing& thing) {
                thing.particleSystem = system;
            })) {
            changed = true;
            editor.statusMessage = "Set particle system path";
        }
    }

    bool play = playCommon.value_or(true);
    if (checkboxMixed("Play", &play, !playCommon.has_value())) {
        if (forEachParticle(editor, targets, [play](slopengine::Thing& thing) {
                thing.particlePlay = play;
                thing.haveParticlePlay = true;
            })) {
            changed = true;
            editor.statusMessage = play ? "Particle play: on" : "Particle play: off";
        }
    }

    if (ImGui::Button("Restart preview")) {
        editor.particlePreviewEnabled = true;
        editor.particlePreviewRestartRequest = true;
        if (forEachParticle(editor, targets, [](slopengine::Thing& thing) {
                thing.particlePlay = true;
                thing.haveParticlePlay = true;
            })) {
            changed = true;
        }
        editor.statusMessage = "Restarted particle preview";
    }

    return changed;
}

void drawTypeInfo(const EditorDocument& doc, const std::vector<int>& targets) {
    const auto typeCommon = commonValue<std::string>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.type; });
    if (!typeCommon.has_value()) {
        ImGui::TextDisabled("Type: mixed");
        return;
    }
    if (typeCommon->empty()) {
        return;
    }
    if (const slopengine::ThingDef* def = slopengine::thingDefRegistry().find(*typeCommon)) {
        const char* role = "package";
        switch (def->packageRole) {
        case slopengine::PackageRole::Engine:
            role = "engine";
            break;
        case slopengine::PackageRole::Base:
            role = "base game";
            break;
        case slopengine::PackageRole::Mod:
            role = "mod";
            break;
        }
        ImGui::Text("Type: %s (%s)", def->label.c_str(), def->id.c_str());
        ImGui::TextDisabled("From: %s — %s", role, def->packageId.c_str());
    } else {
        ImGui::Text("Type: %s", typeCommon->c_str());
        ImGui::TextDisabled("From: unknown catalog");
    }
}

bool drawPropSection(
    Editor& editor,
    slopengine::AssetStore& assets,
    const std::vector<int>& targets) {
    bool changed = false;
    bool mixedChannel = false;
    const PresentationChannel channel =
        inferPresentationChannel(editor, editor.doc(), targets, &mixedChannel);
    const char* kindLabel = channel == PresentationChannel::Geo ? "geo" : "sprite";
    if (mixedChannel) {
        kindLabel = "prop";
    }
    ImGui::Text("Kind: %s", kindLabel);
    drawTypeInfo(editor.doc(), targets);
    ImGui::Text("%d selected", static_cast<int>(targets.size()));
    ImGui::Separator();
    if (drawPresentationSection(editor, assets, targets, true)) {
        changed = true;
    }
    return changed;
}

bool drawActorSection(
    Editor& editor,
    slopengine::AssetStore& assets,
    const std::vector<int>& targets) {
    bool changed = false;
    const EditorDocument& doc = editor.doc();

    ImGui::Text("Kind: actor");
    drawTypeInfo(doc, targets);
    ImGui::Text("%d actor(s)", static_cast<int>(targets.size()));
    {
        const auto typeCommon = commonValue<std::string>(
            doc,
            targets,
            [](const slopengine::Thing& t) { return t.type; });
        if (!typeCommon.has_value()) {
            ImGui::TextDisabled("Behavior: mixed");
        } else if (!typeCommon->empty()) {
            if (const slopengine::ThingDef* def =
                    slopengine::thingDefRegistry().find(*typeCommon)) {
                if (!def->behavior.empty()) {
                    ImGui::TextDisabled("Behavior: %s", def->behavior.c_str());
                } else {
                    ImGui::TextDisabled("Behavior: (none)");
                }
                if (!def->idleAnim.empty()) {
                    ImGui::TextDisabled("Idle anim: %s", def->idleAnim.c_str());
                }
            }
        }
    }
    ImGui::Separator();

    if (drawPresentationSection(editor, assets, targets, false)) {
        changed = true;
    }

    const auto radiusCommon = commonValue<float>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.motorRadius; },
        [](float a, float b) { return nearlyEqual(a, b); });
    const auto heightCommon = commonValue<float>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.motorHeight; },
        [](float a, float b) { return nearlyEqual(a, b); });
    const auto speedCommon = commonValue<float>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.motorSpeed; },
        [](float a, float b) { return nearlyEqual(a, b); });
    const auto gravityCommon = commonValue<float>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.motorGravity; },
        [](float a, float b) { return nearlyEqual(a, b); });
    const auto tagsCommon = commonValue<std::string>(
        doc,
        targets,
        [](const slopengine::Thing& t) {
            std::string joined;
            for (std::size_t i = 0; i < t.tags.size(); ++i) {
                if (i > 0) {
                    joined.push_back(' ');
                }
                joined += t.tags[i];
            }
            return joined;
        });

    float radius = radiusCommon.value_or(0.3f);
    if (dragFloatMixed("Radius", &radius, !radiusCommon.has_value(), 0.01f, 0.05f, 2.0f)) {
        if (forEachActor(editor, targets, [radius](slopengine::Thing& thing) {
                thing.motorRadius = radius;
                thing.haveMotor = true;
            })) {
            changed = true;
            editor.statusMessage = "Set actor motor radius";
        }
    }

    float height = heightCommon.value_or(1.1f);
    if (dragFloatMixed("Height", &height, !heightCommon.has_value(), 0.01f, 0.1f, 4.0f)) {
        if (forEachActor(editor, targets, [height](slopengine::Thing& thing) {
                thing.motorHeight = height;
                thing.haveMotor = true;
            })) {
            changed = true;
            editor.statusMessage = "Set actor motor height";
        }
    }

    float speed = speedCommon.value_or(6.0f);
    if (dragFloatMixed("Speed", &speed, !speedCommon.has_value(), 0.05f, 0.0f, 20.0f)) {
        if (forEachActor(editor, targets, [speed](slopengine::Thing& thing) {
                thing.motorSpeed = speed;
                thing.haveMotor = true;
            })) {
            changed = true;
            editor.statusMessage = "Set actor motor speed";
        }
    }

    float gravity = gravityCommon.value_or(9.81f);
    if (dragFloatMixed("Gravity", &gravity, !gravityCommon.has_value(), 0.05f, 0.0f, 40.0f)) {
        if (forEachActor(editor, targets, [gravity](slopengine::Thing& thing) {
                thing.motorGravity = gravity;
                thing.haveMotor = true;
            })) {
            changed = true;
            editor.statusMessage = "Set actor motor gravity";
        }
    }

    char tagsBuf[256];
    copyToBuf(tagsBuf, sizeof(tagsBuf), tagsCommon.value_or(std::string{}));
    if (inputTextMixed("Tags", tagsBuf, sizeof(tagsBuf), !tagsCommon.has_value())) {
        std::vector<std::string> tags;
        std::string token;
        for (const char* p = tagsBuf; *p != '\0'; ++p) {
            if (*p == ' ' || *p == '\t' || *p == ',') {
                if (!token.empty()) {
                    tags.push_back(token);
                    token.clear();
                }
                continue;
            }
            token.push_back(*p);
        }
        if (!token.empty()) {
            tags.push_back(token);
        }
        if (forEachActor(editor, targets, [&tags](slopengine::Thing& thing) {
                thing.tags = tags;
            })) {
            changed = true;
            editor.statusMessage = "Set actor tags";
        }
    }

    auto joinTagList = [](const std::vector<std::string>& tags) {
        std::string joined;
        for (std::size_t i = 0; i < tags.size(); ++i) {
            if (i > 0) {
                joined.push_back(' ');
            }
            joined += tags[i];
        }
        return joined;
    };
    auto parseTagList = [](const char* text) {
        std::vector<std::string> tags;
        std::string token;
        for (const char* p = text; *p != '\0'; ++p) {
            if (*p == ' ' || *p == '\t' || *p == ',') {
                if (!token.empty()) {
                    tags.push_back(token);
                    token.clear();
                }
                continue;
            }
            token.push_back(*p);
        }
        if (!token.empty()) {
            tags.push_back(token);
        }
        return tags;
    };

    ImGui::Separator();
    ImGui::TextDisabled("Sight");
    const auto enabledCommon = commonValue<bool>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.haveSight && t.sightEnabled; });
    const auto rangeCommon = commonValue<float>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.sightRange; },
        [](float a, float b) { return nearlyEqual(a, b); });
    const auto fovCommon = commonValue<float>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.sightFovDegrees; },
        [](float a, float b) { return nearlyEqual(a, b); });
    const auto eyeLiftCommon = commonValue<float>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.sightEyeLift; },
        [](float a, float b) { return nearlyEqual(a, b); });
    const auto seeTagsCommon = commonValue<std::string>(
        doc,
        targets,
        [&](const slopengine::Thing& t) { return joinTagList(t.sightSeeTags); });
    const auto ignoreTagsCommon = commonValue<std::string>(
        doc,
        targets,
        [&](const slopengine::Thing& t) { return joinTagList(t.sightIgnoreTags); });
    const auto filterCommon = commonValue<std::string>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.sightFilterProc; });

    bool enabled = enabledCommon.value_or(false);
    if (checkboxMixed("Enabled", &enabled, !enabledCommon.has_value())) {
        if (forEachActor(editor, targets, [enabled](slopengine::Thing& thing) {
                thing.haveSight = true;
                thing.sightEnabled = enabled;
            })) {
            changed = true;
            editor.statusMessage = "Set actor sight enabled";
        }
    }

    float range = rangeCommon.value_or(32.0f);
    if (dragFloatMixed("Range", &range, !rangeCommon.has_value(), 0.25f, 0.5f, 200.0f)) {
        if (forEachActor(editor, targets, [range](slopengine::Thing& thing) {
                thing.haveSight = true;
                thing.sightRange = range;
            })) {
            changed = true;
            editor.statusMessage = "Set actor sight range";
        }
    }

    float fov = fovCommon.value_or(180.0f);
    if (dragFloatMixed("FOV", &fov, !fovCommon.has_value(), 1.0f, 0.0f, 360.0f)) {
        if (forEachActor(editor, targets, [fov](slopengine::Thing& thing) {
                thing.haveSight = true;
                thing.sightFovDegrees = fov;
            })) {
            changed = true;
            editor.statusMessage = "Set actor sight FOV";
        }
    }

    float eyeLift = eyeLiftCommon.value_or(0.75f);
    if (dragFloatMixed("Eye lift", &eyeLift, !eyeLiftCommon.has_value(), 0.01f, 0.0f, 2.0f)) {
        if (forEachActor(editor, targets, [eyeLift](slopengine::Thing& thing) {
                thing.haveSight = true;
                thing.sightEyeLift = eyeLift;
            })) {
            changed = true;
            editor.statusMessage = "Set actor sight eye lift";
        }
    }

    char seeTagsBuf[256];
    copyToBuf(seeTagsBuf, sizeof(seeTagsBuf), seeTagsCommon.value_or(std::string{}));
    if (inputTextMixed("See tags", seeTagsBuf, sizeof(seeTagsBuf), !seeTagsCommon.has_value())) {
        const std::vector<std::string> tags = parseTagList(seeTagsBuf);
        if (forEachActor(editor, targets, [&tags](slopengine::Thing& thing) {
                thing.haveSight = true;
                thing.sightSeeTags = tags;
            })) {
            changed = true;
            editor.statusMessage = "Set actor see tags";
        }
    }

    char ignoreTagsBuf[256];
    copyToBuf(ignoreTagsBuf, sizeof(ignoreTagsBuf), ignoreTagsCommon.value_or(std::string{}));
    if (inputTextMixed(
            "Ignore tags", ignoreTagsBuf, sizeof(ignoreTagsBuf), !ignoreTagsCommon.has_value())) {
        const std::vector<std::string> tags = parseTagList(ignoreTagsBuf);
        if (forEachActor(editor, targets, [&tags](slopengine::Thing& thing) {
                thing.haveSight = true;
                thing.sightIgnoreTags = tags;
            })) {
            changed = true;
            editor.statusMessage = "Set actor ignore tags";
        }
    }

    char filterBuf[128];
    copyToBuf(filterBuf, sizeof(filterBuf), filterCommon.value_or(std::string{}));
    if (inputTextMixed("Filter", filterBuf, sizeof(filterBuf), !filterCommon.has_value())) {
        const std::string filter = filterBuf;
        if (forEachActor(editor, targets, [&filter](slopengine::Thing& thing) {
                thing.haveSight = true;
                thing.sightFilterProc = filter;
            })) {
            changed = true;
            editor.statusMessage = "Set actor sight filter";
        }
    }

    return changed;
}

bool forEachUsableOrMover(
    Editor& editor,
    const std::vector<int>& targets,
    const std::function<void(slopengine::Thing&)>& fn) {
    return forEachTarget(
        editor,
        targets,
        [](const slopengine::Thing& t) {
            return t.kind == slopengine::ThingKind::Usable || t.kind == slopengine::ThingKind::Mover;
        },
        fn);
}

bool drawUseHandlerSection(
    Editor& editor,
    slopengine::AssetStore& assets,
    const std::vector<int>& targets,
    const char* kindLabel) {
    bool changed = false;
    const EditorDocument& doc = editor.doc();
    ImGui::Text("Kind: %s", kindLabel);
    ImGui::Text("%d selected", static_cast<int>(targets.size()));
    ImGui::Separator();

    if (drawPresentationSection(editor, assets, targets, false)) {
        changed = true;
    }

    const auto onUseCommon = commonValue<slopengine::HandlerBinding>(
        doc, targets, [](const slopengine::Thing& t) { return t.onUse; });
    if (drawHandlerBindingEditor(
            editor,
            "On use",
            "onuse",
            slopengine::MapHandlerKind::Use,
            onUseCommon,
            [&](slopengine::HandlerBinding& next) {
                forEachUsableOrMover(editor, targets, [&](slopengine::Thing& thing) {
                    thing.onUse = next;
                    if (!next.empty()) {
                        thing.havePrompt = true;
                    }
                });
            })) {
        changed = true;
    }
    return changed;
}

bool forEachMover(
    Editor& editor,
    const std::vector<int>& targets,
    const std::function<void(slopengine::Thing&)>& fn) {
    return forEachTarget(
        editor,
        targets,
        [](const slopengine::Thing& t) { return t.kind == slopengine::ThingKind::Mover; },
        fn);
}

bool drawMoverSection(
    Editor& editor,
    slopengine::AssetStore& assets,
    const std::vector<int>& targets) {
    bool changed = false;
    const EditorDocument& doc = editor.doc();
    ImGui::TextUnformatted("Kind: mover");
    ImGui::Text("%d mover(s)", static_cast<int>(targets.size()));
    ImGui::Separator();

    if (drawPresentationSection(editor, assets, targets, false)) {
        changed = true;
    }

    const auto brushCommon = commonValue<std::string>(
        doc, targets, [](const slopengine::Thing& t) { return t.brush; });
    std::vector<std::string> brushIds;
    brushIds.reserve(doc.brushes.size());
    for (const slopengine::Brush& brush : doc.brushes) {
        if (!brush.id.empty() && brush.role == slopengine::BrushRole::Detail) {
            brushIds.push_back(brush.id);
        }
    }
    std::sort(brushIds.begin(), brushIds.end());
    brushIds.erase(std::unique(brushIds.begin(), brushIds.end()), brushIds.end());

    const bool mixedBrush = !brushCommon.has_value();
    const std::string brushValue = brushCommon.value_or(std::string{});
    const char* preview = mixedBrush              ? "(mixed)"
        : brushValue.empty()                      ? "(none — use geo/sprite)"
                                                  : brushValue.c_str();
    if (mixedBrush) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.65f);
    }
    if (ImGui::BeginCombo("Brush leaf", preview)) {
        if (ImGui::Selectable("(none)", !mixedBrush && brushValue.empty())) {
            if (forEachMover(editor, targets, [](slopengine::Thing& thing) {
                    thing.brush.clear();
                })) {
                changed = true;
                editor.markRadDirty();
                editor.statusMessage = "Cleared mover brush";
            }
        }
        for (const std::string& id : brushIds) {
            const bool selected = !mixedBrush && brushValue == id;
            if (ImGui::Selectable(id.c_str(), selected)) {
                if (forEachMover(editor, targets, [&id](slopengine::Thing& thing) {
                        thing.brush = id;
                        thing.geo.clear();
                        thing.sprite.clear();
                    })) {
                    changed = true;
                    editor.markRadDirty();
                    editor.statusMessage = "Set mover brush";
                }
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (mixedBrush) {
        ImGui::PopStyleVar();
    }

    ImGui::Separator();
    const auto pushCommon = commonValue<std::string>(
        doc, targets, [](const slopengine::Thing& t) {
            return t.moverPush.empty() ? std::string("full") : t.moverPush;
        });
    const bool mixedPush = !pushCommon.has_value();
    const std::string pushValue = pushCommon.value_or(std::string{"full"});
    if (mixedPush) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.65f);
    }
    if (ImGui::BeginCombo("Push", mixedPush ? "(mixed)" : pushValue.c_str())) {
        const char* pushOptions[] = {"full", "horizontal", "off"};
        for (const char* option : pushOptions) {
            const bool selected = !mixedPush && pushValue == option;
            if (ImGui::Selectable(option, selected)) {
                if (forEachMover(editor, targets, [option](slopengine::Thing& thing) {
                        thing.moverPush = option;
                    })) {
                    changed = true;
                    editor.statusMessage = "Set mover push";
                }
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (mixedPush) {
        ImGui::PopStyleVar();
    }

    const auto slideCommon = commonValue<bool>(
        doc, targets, [](const slopengine::Thing& t) { return t.moverSlide; });
    bool slide = slideCommon.value_or(true);
    if (checkboxMixed("Carry (ride)", &slide, !slideCommon.has_value())) {
        if (forEachMover(editor, targets, [slide](slopengine::Thing& thing) {
                thing.moverSlide = slide;
                thing.haveMoverSlide = true;
            })) {
            changed = true;
            editor.statusMessage = "Set mover carry";
        }
    }

    ImGui::Separator();
    const auto onUseCommon = commonValue<slopengine::HandlerBinding>(
        doc, targets, [](const slopengine::Thing& t) { return t.onUse; });
    if (drawHandlerBindingEditor(
            editor,
            "On use",
            "onuse",
            slopengine::MapHandlerKind::Use,
            onUseCommon,
            [&](slopengine::HandlerBinding& next) {
                forEachMover(editor, targets, [&](slopengine::Thing& thing) {
                    thing.onUse = next;
                    if (!next.empty()) {
                        thing.havePrompt = true;
                    }
                });
            })) {
        changed = true;
    }
    return changed;
}

bool drawTriggerSection(Editor& editor, const std::vector<int>& targets) {
    bool changed = false;
    const EditorDocument& doc = editor.doc();

    ImGui::TextUnformatted("Kind: trigger");
    ImGui::Text("%d trigger(s)", static_cast<int>(targets.size()));
    ImGui::Separator();

    const auto onEnterCommon = commonValue<slopengine::HandlerBinding>(
        doc, targets, [](const slopengine::Thing& t) { return t.onEnter; });
    const auto onExitCommon = commonValue<slopengine::HandlerBinding>(
        doc, targets, [](const slopengine::Thing& t) { return t.onExit; });
    const auto sizeCommon = commonValue<Vector3>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.triggerSize; },
        [](Vector3 a, Vector3 b) {
            return nearlyEqual(a.x, b.x) && nearlyEqual(a.y, b.y) && nearlyEqual(a.z, b.z);
        });

    if (drawHandlerBindingEditor(
            editor,
            "On enter",
            "onenter",
            slopengine::MapHandlerKind::Enter,
            onEnterCommon,
            [&](slopengine::HandlerBinding& next) {
                forEachTrigger(editor, targets, [&](slopengine::Thing& thing) {
                    thing.onEnter = next;
                });
            })) {
        changed = true;
    }

    if (drawHandlerBindingEditor(
            editor,
            "On exit",
            "onexit",
            slopengine::MapHandlerKind::Exit,
            onExitCommon,
            [&](slopengine::HandlerBinding& next) {
                forEachTrigger(editor, targets, [&](slopengine::Thing& thing) {
                    thing.onExit = next;
                });
            })) {
        changed = true;
    }

    Vector3 size = sizeCommon.value_or(Vector3{1.0f, 1.0f, 1.0f});
    float sizeArr[3] = {size.x, size.y, size.z};
    if (!sizeCommon.has_value()) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.65f);
    }
    const bool sizeChanged = ImGui::DragFloat3("Size", sizeArr, 0.05f, 0.01f, 1000.0f);
    if (!sizeCommon.has_value()) {
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("mixed values");
        }
    }
    if (sizeChanged) {
        const Vector3 next{sizeArr[0], sizeArr[1], sizeArr[2]};
        if (forEachTrigger(editor, targets, [next](slopengine::Thing& thing) {
                thing.triggerSize = next;
                thing.haveTriggerSize = true;
            })) {
            changed = true;
            editor.statusMessage = "Set trigger size";
        }
    }

    return changed;
}

std::vector<std::string> scanSkyMaterials(slopengine::AssetStore& assets) {
    std::vector<std::string> allMaterials = scanPackageAssets(assets, "materials", ".mat");
    std::vector<std::string> skyMaterials;
    skyMaterials.reserve(allMaterials.size());
    for (const std::string& path : allMaterials) {
        const slopengine::MaterialAsset* asset = assets.getMaterialAsset(path);
        if (asset != nullptr && asset->sky) {
            skyMaterials.push_back(path);
        }
    }
    std::sort(skyMaterials.begin(), skyMaterials.end());
    const auto defaultIt = std::find(skyMaterials.begin(), skyMaterials.end(), "engine/sky");
    if (defaultIt != skyMaterials.end() && defaultIt != skyMaterials.begin()) {
        std::rotate(skyMaterials.begin(), defaultIt, defaultIt + 1);
    }
    return skyMaterials;
}

const char* skyModeLabel(slopengine::SkyboxMode mode) {
    switch (mode) {
    case slopengine::SkyboxMode::Solid:
        return "Solid";
    case slopengine::SkyboxMode::Cube:
        return "Cube";
    case slopengine::SkyboxMode::Gradient:
        return "Gradient";
    }
    return "Unknown";
}

bool drawSkyTextureCombo(
    Editor& editor,
    slopengine::AssetStore& assets,
    const std::vector<int>& targets,
    const char* label,
    const std::function<std::string(const slopengine::Thing&)>& getter,
    const std::function<void(slopengine::Thing&, const std::string&)>& setter) {
    bool changed = false;
    const EditorDocument& doc = editor.doc();
    const auto pathCommon = commonValue<std::string>(doc, targets, getter);
    const std::vector<std::string> texturePaths = scanPackageAssets(assets, "textures", ".png");
    const char* preview =
        !pathCommon.has_value() ? "(mixed)" : (pathCommon->empty() ? "(none)" : pathCommon->c_str());
    if (ImGui::BeginCombo(label, preview)) {
        if (ImGui::Selectable("(none)", pathCommon.has_value() && pathCommon->empty())) {
            if (forEachSkybox(editor, targets, [&](slopengine::Thing& thing) {
                    setter(thing, {});
                })) {
                changed = true;
                editor.statusMessage = "Cleared sky cube face";
            }
        }
        for (const std::string& path : texturePaths) {
            const bool selected = pathCommon.has_value() && *pathCommon == path;
            if (ImGui::Selectable(path.c_str(), selected)) {
                if (forEachSkybox(editor, targets, [&](slopengine::Thing& thing) {
                        setter(thing, path);
                    })) {
                    changed = true;
                    editor.statusMessage = "Set sky cube face";
                }
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool drawSkyboxSection(
    Editor& editor,
    slopengine::AssetStore& assets,
    const std::vector<int>& targets) {
    bool changed = false;
    const EditorDocument& doc = editor.doc();

    ImGui::Text("Kind: skybox");
    ImGui::Text("%d skybox(es)", static_cast<int>(targets.size()));
    ImGui::Separator();

    const auto materialCommon = commonValue<std::string>(
        doc, targets, [](const slopengine::Thing& t) { return t.skyMaterial; });
    const std::vector<std::string> skyMaterials = scanSkyMaterials(assets);
    const char* materialPreview = !materialCommon.has_value()
        ? "(mixed)"
        : (materialCommon->empty() ? "(none)" : materialCommon->c_str());
    if (ImGui::BeginCombo("Sky material##skybox", materialPreview)) {
        if (ImGui::Selectable("(none)", materialCommon.has_value() && materialCommon->empty())) {
            if (forEachSkybox(editor, targets, [](slopengine::Thing& thing) {
                    thing.skyMaterial.clear();
                    thing.haveSkyboxMode = false;
                })) {
                changed = true;
                editor.statusMessage = "Cleared sky material";
            }
        }
        for (const std::string& path : skyMaterials) {
            const bool selected = materialCommon.has_value() && *materialCommon == path;
            if (ImGui::Selectable(path.c_str(), selected)) {
                if (forEachSkybox(editor, targets, [&path](slopengine::Thing& thing) {
                        thing.skyMaterial = path;
                        thing.haveSkyboxMode = false;
                    })) {
                    changed = true;
                    editor.statusMessage = "Set sky material";
                }
            }
        }
        ImGui::EndCombo();
    }

    if (!targets.empty()) {
        const slopengine::Thing* previewThing = thingAt(doc, targets.front());
        if (previewThing != nullptr) {
            const slopengine::SkyboxSettings settings =
                slopengine::skyboxSettingsFromThing(*previewThing, &assets);
            ImGui::Text("Resolved: %s", skyModeLabel(settings.mode));
        }
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Overrides", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto modeCommon = commonValue<slopengine::SkyboxMode>(
            doc,
            targets,
            [](const slopengine::Thing& t) { return t.skyboxMode; },
            [](slopengine::SkyboxMode a, slopengine::SkyboxMode b) { return a == b; });
        const auto overrideCommon = commonValue<bool>(
            doc, targets, [](const slopengine::Thing& t) { return t.haveSkyboxMode; });

        int modeIndex = modeCommon.has_value() ? static_cast<int>(*modeCommon) : 0;
        const char* modeLabels[] = {"Solid", "Cube", "Gradient"};
        if (!modeCommon.has_value()) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.65f);
        }
        if (ImGui::Combo("Mode##skybox", &modeIndex, modeLabels, IM_ARRAYSIZE(modeLabels))) {
            const slopengine::SkyboxMode next = static_cast<slopengine::SkyboxMode>(modeIndex);
            if (forEachSkybox(editor, targets, [next](slopengine::Thing& thing) {
                    thing.skyboxMode = next;
                    thing.haveSkyboxMode = true;
                    if (next == slopengine::SkyboxMode::Gradient) {
                        slopengine::ensureSkyboxGradientDefaults(thing);
                    }
                })) {
                changed = true;
                editor.statusMessage = "Set skybox mode override";
            }
        }
        if (!modeCommon.has_value()) {
            ImGui::PopStyleVar();
        }

        bool hasOverride = overrideCommon.value_or(false);
        if (checkboxMixed("Use overrides", &hasOverride, !overrideCommon.has_value())) {
            if (forEachSkybox(editor, targets, [hasOverride](slopengine::Thing& thing) {
                    thing.haveSkyboxMode = hasOverride;
                    if (hasOverride && thing.skyboxMode == slopengine::SkyboxMode::Gradient) {
                        slopengine::ensureSkyboxGradientDefaults(thing);
                    }
                })) {
                changed = true;
                editor.statusMessage = hasOverride ? "Sky overrides enabled" : "Sky overrides disabled";
            }
        }

        const slopengine::SkyboxMode editMode =
            modeCommon.value_or(slopengine::SkyboxMode::Gradient);
        if (hasOverride) {
            if (editMode == slopengine::SkyboxMode::Solid) {
                const auto colorCommon = commonValue<Vector3>(
                    doc, targets, [](const slopengine::Thing& t) { return t.color; }, colorEqual);
                float color[3] = {
                    colorCommon.has_value() ? colorCommon->x : 0.0f,
                    colorCommon.has_value() ? colorCommon->y : 0.0f,
                    colorCommon.has_value() ? colorCommon->z : 0.0f,
                };
                if (colorEdit3Mixed("Color##skybox", color, !colorCommon.has_value())) {
                    const Vector3 next{color[0], color[1], color[2]};
                    if (forEachSkybox(editor, targets, [next](slopengine::Thing& thing) {
                            thing.color = next;
                            thing.haveSkyboxMode = true;
                            thing.skyboxMode = slopengine::SkyboxMode::Solid;
                        })) {
                        changed = true;
                        editor.statusMessage = "Set sky color";
                    }
                }
            } else if (editMode == slopengine::SkyboxMode::Gradient) {
                for (int stopIndex = 0; stopIndex < 4; ++stopIndex) {
                    ImGui::PushID(stopIndex);
                    const auto posCommon = commonValue<float>(
                        doc,
                        targets,
                        [stopIndex](const slopengine::Thing& t) {
                            if (stopIndex >= t.skyGradientStopCount) {
                                return 0.0f;
                            }
                            return t.skyGradientStops[static_cast<std::size_t>(stopIndex)].position;
                        });
                    const auto stopColorCommon = commonValue<Vector3>(
                        doc,
                        targets,
                        [stopIndex](const slopengine::Thing& t) {
                            if (stopIndex >= t.skyGradientStopCount) {
                                return Vector3{0.0f, 0.0f, 0.0f};
                            }
                            return t.skyGradientStops[static_cast<std::size_t>(stopIndex)].color;
                        },
                        colorEqual);
                    float position = posCommon.value_or(0.0f);
                    float stopColor[3] = {
                        stopColorCommon.has_value() ? stopColorCommon->x : 0.0f,
                        stopColorCommon.has_value() ? stopColorCommon->y : 0.0f,
                        stopColorCommon.has_value() ? stopColorCommon->z : 0.0f,
                    };
                    if (dragFloatMixed(
                            "Position",
                            &position,
                            !posCommon.has_value(),
                            0.01f,
                            0.0f,
                            1.0f)) {
                        if (forEachSkybox(editor, targets, [stopIndex, position](slopengine::Thing& thing) {
                                thing.haveSkyboxMode = true;
                                thing.skyboxMode = slopengine::SkyboxMode::Gradient;
                                if (thing.skyGradientStopCount < 4) {
                                    thing.skyGradientStopCount = 4;
                                }
                                thing.skyGradientStops[static_cast<std::size_t>(stopIndex)].position =
                                    position;
                            })) {
                            changed = true;
                        }
                    }
                    if (colorEdit3Mixed("Color", stopColor, !stopColorCommon.has_value())) {
                        const Vector3 next{stopColor[0], stopColor[1], stopColor[2]};
                        if (forEachSkybox(editor, targets, [stopIndex, next](slopengine::Thing& thing) {
                                thing.haveSkyboxMode = true;
                                thing.skyboxMode = slopengine::SkyboxMode::Gradient;
                                if (thing.skyGradientStopCount < 4) {
                                    thing.skyGradientStopCount = 4;
                                }
                                thing.skyGradientStops[static_cast<std::size_t>(stopIndex)].color = next;
                            })) {
                            changed = true;
                        }
                    }
                    ImGui::PopID();
                }
            } else if (editMode == slopengine::SkyboxMode::Cube) {
                struct FaceBinding {
                    const char* label;
                    std::string slopengine::Thing::*field;
                };
                const FaceBinding faces[] = {
                    {"+X (px)", &slopengine::Thing::skyCubePx},
                    {"-X (nx)", &slopengine::Thing::skyCubeNx},
                    {"+Y (py)", &slopengine::Thing::skyCubePy},
                    {"-Y (ny)", &slopengine::Thing::skyCubeNy},
                    {"+Z (pz)", &slopengine::Thing::skyCubePz},
                    {"-Z (nz)", &slopengine::Thing::skyCubeNz},
                };
                for (const FaceBinding& face : faces) {
                    if (drawSkyTextureCombo(
                            editor,
                            assets,
                            targets,
                            face.label,
                            [field = face.field](const slopengine::Thing& thing) {
                                return thing.*field;
                            },
                            [field = face.field](slopengine::Thing& thing, const std::string& path) {
                                thing.*field = path;
                                thing.haveSkyboxMode = true;
                                thing.skyboxMode = slopengine::SkyboxMode::Cube;
                            })) {
                        changed = true;
                    }
                }
            }
        }
    }

    return changed;
}

bool drawPickupSection(
    Editor& editor,
    slopengine::AssetStore& assets,
    const std::vector<int>& targets) {
    bool changed = false;
    const EditorDocument& doc = editor.doc();

    ImGui::TextUnformatted("Kind: pickup");
    ImGui::Text("%d pickup(s)", static_cast<int>(targets.size()));
    ImGui::Separator();

    if (drawPresentationSection(editor, assets, targets, false)) {
        changed = true;
    }

    ImGui::Separator();
    const auto onEnterCommon = commonValue<slopengine::HandlerBinding>(
        doc, targets, [](const slopengine::Thing& t) { return t.onEnter; });
    const auto sizeCommon = commonValue<Vector3>(
        doc,
        targets,
        [](const slopengine::Thing& t) { return t.triggerSize; },
        [](Vector3 a, Vector3 b) {
            return nearlyEqual(a.x, b.x) && nearlyEqual(a.y, b.y) && nearlyEqual(a.z, b.z);
        });

    if (drawHandlerBindingEditor(
            editor,
            "On enter",
            "onenter",
            slopengine::MapHandlerKind::Enter,
            onEnterCommon,
            [&](slopengine::HandlerBinding& next) {
                forEachPickup(editor, targets, [&](slopengine::Thing& thing) {
                    thing.onEnter = next;
                    if (!next.empty()) {
                        thing.haveTriggerSize = true;
                    }
                });
            })) {
        changed = true;
    }

    Vector3 size = sizeCommon.value_or(Vector3{1.0f, 1.0f, 1.0f});
    float sizeArr[3] = {size.x, size.y, size.z};
    if (!sizeCommon.has_value()) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.65f);
    }
    const bool sizeChanged = ImGui::DragFloat3("Size", sizeArr, 0.05f, 0.01f, 1000.0f);
    if (!sizeCommon.has_value()) {
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("mixed values");
        }
    }
    if (sizeChanged) {
        const Vector3 next{sizeArr[0], sizeArr[1], sizeArr[2]};
        if (forEachPickup(editor, targets, [next](slopengine::Thing& thing) {
                thing.triggerSize = next;
                thing.haveTriggerSize = true;
            })) {
            changed = true;
            editor.statusMessage = "Set pickup trigger size";
        }
    }

    ImGui::Separator();
    const auto onUseCommon = commonValue<slopengine::HandlerBinding>(
        doc, targets, [](const slopengine::Thing& t) { return t.onUse; });
    if (drawHandlerBindingEditor(
            editor,
            "On use",
            "onuse",
            slopengine::MapHandlerKind::Use,
            onUseCommon,
            [&](slopengine::HandlerBinding& next) {
                forEachPickup(editor, targets, [&](slopengine::Thing& thing) {
                    thing.onUse = next;
                });
            })) {
        changed = true;
    }

    return changed;
}

} // namespace

ThingPanelResult ThingPanel::drawSection(
    Editor& editor,
    slopengine::AssetStore& assets,
    float bodyHeight) {
    ThingPanelResult result{};
    if (!ImGui::BeginChild("##thingsection", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return result;
    }

    ThingEditKind editKind = ThingEditKind::None;
    const std::vector<int> targets = collectEditableTargets(editor.doc(), &editKind);
    if (targets.empty() || editKind == ThingEditKind::None) {
        ImGui::TextDisabled(
            "Select prop, light, sound, particle, skybox, actor, trigger, usable, pickup, or mover thing(s) to edit");
        ImGui::EndChild();
        return result;
    }
    if (editKind == ThingEditKind::Mixed) {
        ImGui::TextDisabled("Select only one editable kind");
        ImGui::EndChild();
        return result;
    }

    if (editKind != ThingEditKind::Skybox) {
        result.changed = drawWorldPoseSection(editor, targets);
    }

    if (editKind == ThingEditKind::Light) {
        result.changed = drawLightSection(editor, targets) || result.changed;
    } else if (editKind == ThingEditKind::Sound) {
        result.changed = drawSoundSection(editor, targets) || result.changed;
    } else if (editKind == ThingEditKind::Prop) {
        result.changed = drawPropSection(editor, assets, targets) || result.changed;
    } else if (editKind == ThingEditKind::Actor) {
        result.changed = drawActorSection(editor, assets, targets) || result.changed;
    } else if (editKind == ThingEditKind::Trigger) {
        result.changed = drawTriggerSection(editor, targets) || result.changed;
    } else if (editKind == ThingEditKind::Usable) {
        result.changed = drawUseHandlerSection(editor, assets, targets, "usable") || result.changed;
    } else if (editKind == ThingEditKind::Pickup) {
        result.changed = drawPickupSection(editor, assets, targets) || result.changed;
    } else if (editKind == ThingEditKind::Mover) {
        result.changed = drawMoverSection(editor, assets, targets) || result.changed;
    } else if (editKind == ThingEditKind::Particle) {
        result.changed = drawParticleSection(editor, assets, targets) || result.changed;
    } else if (editKind == ThingEditKind::Skybox) {
        result.changed = drawSkyboxSection(editor, assets, targets) || result.changed;
    }

    ImGui::EndChild();
    return result;
}

}
