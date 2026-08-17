#include "browser_panel.hpp"
#include "editor.hpp"
#include "inspector_panel.hpp"

#include "assets/asset_store.hpp"
#include "core/package_meta.hpp"
#include "core/package_search.hpp"
#include "core/user_paths.hpp"
#include "game/app_config.hpp"
#include "map/csg_script.hpp"
#include "ui/icon_ui.hpp"
#include "ui/imgui_fonts.hpp"

#include "imgui.h"
#include "rlImGui.h"

#include <raylib.h>
#include <s7.h>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

using slopengine::beginMainMenuBar;
using slopengine::beginMenuWithIcon;
using slopengine::endMainMenuBar;
using slopengine::kDefaultIconSet;
using slopengine::menuItemWithIcon;

namespace {

struct ToolConfig {
    slopengine::AppConfig mount;
    std::filesystem::path target;
};

void printUsage() {
    std::cerr
        << "Usage: slopthing --base-game <path> [--mod <path>]... --target <path>\n"
        << "\n"
        << "  --base-game   Base game package directory (required)\n"
        << "  --mod         Additional mod package directory (repeatable)\n"
        << "  --target      Package directory whose data/things.s7 is edited\n";
}

std::optional<ToolConfig> parseArgs(int argc, char* argv[]) {
    ToolConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto needValue = [&]() -> const char* {
            if (i + 1 >= argc) {
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--base-game") {
            const char* value = needValue();
            if (value == nullptr) {
                return std::nullopt;
            }
            config.mount.base_game = value;
            continue;
        }
        if (arg == "--mod") {
            const char* value = needValue();
            if (value == nullptr) {
                return std::nullopt;
            }
            config.mount.mods.emplace_back(value);
            continue;
        }
        if (arg == "--target") {
            const char* value = needValue();
            if (value == nullptr) {
                return std::nullopt;
            }
            config.target = slopengine::resolveApplicationPackagePath(
                value,
                slopengine::applicationSearchPaths(slopengine::userConfiguredSearchPaths()));
            continue;
        }
        return std::nullopt;
    }

    if (config.mount.base_game.empty() || config.target.empty()) {
        return std::nullopt;
    }
    return config;
}

bool pathEquals(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code ec;
    const auto ca = std::filesystem::weakly_canonical(a, ec);
    if (ec) {
        return a == b;
    }
    const auto cb = std::filesystem::weakly_canonical(b, ec);
    if (ec) {
        return a == b;
    }
    return ca == cb;
}

bool targetIsMounted(const slopengine::AssetStore& assets, const std::filesystem::path& target) {
    for (const slopengine::Package& package : assets.packages()) {
        if (pathEquals(package.root(), target)) {
            return true;
        }
    }
    return false;
}

}

int main(int argc, char* argv[]) {
    const auto config = parseArgs(argc, argv);
    if (!config) {
        printUsage();
        return 1;
    }

    SetTraceLogLevel(LOG_INFO);
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1500, 880, "slopthing");
    if (!IsWindowReady()) {
        std::cerr << "slopthing: failed to initialize window\n";
        return 1;
    }
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    slopengine::AssetStore assets(config->mount);
    if (!targetIsMounted(assets, config->target)) {
        std::cerr << "slopthing: --target must be one of the mounted packages\n";
        CloseWindow();
        return 1;
    }

    ImFont* monoFont = slopengine::setupImGuiWithUiAndMonoFont(
        assets, slopengine::kDefaultUiFontPath, slopengine::kMonoUiFontPath, true);

    slopthing::Editor editor;
    editor.scheme = s7_init();
    editor.assets = &assets;
    slopengine::loadPackageMapHandlers(editor.scheme, assets);
    editor.targetRoot = config->target;
    if (auto meta = slopengine::loadPackageMetaFile(config->target / "package.meta")) {
        editor.targetPackageId = meta->id;
    }
    editor.thingsFilePath = editor.targetRoot / "data" / "things.s7";
    editor.load();

    bool quit = false;
    while (!quit) {
        if (WindowShouldClose()) {
            quit = true;
        }
        if (editor.statusTimer > 0.0f) {
            editor.statusTimer -= GetFrameTime();
            if (editor.statusTimer <= 0.0f) {
                editor.statusMessage.clear();
            }
        }

        rlImGuiBegin();

        const float chromeHeight = slopengine::mainMenuBarHeight();
        const float statusHeight = ImGui::GetFrameHeightWithSpacing();
        const float leftWidth = 360.0f;
        const float screenW = static_cast<float>(GetScreenWidth());
        const float screenH = static_cast<float>(GetScreenHeight());
        const float bodyTop = chromeHeight;
        const float bodyHeight = screenH - chromeHeight - statusHeight;

        if (beginMainMenuBar()) {
            constexpr const char* kIcons = kDefaultIconSet;
            if (beginMenuWithIcon(assets, kIcons, "folder", "File", true)) {
                if (menuItemWithIcon(assets, kIcons, "disk", "Save", "Ctrl+S")) {
                    editor.save();
                }
                if (menuItemWithIcon(assets, kIcons, "arrow_refresh", "Reload", nullptr, false, !editor.dirty)) {
                    editor.load();
                }
                ImGui::Separator();
                if (menuItemWithIcon(assets, kIcons, "door", "Quit")) {
                    quit = true;
                }
                ImGui::EndMenu();
            }
            ImGui::TextDisabled(
                "  %s%s", editor.targetPackageId.c_str(), editor.dirty ? " *" : "");
            endMainMenuBar();
        }

        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_S)) {
            editor.save();
        }

        const ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;

        ImGui::SetNextWindowPos(ImVec2(0.0f, bodyTop));
        ImGui::SetNextWindowSize(ImVec2(leftWidth, bodyHeight));
        ImGui::Begin("Browser", nullptr, panelFlags);
        slopthing::drawBrowserPanel(editor, assets, bodyHeight - ImGui::GetFrameHeightWithSpacing() * 2.5f);
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(leftWidth, bodyTop));
        ImGui::SetNextWindowSize(ImVec2(screenW - leftWidth, bodyHeight));
        ImGui::Begin("Inspector", nullptr, panelFlags | ImGuiWindowFlags_NoScrollWithMouse);
        slopthing::drawInspectorPanel(editor, assets, monoFont, bodyHeight - 8.0f);
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(0.0f, screenH - statusHeight));
        ImGui::SetNextWindowSize(ImVec2(screenW, statusHeight));
        ImGui::Begin(
            "##status",
            nullptr,
            panelFlags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::TextUnformatted(editor.thingsFilePath.string().c_str());
        if (!editor.statusMessage.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled(" — %s", editor.statusMessage.c_str());
        }
        ImGui::End();

        BeginDrawing();
        ClearBackground(Color{30, 30, 34, 255});
        rlImGuiEnd();
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
