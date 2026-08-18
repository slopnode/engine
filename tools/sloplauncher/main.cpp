#include "development_tab.hpp"
#include "game_tab.hpp"
#include "launcher_state.hpp"
#include "packages_panel.hpp"

#include "imgui.h"
#include "rlImGui.h"

#include <raylib.h>

int main() {
    SetTraceLogLevel(LOG_INFO);
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1100, 720, "slopengine launcher");
    if (!IsWindowReady()) {
        return 1;
    }
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    rlImGuiSetup(true);

    sloplauncher::LauncherState state;
    state.init();

    bool quit = false;
    while (!quit) {
        if (WindowShouldClose()) {
            quit = true;
        }

        rlImGuiBegin();

        const float screenW = static_cast<float>(GetScreenWidth());
        const float screenH = static_cast<float>(GetScreenHeight());
        const float leftWidth = 420.0f;
        const float rightWidth = screenW - leftWidth;
        const float statusHeight = ImGui::GetFrameHeightWithSpacing();
        const float bodyHeight = screenH - statusHeight;

        const ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollWithMouse;

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(leftWidth, bodyHeight));
        ImGui::Begin("Packages", nullptr, panelFlags);
        sloplauncher::drawPackagesPanel(state);
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(leftWidth, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(rightWidth, bodyHeight));
        ImGui::Begin("##right", nullptr, panelFlags | ImGuiWindowFlags_NoTitleBar);
        if (ImGui::BeginTabBar("##launcherTabs")) {
            if (ImGui::BeginTabItem("Game")) {
                sloplauncher::drawGameTab(state);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Development")) {
                sloplauncher::drawDevelopmentTab(state);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(0.0f, bodyHeight));
        ImGui::SetNextWindowSize(ImVec2(screenW, statusHeight));
        ImGui::Begin(
            "##status",
            nullptr,
            panelFlags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollWithMouse);
        if (!state.statusMessage.empty()) {
            const ImVec4 color = state.statusIsError
                ? ImVec4(0.9f, 0.35f, 0.35f, 1.0f)
                : ImVec4(0.4f, 0.85f, 0.45f, 1.0f);
            ImGui::TextColored(color, "%s", state.statusMessage.c_str());
        }
        ImGui::End();

        BeginDrawing();
        ClearBackground(Color{24, 24, 28, 255});
        rlImGuiEnd();
        EndDrawing();
    }

    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
