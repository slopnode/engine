#include "inspector_panel.hpp"

#include "package_scan.hpp"

#include "map/handler_binding.hpp"
#include "map/map_handler_registry.hpp"
#include "ui/icon_ui.hpp"

#include "imgui.h"

#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace slopthing {

namespace {

using slopengine::buttonWithIcon;
using slopengine::collapsingHeaderWithIcon;
using slopengine::kDefaultIconSet;

constexpr float kLabelWidth = 150.0f;
constexpr const char* kIcons = kDefaultIconSet;

void labeledField(const char* label, float width = kLabelWidth) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(width);
    ImGui::SetNextItemWidth(-1.0f);
}

std::string trim(const std::string& s) {
    std::size_t begin = s.find_first_not_of(" \t");
    if (begin == std::string::npos) {
        return "";
    }
    std::size_t end = s.find_last_not_of(" \t");
    return s.substr(begin, end - begin + 1);
}

bool editStrField(NodePtr& alist, const char* key, const char* label) {
    const std::string value = getStr(alist, key).value_or("");
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s", value.c_str());
    labeledField(label);
    ImGui::PushID(key);
    const bool changed = ImGui::InputText("##v", buf, sizeof(buf));
    ImGui::PopID();
    if (changed) {
        setStr(alist, key, buf);
    }
    return changed;
}

bool editFloatField(NodePtr& alist, const char* key, const char* label, float step = 0.1f) {
    float value = static_cast<float>(getFloat(alist, key).value_or(0.0));
    labeledField(label);
    ImGui::PushID(key);
    const bool changed = ImGui::DragFloat("##v", &value, step);
    ImGui::PopID();
    if (changed) {
        setFloat(alist, key, value);
    }
    return changed;
}

bool editIntField(NodePtr& alist, const char* key, const char* label) {
    int value = static_cast<int>(getInt(alist, key).value_or(0));
    labeledField(label);
    ImGui::PushID(key);
    const bool changed = ImGui::DragInt("##v", &value);
    ImGui::PopID();
    if (changed) {
        setInt(alist, key, value);
    }
    return changed;
}

bool editBoolField(NodePtr& alist, const char* key, const char* label) {
    bool value = getBool(alist, key).value_or(false);
    labeledField(label);
    ImGui::PushID(key);
    const bool changed = ImGui::Checkbox("##v", &value);
    ImGui::PopID();
    if (changed) {
        setBool(alist, key, value);
    }
    return changed;
}

bool editComboField(
    NodePtr& alist, const char* key, const char* label, const std::vector<const char*>& options) {
    const std::string value = getStr(alist, key).value_or(options.empty() ? "" : options.front());
    int idx = 0;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (value == options[i]) {
            idx = static_cast<int>(i);
        }
    }
    labeledField(label);
    ImGui::PushID(key);
    const bool changed =
        ImGui::Combo("##v", &idx, options.data(), static_cast<int>(options.size()));
    ImGui::PopID();
    if (changed) {
        setSymbol(alist, key, options[static_cast<std::size_t>(idx)]);
    }
    return changed;
}

bool editStrListField(NodePtr& alist, const char* key, const char* label) {
    const std::vector<std::string> items = getStrList(alist, key);
    std::string joined;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            joined += ", ";
        }
        joined += items[i];
    }
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s", joined.c_str());
    labeledField(label);
    ImGui::PushID(key);
    const bool changed = ImGui::InputText("##v", buf, sizeof(buf));
    ImGui::PopID();
    if (changed) {
        std::vector<std::string> parsed;
        std::string cur;
        for (char c : std::string(buf)) {
            if (c == ',') {
                std::string t = trim(cur);
                if (!t.empty()) {
                    parsed.push_back(t);
                }
                cur.clear();
            } else {
                cur += c;
            }
        }
        std::string t = trim(cur);
        if (!t.empty()) {
            parsed.push_back(t);
        }
        setStrList(alist, key, parsed);
    }
    return changed;
}

