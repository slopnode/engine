#include "browser_panel.hpp"

#include "ui/icon_ui.hpp"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace slopthing {

namespace {

using slopengine::buttonWithIcon;
using slopengine::drawIconImGui;
using slopengine::iconButton;
using slopengine::kDefaultIconSet;
using slopengine::selectableWithIcon;

float iconButtonWidth() {
    return ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x;
}

bool deleteIconButton(slopengine::AssetStore& assets) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.30f, 0.30f, 1.0f));
    const bool pressed =
        iconButton(assets, kDefaultIconSet, "cross", ImVec2(iconButtonWidth(), 0.0f));
    ImGui::PopStyleColor();
    return pressed;
}

bool containsCaseInsensitive(const std::string& hay, const std::string& needle) {
    auto it = std::search(
        hay.begin(),
        hay.end(),
        needle.begin(),
        needle.end(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                std::tolower(static_cast<unsigned char>(b));
        });
    return it != hay.end();
}

bool matchesFilter(const ThingEntry& t, const std::string& filter) {
    if (filter.empty()) {
        return true;
    }
    if (containsCaseInsensitive(t.id, filter)) {
        return true;
    }
    if (auto label = getStr(t.alist, "label"); label && containsCaseInsensitive(*label, filter)) {
        return true;
    }
    return false;
}

void drawThingRow(Editor& editor, slopengine::AssetStore& assets, const ThingEntry& t) {
    const std::string icon = getStr(t.alist, "icon").value_or("page");
    const std::string label = getStr(t.alist, "label").value_or(t.id);
    const bool selected = editor.selectedId == t.id;
    ImGui::PushID(t.id.c_str());
    // Match slopmap's thing-catalog highlight: the selected row gets the
    // theme's button-active blue instead of the default header highlight.
    if (selected) {
        const ImVec4 activeColor = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
        ImGui::PushStyleColor(ImGuiCol_Header, activeColor);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, activeColor);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, activeColor);
    }
    if (selectableWithIcon(assets, kDefaultIconSet, icon, label.c_str(), selected)) {
        editor.select(t.id);
    }
    if (selected) {
        ImGui::PopStyleColor(3);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", t.id.c_str());
    }
    ImGui::PopID();
}

}

void drawBrowserPanel(Editor& editor, slopengine::AssetStore& assets, float bodyHeight) {
    constexpr const char* kIcons = kDefaultIconSet;
    const ThingEntry* sel = editor.selected();

    if (buttonWithIcon(assets, kIcons, "page_add", "New")) {
        editor.newThingIsDuplicate = false;
        editor.idInputBuf[0] = '\0';
        editor.showNewThingModal = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(sel == nullptr);
    if (buttonWithIcon(assets, kIcons, "page_copy", "Duplicate")) {
        editor.newThingIsDuplicate = true;
        std::snprintf(editor.idInputBuf, sizeof(editor.idInputBuf), "%s_copy", sel->id.c_str());
        editor.showNewThingModal = true;
    }
    ImGui::SameLine();
    if (deleteIconButton(assets)) {
        editor.deleteThing(sel->id);
    }
    ImGui::EndDisabled();

    char filterBuf[128];
    std::snprintf(filterBuf, sizeof(filterBuf), "%s", editor.filter.c_str());
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##filter", "Filter...", filterBuf, sizeof(filterBuf))) {
        editor.filter = filterBuf;
    }

    ImGui::Separator();

    ImGui::BeginChild("##thinglist", ImVec2(0, bodyHeight), false);

    std::vector<bool> used(editor.doc.things.size(), false);

    for (const FolderEntry& folder : editor.doc.folders) {
        std::vector<std::size_t> members;
        for (std::size_t i = 0; i < editor.doc.things.size(); ++i) {
            const ThingEntry& t = editor.doc.things[i];
            if (getStr(t.alist, "path").value_or("") == folder.path &&
                matchesFilter(t, editor.filter)) {
                members.push_back(i);
            }
        }
        if (members.empty() && !editor.filter.empty()) {
            continue;
        }
        ImGui::PushID(folder.path.c_str());
        drawIconImGui(assets, kIcons, folder.icon.empty() ? "folder" : folder.icon);
        ImGui::SameLine();
        const bool open = ImGui::TreeNodeEx(
            folder.path.c_str(),
            ImGuiTreeNodeFlags_DefaultOpen,
            "%s (%zu)",
            folder.path.c_str(),
            members.size());
        if (open) {
            for (std::size_t i : members) {
                used[i] = true;
                drawThingRow(editor, assets, editor.doc.things[i]);
            }
            ImGui::TreePop();
        } else {
            for (std::size_t i : members) {
                used[i] = true;
            }
        }
        ImGui::PopID();
    }

    std::vector<std::size_t> unsorted;
    for (std::size_t i = 0; i < editor.doc.things.size(); ++i) {
        if (!used[i] && matchesFilter(editor.doc.things[i], editor.filter)) {
            unsorted.push_back(i);
        }
    }
    if (!unsorted.empty()) {
        if (ImGui::TreeNodeEx(
                "(unsorted)",
                ImGuiTreeNodeFlags_DefaultOpen,
                "(unsorted) (%zu)",
                unsorted.size())) {
            for (std::size_t i : unsorted) {
                drawThingRow(editor, assets, editor.doc.things[i]);
            }
            ImGui::TreePop();
        }
    }

    ImGui::EndChild();

    if (editor.showNewThingModal) {
        ImGui::OpenPopup("New Thing");
        editor.showNewThingModal = false;
    }
    if (ImGui::BeginPopupModal("New Thing", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Thing id");
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputText("##newid", editor.idInputBuf, sizeof(editor.idInputBuf));
        const bool inUse = editor.doc.idInUse(editor.idInputBuf);
        if (inUse) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "id already in use");
        }
        const char* createLabel = editor.newThingIsDuplicate ? "Duplicate" : "Create";
        ImGui::BeginDisabled(inUse || editor.idInputBuf[0] == '\0');
        if (buttonWithIcon(assets, kIcons, "page_add", createLabel, ImVec2(140, 0))) {
            bool ok = false;
            if (editor.newThingIsDuplicate && sel != nullptr) {
                ok = editor.duplicateThing(sel->id, editor.idInputBuf);
            } else {
                const std::string folderPath =
                    editor.doc.folders.empty() ? "" : editor.doc.folders.front().path;
                ok = editor.createThing(editor.idInputBuf, folderPath);
            }
            if (ok) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (buttonWithIcon(assets, kIcons, "cancel", "Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

}
