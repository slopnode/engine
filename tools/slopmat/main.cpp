#include "anim_panel.hpp"
#include "asset_picker.hpp"
#include "browser.hpp"
#include "editor.hpp"
#include "graph_panel.hpp"
#include "layout.hpp"
#include "material_panel.hpp"
#include "preview.hpp"
#include "thumb_cache.hpp"

#include "assets/asset_store.hpp"
#include "core/log.hpp"
#include "core/package_search.hpp"
#include "core/user_paths.hpp"
#include "game/app_config.hpp"
#include "ui/icon_ui.hpp"
#include "ui/slopui_tool.hpp"
#include "ui/slopui_widgets.hpp"

#include "imgui.h"
#include "rlImGui.h"

#include <raylib.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>

using slopengine::beginMainMenuBar;
using slopengine::beginMenuWithIcon;
using slopengine::buttonWithIcon;
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
        << "Usage: slopmat --game <path> [--mod <path>]... [--target <path>]\n"
        << "\n"
        << "  --game        Game package directory (required)\n"
        << "  --mod         Additional mod package directory (repeatable)\n"
        << "  --target      Package directory that receives .mat / .texgraph saves\n"
        << "                (defaults to --game; required when --mod is given)\n"
        << "  --verbose     Raise the trace log level to show DEBUG messages\n";
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

        if (arg == "--game") {
            const char* value = needValue();
            if (value == nullptr) {
                return std::nullopt;
            }
            config.mount.game = value;
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
                value, slopengine::applicationSearchPaths(slopengine::userConfiguredSearchPaths()));
            continue;
        }
        if (arg == "--verbose") {
            config.mount.verbose = true;
            continue;
        }
        return std::nullopt;
    }

    if (config.mount.game.empty()) {
        return std::nullopt;
    }
    if (config.target.empty()) {
        if (!config.mount.mods.empty()) {
            return std::nullopt;
        }
        config.target = slopengine::resolveApplicationPackagePath(
            config.mount.game, slopengine::applicationSearchPaths(slopengine::userConfiguredSearchPaths()));
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

bool isSavable(const slopmat::EditorDocument& doc) {
    return doc.open && doc.kind != slopmat::DocKind::Texture;
}

} // namespace

