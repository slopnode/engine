#include "inspector_panel.hpp"

#include "ui/icon_ui.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
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

void drawTriggerSizeSection(Editor& editor, slopengine::AssetStore& assets, NodePtr& alist) {
    if (!collapsingHeaderWithIcon(assets, kIcons, "package", "Trigger size (pickups)")) {
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

const std::vector<std::string>& knownKeys() {
    static const std::vector<std::string> keys = {
        "label",
        "icon",
        "path",
        "kind",
        "sprite",
        "geo",
        "frame",
        "anim",
        "anim-loop",
        "tags",
        "motor",
        "sight",
        "trigger-size",
    };
    return keys;
}

void drawRawSection(Editor& editor, slopengine::AssetStore& assets, ImFont* monoFont, ThingEntry& t) {
    if (!collapsingHeaderWithIcon(assets, kIcons, "page_white_code", "Other / raw")) {
        return;
    }

    const std::vector<NodePtr> kvs = listItems(t.alist);
    std::vector<NodePtr> other;
    for (const NodePtr& entry : kvs) {
        if (!isPair(entry) || !entry->car) {
            continue;
        }
        const auto& keys = knownKeys();
        if (std::find(keys.begin(), keys.end(), entry->car->str) == keys.end()) {
            other.push_back(entry);
        }
    }
    if (!other.empty()) {
        ImGui::TextDisabled("No dedicated editor yet for:");
        for (const NodePtr& entry : other) {
            ImGui::BulletText("%s", writeInline(entry).c_str());
        }
        ImGui::Spacing();
    }

    ImGui::TextUnformatted("Full alist (advanced — edit and Apply to re-parse):");
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

    if (collapsingHeaderWithIcon(assets, kIcons, "arrow_out", "Motor (movement)")) {
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

    drawTriggerSizeSection(editor, assets, t.alist);
    drawSightSection(editor, assets, t.alist);

    if (collapsingHeaderWithIcon(assets, kIcons, "tag_orange", "Tags")) {
        if (editStrListField(t.alist, "tags", "Tags")) {
            editor.markDirty();
        }
    }

    drawRawSection(editor, assets, monoFont, t);

    ImGui::EndChild();
}

}
