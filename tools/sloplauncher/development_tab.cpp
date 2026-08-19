#include "development_tab.hpp"

#include "imgui.h"

namespace sloplauncher {

void drawDevelopmentTab(LauncherState& state) {
    if (state.baseGameId.empty()) {
        ImGui::TextDisabled("Select a base game to use the dev tools.");
        return;
    }

    ImGui::TextDisabled("Save target");
    ImGui::Separator();
    ImGui::TextWrapped("Which mounted package should slopmap/slopsprite/slopthing save new content into?");
    ImGui::Spacing();

    for (const std::string& id : state.mountedPackageIds()) {
        const slopengine::Package* package = state.findPackage(id);
        if (package == nullptr) {
            continue;
        }
        const std::string label =
            package->meta().name.empty() ? id : package->meta().name;
        const bool selected = state.devTargetId == id;
        ImGui::PushID(id.c_str());
        if (ImGui::RadioButton("##target", selected)) {
            state.devTargetId = id;
        }
        ImGui::SameLine();
        ImGui::Text(
            "%s%s", label.c_str(), id == state.baseGameId ? " (base game)" : " (mod)");
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Editors");
    ImGui::Spacing();

    ImGui::BeginDisabled(state.devTargetId.empty());
    if (ImGui::Button("Launch slopmap", ImVec2(-1.0f, 0.0f))) {
        state.launchDevTool("slopmap");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Map/brush editor");
    }
    if (ImGui::Button("Launch slopsprite", ImVec2(-1.0f, 0.0f))) {
        state.launchDevTool("slopsprite");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Sprite/animation editor");
    }
    if (ImGui::Button("Launch slopthing", ImVec2(-1.0f, 0.0f))) {
        state.launchDevTool("slopthing");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Thing-def (weapons/actors/pickups) editor");
    }
    ImGui::EndDisabled();
}

}
