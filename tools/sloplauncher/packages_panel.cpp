#include "packages_panel.hpp"

#include "core/package_search.hpp"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>

namespace sloplauncher {

namespace {

bool containsIgnoreCase(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    std::string h(haystack.size(), '\0');
    std::string n(needle.size(), '\0');
    std::transform(haystack.begin(), haystack.end(), h.begin(), lower);
    std::transform(needle.begin(), needle.end(), n.begin(), lower);
    return h.find(n) != std::string::npos;
}

void drawSearchPaths(LauncherState& state) {
    ImGui::TextDisabled("Load Directories");
    ImGui::Separator();

    std::size_t removeIndex = static_cast<std::size_t>(-1);
    for (std::size_t i = 0; i < state.searchPaths.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.30f, 0.30f, 1.0f));
        if (ImGui::SmallButton("x")) {
            removeIndex = i;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextWrapped("%s", state.searchPaths[i].string().c_str());
        ImGui::PopID();
    }
    if (removeIndex != static_cast<std::size_t>(-1)) {
        state.removeSearchPath(removeIndex);
    }

    ImGui::Spacing();

    static char inputBuf[512] = {};
    std::snprintf(inputBuf, sizeof(inputBuf), "%s", state.newSearchPathInput.c_str());
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint(
            "##newsearchpath", "/path/to/games", inputBuf, sizeof(inputBuf))) {
        state.newSearchPathInput = inputBuf;
    }
    if (ImGui::Button("Add directory", ImVec2(-1.0f, 0.0f))) {
        if (state.addSearchPath(state.newSearchPathInput)) {
            state.newSearchPathInput.clear();
        }
    }
    if (!state.searchPathError.empty()) {
        ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.35f, 1.0f), "%s", state.searchPathError.c_str());
    }

    const std::vector<std::filesystem::path> defaults = slopengine::defaultApplicationSearchPaths();
    if (!defaults.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Built-in defaults (always searched)");
        for (const std::filesystem::path& path : defaults) {
            ImGui::TextDisabled("%s", path.string().c_str());
        }
    }
}

void drawPackageList(LauncherState& state) {
    ImGui::TextDisabled("Packages");
    ImGui::Separator();

    static char filterBuf[128] = {};
    std::snprintf(filterBuf, sizeof(filterBuf), "%s", state.packageFilterText.c_str());
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##packagefilter", "Filter...", filterBuf, sizeof(filterBuf))) {
        state.packageFilterText = filterBuf;
    }
    ImGui::Spacing();

    if (state.discovered.empty()) {
        ImGui::TextDisabled("No packages found under the configured load directories.");
        return;
    }

    ImGui::BeginChild("##packagelist", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    for (const slopengine::Package& package : state.discovered) {
        const std::string& id = package.meta().id;
        const std::string label = package.meta().name.empty() ? id : package.meta().name;
        if (!containsIgnoreCase(label, state.packageFilterText)
            && !containsIgnoreCase(id, state.packageFilterText)) {
            continue;
        }

        ImGui::PushID(id.c_str());
        const bool isBase = state.baseGameId == id;
        if (ImGui::RadioButton("##base", isBase)) {
            state.setBaseGame(id);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Set as base game");
        }
        ImGui::SameLine();

        const bool isMod =
            std::find(state.modIds.begin(), state.modIds.end(), id) != state.modIds.end();
        ImGui::BeginDisabled(isBase);
        bool modChecked = isMod;
        if (ImGui::Checkbox("Mod", &modChecked)) {
            state.toggleMod(id);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();

        ImGui::Text("%s", label.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(%s %s)", id.c_str(), package.meta().version.c_str());

        ImGui::PopID();
    }
    ImGui::EndChild();
}

}

void drawPackagesPanel(LauncherState& state) {
    ImGui::BeginChild("##searchpaths", ImVec2(0.0f, 220.0f), ImGuiChildFlags_Borders);
    drawSearchPaths(state);
    ImGui::EndChild();

    ImGui::Spacing();

    drawPackageList(state);
}

}