int main(int argc, char* argv[]) {
    const auto config = parseArgs(argc, argv);
    if (!config) {
        printUsage();
        return 1;
    }

    slopengine::Log::init(config->mount.verbose ? slopengine::LogLevel::Debug : slopengine::LogLevel::Info);
    slopengine::Log::addDefaultConsoleSink();
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1400, 820, "slopmat");
    if (!IsWindowReady()) {
        std::cerr << "slopmat: failed to initialize window\n";
        return 1;
    }
    MaximizeWindow();
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    slopengine::AssetStore assets(config->mount);
    if (!targetIsMounted(assets, config->target)) {
        std::cerr << "slopmat: --target must be one of the mounted packages\n";
        assets.releaseGpuResources();
        CloseWindow();
        return 1;
    }

    slopengine::installToolTheme(assets, "slopmat");

    slopmat::Editor editor;
    editor.targetRoot = config->target;

    slopmat::Browser browser;
    browser.rescan(assets);

    slopmat::AssetPicker picker;
    slopmat::PreviewView preview;
    slopmat::ThumbCache thumbs;

    bool quit = false;
    while (!quit) {
        if (WindowShouldClose()) {
            quit = true;
        }

        editor.captureUndo(ImGui::IsAnyItemActive());
        if (editor.doc.previewDirty && editor.doc.open && !ImGui::IsAnyItemActive()) {
            editor.rebuildPreview(assets);
        }
        thumbs.beginFrame();
        if (editor.statusTimer > 0.0f) {
            editor.statusTimer -= GetFrameTime();
            if (editor.statusTimer <= 0.0f) {
                editor.statusMessage.clear();
            }
        }

        rlImGuiBegin();

        const float chromeHeight = slopengine::mainMenuBarHeight();
        const float statusHeight = ImGui::GetFrameHeightWithSpacing();
        const slopmat::UiLayout layout = slopmat::computeUiLayout(chromeHeight, statusHeight);

        if (beginMainMenuBar()) {
            if (beginMenuWithIcon(assets, kDefaultIconSet, "folder", "File", true)) {
                if (menuItemWithIcon(assets, kDefaultIconSet, "palette", "New Material...", "Ctrl+N")) {
                    editor.newModalKind = slopmat::DocKind::Material;
                    editor.showNewModal = true;
                }
                if (menuItemWithIcon(assets, kDefaultIconSet, "layers", "New Texture Graph...", "Ctrl+Shift+N")) {
                    editor.newModalKind = slopmat::DocKind::TextureGraph;
                    editor.showNewModal = true;
                }
                if (menuItemWithIcon(assets, kDefaultIconSet, "film", "New Texture Animation...")) {
                    editor.newModalKind = slopmat::DocKind::TextureAnim;
                    editor.showNewModal = true;
                }
                if (menuItemWithIcon(assets, kDefaultIconSet, "disk", "Save", "Ctrl+S", false, isSavable(editor.doc))) {
                    if (editor.save(assets)) {
                        thumbs.invalidate(editor.doc.virtualPath);
                        browser.rescan(assets);
                    }
                }
                const bool canExport = editor.doc.open && !editor.doc.previews.empty() && editor.doc.kind != slopmat::DocKind::Texture;
                if (menuItemWithIcon(assets, kDefaultIconSet, "picture_save", "Export Preview as PNG", nullptr, false, canExport)) {
                    std::string written;
                    const std::string slot = editor.doc.previews[static_cast<std::size_t>(std::clamp(preview.slot, 0, static_cast<int>(editor.doc.previews.size()) - 1))].name;
                    if (editor.exportSlot(assets, slot, written)) {
                        browser.rescan(assets);
                    }
                }
                if (menuItemWithIcon(assets, kDefaultIconSet, "arrow_refresh", "Rescan")) {
                    browser.rescan(assets);
                    thumbs.clear();
                }
                ImGui::Separator();
                if (menuItemWithIcon(assets, kDefaultIconSet, "door", "Quit")) {
                    quit = true;
                }
                ImGui::EndMenu();
            }
            if (beginMenuWithIcon(assets, kDefaultIconSet, "pencil", "Edit", true)) {
                if (menuItemWithIcon(assets, kDefaultIconSet, "arrow_undo", "Undo", "Ctrl+Z", false, !editor.undoStack.empty())) {
                    editor.undo();
                }
                if (menuItemWithIcon(assets, kDefaultIconSet, "arrow_redo", "Redo", "Ctrl+Y", false, !editor.redoStack.empty())) {
                    editor.redo();
                }
                ImGui::EndMenu();
            }
            endMainMenuBar();
        }

        const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        if (ctrl && IsKeyPressed(KEY_S) && isSavable(editor.doc)) {
            if (editor.save(assets)) {
                thumbs.invalidate(editor.doc.virtualPath);
                browser.rescan(assets);
            }
        }
        if (ctrl && IsKeyPressed(KEY_Z) && !ImGui::GetIO().WantTextInput) {
            if (shift) {
                editor.redo();
            } else {
                editor.undo();
            }
        }
        if (ctrl && IsKeyPressed(KEY_Y) && !ImGui::GetIO().WantTextInput) {
            editor.redo();
        }
        if (ctrl && IsKeyPressed(KEY_N)) {
            if (shift || browser.activeTab == slopmat::DocKind::TextureGraph) {
                editor.newModalKind = slopmat::DocKind::TextureGraph;
            } else if (browser.activeTab == slopmat::DocKind::TextureAnim) {
                editor.newModalKind = slopmat::DocKind::TextureAnim;
            } else {
                editor.newModalKind = slopmat::DocKind::Material;
            }
            editor.showNewModal = true;
        }

        if (editor.showNewModal) {
            ImGui::OpenPopup("New Asset");
            editor.showNewModal = false;
        }
        if (ImGui::BeginPopupModal("New Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const bool graph = editor.newModalKind == slopmat::DocKind::TextureGraph;
            const bool anim = editor.newModalKind == slopmat::DocKind::TextureAnim;
            ImGui::Text(
                "New %s -- virtual path under %s/ (no extension)",
                slopmat::docKindLabel(editor.newModalKind),
                graph || anim ? "textures" : "materials");
            ImGui::SetNextItemWidth(360.0f);
            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::InputTextWithHint("##newpath", graph ? "e.g. walls/brick-grime" : "e.g. walls/brick", editor.newPathBuf, sizeof(editor.newPathBuf));
            if (editor.doc.dirty) {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Current document has unsaved changes.");
            }
            const char* createLabel = editor.doc.dirty ? "Discard & Create" : "Create";
            if (buttonWithIcon(assets, kDefaultIconSet, "page_add", createLabel, ImVec2(160, 0))) {
                bool created = false;
                if (graph) {
                    created = editor.newTextureGraph(editor.newPathBuf);
                } else if (anim) {
                    created = editor.newTextureAnim(editor.newPathBuf);
                } else {
                    created = editor.newMaterial(editor.newPathBuf);
                }
                if (created) {
                    editor.newPathBuf[0] = '\0';
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (buttonWithIcon(assets, kDefaultIconSet, "cancel", "Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        const ImGuiWindowFlags panelFlags =
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        const ImGuiWindowFlags scrollablePanelFlags =
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

        ImGui::SetNextWindowPos(ImVec2(layout.leftPanel.x, layout.leftPanel.y));
        ImGui::SetNextWindowSize(ImVec2(layout.leftPanel.width, layout.leftPanel.height));
        slopengine::slopuiBegin("Browser", nullptr, panelFlags);
        browser.draw(editor, assets, thumbs);
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(layout.rightPanel.x, layout.rightPanel.y));
        ImGui::SetNextWindowSize(ImVec2(layout.rightPanel.width, layout.rightPanel.height));
        slopengine::slopuiBegin("Inspector", nullptr, scrollablePanelFlags);
        if (!editor.doc.open) {
            ImGui::TextDisabled("Nothing selected.");
        } else if (editor.doc.kind == slopmat::DocKind::Material) {
            slopmat::drawMaterialInspector(editor, assets, picker, browser);
        } else if (editor.doc.kind == slopmat::DocKind::TextureGraph) {
            slopmat::drawGraphInspector(editor, assets, picker, browser);
        } else if (editor.doc.kind == slopmat::DocKind::TextureAnim) {
            slopmat::drawAnimInspector(editor, assets, picker, browser);
        } else {
            ImGui::TextDisabled("%s", editor.doc.virtualPath.c_str());
            const Texture2D texture = assets.getTexture(editor.doc.virtualPath);
            ImGui::Text("%d x %d", texture.width, texture.height);
            ImGui::TextWrapped("Textures are view-only. Reference this texture from a material or an image node of a texture graph.");
        }
        picker.draw(assets);
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(layout.content.x, layout.content.y));
        ImGui::SetNextWindowSize(ImVec2(layout.content.width, layout.content.height));
        slopengine::slopuiBegin("Preview", nullptr, panelFlags | ImGuiWindowFlags_NoTitleBar);
        slopmat::drawPreviewPanel(preview, editor, assets);
        ImGui::End();

        std::string statusLine = "slopmat";
        if (editor.doc.open) {
            statusLine += " | ";
            statusLine += slopmat::docKindLabel(editor.doc.kind);
            statusLine += " ";
            statusLine += editor.doc.virtualPath;
            if (editor.doc.dirty) {
                statusLine += "*";
            }
        }
        if (!editor.statusMessage.empty()) {
            statusLine += " | ";
            statusLine += editor.statusMessage;
        }

        ImGui::SetNextWindowPos(ImVec2(0.0f, static_cast<float>(GetScreenHeight()) - statusHeight), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(GetScreenWidth()), statusHeight), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
        if (slopengine::slopuiBegin(
                "##status",
                nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoBringToFrontOnFocus)) {
            ImGui::TextUnformatted(statusLine.c_str());
        }
        ImGui::End();
        ImGui::PopStyleVar();

        BeginDrawing();
        ClearBackground(Color{24, 24, 28, 255});
        rlImGuiEnd();
        EndDrawing();
    }

    editor.releasePreview();
    preview.worldPreview.release();
    thumbs.clear();
    assets.releaseGpuResources();
    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
