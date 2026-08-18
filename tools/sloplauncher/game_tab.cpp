#include "game_tab.hpp"

#include "imgui.h"

#include <cstdio>

namespace sloplauncher {

void drawGameTab(LauncherState& state) {
    ImGui::TextDisabled("Profile");
    ImGui::Separator();

    if (state.baseGameId.empty()) {
        ImGui::TextDisabled("Select a base game to configure a profile.");
    } else {
        for (const std::string& profile : state.existingProfiles) {
            const bool selected = state.profileName == profile;
            if (ImGui::Selectable(profile.c_str(), selected)) {
                state.profileName = profile;
            }
        }
        ImGui::Spacing();

        static char profileBuf[128] = {};
        std::snprintf(profileBuf, sizeof(profileBuf), "%s", state.profileName.c_str());
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputTextWithHint("##profilename", "default", profileBuf, sizeof(profileBuf))) {
            state.profileName = profileBuf;
        }
    }

    ImGui::Spacing();
    ImGui::Checkbox("--debug (developer UI)", &state.debugMode);

    ImGui::Spacing();
    ImGui::TextDisabled("Options");
    ImGui::Separator();

    if (state.cliSchema.empty()) {
        ImGui::TextDisabled("No package-specific options for this game.");
    } else {
        for (const slopengine::PackageCliFlag& flag : state.cliSchema) {
            ImGui::PushID(flag.name.c_str());
            if (flag.kind == slopengine::PackageCliValueKind::Flag) {
                bool checked = state.cliFlagValues[flag.name];
                if (ImGui::Checkbox(flag.name.c_str(), &checked)) {
                    state.cliFlagValues[flag.name] = checked;
                }
            } else {
                ImGui::TextUnformatted(flag.name.c_str());
                char buf[256] = {};
                std::snprintf(buf, sizeof(buf), "%s", state.cliStringValues[flag.name].c_str());
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputText("##value", buf, sizeof(buf))) {
                    state.cliStringValues[flag.name] = buf;
                }
            }
            if (!flag.help.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", flag.help.c_str());
            }
            ImGui::PopID();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::BeginDisabled(state.baseGameId.empty());
    if (ImGui::Button("Launch", ImVec2(-1.0f, 0.0f))) {
        state.launch();
    }
    ImGui::EndDisabled();
}

}