bool editOptionalFloat(NodePtr& alist, const char* key, const char* label, float defaultValue) {
    bool present = alistHasKey(alist, key);
    ImGui::PushID(key);
    bool changed = false;
    if (ImGui::Checkbox("##present", &present)) {
        if (present) {
            setFloat(alist, key, defaultValue);
        } else {
            alistRemoveKey(alist, key);
        }
        changed = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!present);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(kLabelWidth);
    ImGui::SetNextItemWidth(-1.0f);
    float value = static_cast<float>(getFloat(alist, key).value_or(defaultValue));
    if (ImGui::DragFloat("##v", &value, 0.05f) && present) {
        setFloat(alist, key, value);
        changed = true;
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    return changed;
}

bool editOptionalInt(NodePtr& alist, const char* key, const char* label, int defaultValue) {
    bool present = alistHasKey(alist, key);
    ImGui::PushID(key);
    bool changed = false;
    if (ImGui::Checkbox("##present", &present)) {
        if (present) {
            setInt(alist, key, defaultValue);
        } else {
            alistRemoveKey(alist, key);
        }
        changed = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!present);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(kLabelWidth);
    ImGui::SetNextItemWidth(-1.0f);
    int value = static_cast<int>(getInt(alist, key).value_or(defaultValue));
    if (ImGui::DragInt("##v", &value) && present) {
        setInt(alist, key, value);
        changed = true;
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    return changed;
}

void drawIdentitySection(Editor& editor, ThingEntry& t) {
    char idBuf[128];
    std::snprintf(idBuf, sizeof(idBuf), "%s", t.id.c_str());
    labeledField("Id");
    ImGui::PushID("##id");
    if (ImGui::InputText("##v", idBuf, sizeof(idBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
        const std::string newId = trim(idBuf);
        if (!newId.empty() && newId != t.id) {
            if (editor.doc.renameId(t.id, newId)) {
                editor.selectedId = newId;
                editor.markDirty();
            } else {
                editor.setStatus("Id already in use: " + newId, 4.0f);
            }
        }
    }
    ImGui::PopID();

    bool changed = false;
    changed |= editStrField(t.alist, "label", "Label");
    changed |= editStrField(t.alist, "icon", "Icon");
    changed |= editStrField(t.alist, "path", "Folder");
    changed |= editComboField(t.alist, "kind", "Kind", {"prop", "pickup", "actor"});
    if (changed) {
        editor.markDirty();
    }
}

void drawPresentationSection(Editor& editor, NodePtr& alist) {
    bool changed = false;
    changed |= editStrField(alist, "sprite", "Sprite");
    changed |= editStrField(alist, "geo", "Geo");
    changed |= editStrField(alist, "frame", "Frame");
    changed |= editStrField(alist, "anim", "Anim clip");

    bool loop = getBool(alist, "anim-loop").value_or(true);
    labeledField("Anim loop");
    if (ImGui::Checkbox("##animloop", &loop)) {
        setBool(alist, "anim-loop", loop);
        changed = true;
    }
    if (changed) {
        editor.markDirty();
    }
}

std::string handlerProcId(const NodePtr& rawCdr) {
    if (isPair(rawCdr) && rawCdr->car && rawCdr->car->kind == NodeKind::String) {
        return rawCdr->car->str;
    }
    return "";
}

NodePtr handlerArgsAlist(const NodePtr& rawCdr) {
    if (isPair(rawCdr)) {
        return rawCdr->cdr;
    }
    return makeNil();
}

NodePtr defaultArgPair(const slopengine::MapHandlerParam& param) {
    NodePtr value;
    switch (param.type) {
        case slopengine::HandlerArgType::Int:
            value = makeInt(param.hasDefault ? param.defaultValue.i : 0);
            break;
        case slopengine::HandlerArgType::Float:
            value = makeFloat(param.hasDefault ? static_cast<double>(param.defaultValue.f) : 0.0);
            break;
        case slopengine::HandlerArgType::Bool:
            value = makeBool(param.hasDefault ? param.defaultValue.b : false);
            break;
        default:
            value = makeString(param.hasDefault ? param.defaultValue.s : "");
            break;
    }
    return makeCons(makeSymbol(param.name), makeCons(value, makeNil()));
}

/**
 * Editor for a single `(on-enter proc-id (arg value)...)` style binding: a
 * dropdown of handlers this package has registered for @p kind (from
 * data/map-handlers.s7, the same catalog slopmap's own trigger UI reads
 * from), plus typed fields for that handler's declared params.
 */
void drawHandlerBindingField(
    Editor& editor,
    slopengine::AssetStore& assets,
    NodePtr& alist,
    const char* key,
    const char* label,
    slopengine::MapHandlerKind kind) {
    (void)assets;
    ImGui::PushID(key);
    bool present = alistHasKey(alist, key);
    if (ImGui::Checkbox(label, &present)) {
        if (present) {
            alistSetRest(alist, key, makeCons(makeString(""), makeNil()));
        } else {
            alistRemoveKey(alist, key);
        }
        editor.markDirty();
    }
    if (present) {
        NodePtr pair = alistFindPair(alist, key);
        const std::string procId = handlerProcId(pair->cdr);
        const slopengine::MapHandlerDef* current =
            procId.empty() ? nullptr : slopengine::mapHandlerRegistry().find(procId);

        const std::vector<const slopengine::MapHandlerDef*> options =
            slopengine::mapHandlerRegistry().handlersForKind(kind);
        std::string preview = "(choose handler)";
        if (current != nullptr) {
            preview = current->label.empty() ? current->id : current->label;
        } else if (!procId.empty()) {
            preview = procId + " (unregistered)";
        }

        labeledField("Handler");
        if (ImGui::BeginCombo("##handler", preview.c_str())) {
            for (const slopengine::MapHandlerDef* def : options) {
                const bool selected = def->id == procId;
                const std::string itemLabel = def->label.empty() ? def->id : def->label;
                if (ImGui::Selectable(itemLabel.c_str(), selected)) {
                    std::vector<NodePtr> args;
                    for (const slopengine::MapHandlerParam& p : def->params) {
                        args.push_back(defaultArgPair(p));
                    }
                    alistSetRest(alist, key, makeCons(makeString(def->id), makeList(args)));
                    editor.markDirty();
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (current != nullptr && !current->params.empty()) {
            NodePtr args = handlerArgsAlist(alistFindPair(alist, key)->cdr);
            bool argsChanged = false;
            for (const slopengine::MapHandlerParam& p : current->params) {
                switch (p.type) {
                    case slopengine::HandlerArgType::Int:
                        argsChanged |= editIntField(args, p.name.c_str(), p.name.c_str());
                        break;
                    case slopengine::HandlerArgType::Float:
                        argsChanged |= editFloatField(args, p.name.c_str(), p.name.c_str());
                        break;
                    case slopengine::HandlerArgType::Bool:
                        argsChanged |= editBoolField(args, p.name.c_str(), p.name.c_str());
                        break;
                    default:
                        argsChanged |= editStrField(args, p.name.c_str(), p.name.c_str());
                        break;
                }
            }
            if (argsChanged) {
                alistSetRest(alist, key, makeCons(makeString(current->id), args));
                editor.markDirty();
            }
        } else if (!procId.empty() && current == nullptr) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                "Not registered in this package's map-handlers.s7 -- edit args via raw below.");
        }
    }
    ImGui::PopID();
}

void drawTriggerSection(Editor& editor, slopengine::AssetStore& assets, NodePtr& alist) {
    if (!collapsingHeaderWithIcon(assets, kIcons, "lightning", "Trigger")) {
        return;
    }
    bool present = alistHasKey(alist, "trigger-size");
    ImGui::PushID("triggersize");
    if (ImGui::Checkbox("Override trigger size", &present)) {
        if (present) {
            alistSetRest(
                alist,
                "trigger-size",
                makeList({makeFloat(1.0), makeFloat(1.5), makeFloat(1.0)}));
        } else {
            alistRemoveKey(alist, "trigger-size");
        }
        editor.markDirty();
    }
    if (present) {
        NodePtr pair = alistFindPair(alist, "trigger-size");
        const std::vector<NodePtr> items = pair ? listItems(pair->cdr) : std::vector<NodePtr>{};
        float xyz[3] = {1.0f, 1.5f, 1.0f};
        for (int i = 0; i < 3 && i < static_cast<int>(items.size()); ++i) {
            const NodePtr& item = items[static_cast<std::size_t>(i)];
            if (item->kind == NodeKind::Float) {
                xyz[i] = static_cast<float>(item->floatVal);
            } else if (item->kind == NodeKind::Int) {
                xyz[i] = static_cast<float>(item->intVal);
            }
        }
        labeledField("Size (x y z)");
        if (ImGui::DragFloat3("##v", xyz, 0.05f)) {
            alistSetRest(
                alist,
                "trigger-size",
                makeList({makeFloat(xyz[0]), makeFloat(xyz[1]), makeFloat(xyz[2])}));
            editor.markDirty();
        }
    }
    ImGui::PopID();

    ImGui::Spacing();
    drawHandlerBindingField(
        editor, assets, alist, "on-enter", "On Enter", slopengine::MapHandlerKind::Enter);
    drawHandlerBindingField(
        editor, assets, alist, "on-use", "On Use", slopengine::MapHandlerKind::Use);
}

void drawSightSection(Editor& editor, slopengine::AssetStore& assets, NodePtr& alist) {
    if (!collapsingHeaderWithIcon(assets, kIcons, "eye", "Sight")) {
        return;
    }
    ImGui::PushID("sight");
    bool present = hasBlock(alist, "sight");
    if (ImGui::Checkbox("Enabled", &present)) {
        if (present) {
            setBlock(alist, "sight", defaultSightBlock());
        } else {
            removeBlock(alist, "sight");
        }
        editor.markDirty();
    }
    if (present) {
        NodePtr block = blockAlist(alist, "sight");
        bool changed = false;
        changed |= editFloatField(block, "range", "Range", 0.5f);
        changed |= editFloatField(block, "fov", "Field of view", 1.0f);
        changed |= editFloatField(block, "eye-lift", "Eye lift", 0.05f);

        bool active = getBool(block, "enabled").value_or(true);
        labeledField("Active");
        if (ImGui::Checkbox("##sightactive", &active)) {
            setBool(block, "enabled", active);
            changed = true;
        }

        changed |= editStrListField(block, "see-tags", "See tags");
        changed |= editStrListField(block, "ignore-tags", "Ignore tags");
        changed |= editStrField(block, "filter-proc", "Filter proc");
        if (changed) {
            setBlock(alist, "sight", block);
            editor.markDirty();
        }
    }
    ImGui::PopID();
}

/** Which convention field groups this package's own scripts actually reference. */
struct DetectedConventions {
    bool health = false;
    bool painChance = false;
    bool painThreshold = false;
    bool idleAnim = false;
    bool behavior = false;
    bool melee = false;
    bool ranged = false;
    bool lunge = false;

    bool any() const {
        return health || painChance || painThreshold || idleAnim || behavior || melee || ranged ||
            lunge;
    }
};

DetectedConventions detectConventions(const std::set<std::string>& used) {
    DetectedConventions d;
    d.health = used.count("thing-def-health") != 0;
    d.painChance = used.count("thing-def-pain-chance") != 0;
    d.painThreshold = used.count("thing-def-pain-threshold") != 0;
    d.idleAnim = used.count("thing-def-idle-anim") != 0;
    d.behavior = used.count("thing-def-behavior") != 0;
    d.melee = used.count("thing-def-melee-damage") != 0 || used.count("thing-def-melee-range") != 0 ||
        used.count("thing-def-melee-cooldown") != 0 || used.count("thing-def-melee-anim") != 0;
    d.ranged = used.count("thing-def-ranged-range") != 0 ||
        used.count("thing-def-ranged-min-range") != 0 ||
        used.count("thing-def-ranged-cooldown") != 0 ||
        used.count("thing-def-ranged-jitter") != 0 || used.count("thing-def-ranged-anim") != 0;
    d.lunge = used.count("thing-def-lunge-range") != 0 || used.count("thing-def-lunge-speed") != 0 ||
        used.count("thing-def-lunge-cooldown") != 0 || used.count("thing-def-lunge-duration") != 0;
    return d;
}

void drawPackageSpecificSection(
    Editor& editor,
    slopengine::AssetStore& assets,
    NodePtr& alist,
    const DetectedConventions& detected) {
    if (!collapsingHeaderWithIcon(assets, kIcons, "script", "Package Specific")) {
        return;
    }
    if (!detected.any()) {
        return;
    }

    bool changed = false;
    if (detected.health) {
        changed |= editOptionalInt(alist, "health", "Health", 20);
    }
    if (detected.painChance) {
        changed |= editOptionalFloat(alist, "pain-chance", "Pain chance", 0.5f);
    }
    if (detected.painThreshold) {
        changed |= editOptionalFloat(alist, "pain-threshold", "Pain threshold", 8.0f);
    }
    if (detected.idleAnim) {
        changed |= editStrField(alist, "idle-anim", "Idle anim");
    }
    if (detected.behavior) {
        changed |= editStrField(alist, "behavior", "Behavior");
    }

    if (detected.melee) {
        ImGui::PushID("melee");
        bool present = hasBlock(alist, "melee");
        if (ImGui::Checkbox("Melee attack", &present)) {
            if (present) {
                setBlock(alist, "melee", defaultMeleeBlock());
            } else {
                removeBlock(alist, "melee");
            }
            changed = true;
        }
        if (present) {
            NodePtr block = blockAlist(alist, "melee");
            bool blockChanged = false;
            blockChanged |= editFloatField(block, "damage", "Damage", 1.0f);
            blockChanged |= editFloatField(block, "range", "Range", 0.05f);
            blockChanged |= editFloatField(block, "cooldown", "Cooldown", 0.05f);
            blockChanged |= editStrField(block, "anim", "Anim clip");
            if (blockChanged) {
                setBlock(alist, "melee", block);
                changed = true;
            }
        }
        ImGui::PopID();
    }

    if (detected.ranged) {
        ImGui::PushID("ranged");
        bool present = hasBlock(alist, "ranged");
        if (ImGui::Checkbox("Ranged attack", &present)) {
            if (present) {
                setBlock(alist, "ranged", defaultRangedBlock());
            } else {
                removeBlock(alist, "ranged");
            }
            changed = true;
        }
        if (present) {
            NodePtr block = blockAlist(alist, "ranged");
            bool blockChanged = false;
            blockChanged |= editFloatField(block, "cooldown", "Cooldown", 0.05f);
            blockChanged |= editFloatField(block, "range", "Range", 0.5f);
            blockChanged |= editFloatField(block, "min-range", "Min range", 0.5f);
            blockChanged |= editFloatField(block, "jitter", "Cooldown jitter", 0.05f);
            blockChanged |= editStrField(block, "anim", "Anim clip");
            if (blockChanged) {
                setBlock(alist, "ranged", block);
                changed = true;
            }
        }
        ImGui::PopID();
    }

    if (detected.lunge) {
        ImGui::PushID("lunge");
        bool present = hasBlock(alist, "lunge");
        if (ImGui::Checkbox("Lunge attack", &present)) {
            if (present) {
                setBlock(alist, "lunge", defaultLungeBlock());
            } else {
                removeBlock(alist, "lunge");
            }
            changed = true;
        }
        if (present) {
            NodePtr block = blockAlist(alist, "lunge");
            bool blockChanged = false;
            blockChanged |= editFloatField(block, "range", "Range", 0.5f);
            blockChanged |= editFloatField(block, "speed", "Speed", 0.5f);
            blockChanged |= editFloatField(block, "cooldown", "Cooldown", 0.05f);
            blockChanged |= editFloatField(block, "duration", "Duration", 0.05f);
            if (blockChanged) {
                setBlock(alist, "lunge", block);
                changed = true;
            }
        }
        ImGui::PopID();
    }

    if (changed) {
        editor.markDirty();
    }
}

void drawRawSection(Editor& editor, slopengine::AssetStore& assets, ImFont* monoFont, ThingEntry& t) {
    if (!collapsingHeaderWithIcon(assets, kIcons, "page_white_code", "Other / raw")) {
        return;
    }

    const std::vector<NodePtr> kvs = listItems(t.alist);
    static char rawBuf[4096];
    static std::string rawBufId;
    if (rawBufId != t.id) {
        rawBufId = t.id;
        std::string text;
        for (const NodePtr& entry : kvs) {
            text += writeInline(entry);
            text += "\n";
        }
        std::snprintf(rawBuf, sizeof(rawBuf), "%s", text.c_str());
    }
    if (monoFont != nullptr) {
        ImGui::PushFont(monoFont, 0.0f);
    }
    ImGui::InputTextMultiline("##raw", rawBuf, sizeof(rawBuf), ImVec2(-1.0f, 160.0f));
    if (monoFont != nullptr) {
        ImGui::PopFont();
    }
    if (buttonWithIcon(assets, kIcons, "tick", "Apply raw")) {
        const std::string wrapped = "(" + std::string(rawBuf) + ")";
        NodePtr parsed = readDatum(editor.scheme, wrapped);
        if (isPair(parsed) || isNil(parsed)) {
            t.alist = parsed;
            editor.markDirty();
            editor.setStatus("Applied raw edit");
            rawBufId.clear();
        } else {
            editor.setStatus("Raw parse failed", 4.0f);
        }
    }
}

}

void drawInspectorPanel(
    Editor& editor, slopengine::AssetStore& assets, ImFont* monoFont, float bodyHeight) {
    ThingEntry* tptr = editor.selected();

    ImGui::BeginChild("##inspector", ImVec2(0, bodyHeight), false);
    if (tptr == nullptr) {
        ImGui::TextDisabled("Select a thing on the left, or create a new one.");
        ImGui::EndChild();
        return;
    }
    ThingEntry& t = *tptr;

    if (collapsingHeaderWithIcon(assets, kIcons, "tag_blue", "Identity", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawIdentitySection(editor, t);
    }
    if (collapsingHeaderWithIcon(
            assets, kIcons, "picture", "Presentation", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawPresentationSection(editor, t.alist);
    }

    if (collapsingHeaderWithIcon(assets, kIcons, "arrow_out", "Motor")) {
        ImGui::PushID("motor");
        bool present = hasBlock(t.alist, "motor");
        if (ImGui::Checkbox("Enabled", &present)) {
            if (present) {
                setBlock(t.alist, "motor", defaultMotorBlock());
            } else {
                removeBlock(t.alist, "motor");
            }
            editor.markDirty();
        }
        if (present) {
            NodePtr block = blockAlist(t.alist, "motor");
            bool changed = false;
            changed |= editFloatField(block, "radius", "Radius", 0.05f);
            changed |= editFloatField(block, "height", "Height", 0.05f);
            changed |= editFloatField(block, "speed", "Speed", 0.1f);
            changed |= editFloatField(block, "gravity", "Gravity", 0.1f);
            changed |= editFloatField(block, "step-height", "Step height", 0.05f);
            changed |= editFloatField(block, "vertical-speed", "Vertical speed", 0.1f);
            changed |= editFloatField(block, "hover-height", "Hover height", 0.05f);
            changed |= editComboField(block, "hull", "Hull", {"capsule", "sphere"});
            changed |= editComboField(block, "move", "Move mode", {"slide", "fly"});
            if (changed) {
                setBlock(t.alist, "motor", block);
                editor.markDirty();
            }
        }
        ImGui::PopID();
    }

    drawTriggerSection(editor, assets, t.alist);
    drawSightSection(editor, assets, t.alist);

    if (collapsingHeaderWithIcon(assets, kIcons, "tag_orange", "Tags")) {
        if (editStrListField(t.alist, "tags", "Tags")) {
            editor.markDirty();
        }
    }

    const DetectedConventions detected = detectConventions(editor.usedAccessors);
    drawPackageSpecificSection(editor, assets, t.alist, detected);

    drawRawSection(editor, assets, monoFont, t);

    ImGui::EndChild();
}

}
