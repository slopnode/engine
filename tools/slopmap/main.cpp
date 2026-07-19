#include "camera.hpp"
#include "create_tool.hpp"
#include "editor.hpp"
#include "layout.hpp"
#include "material_browser.hpp"
#include "preview.hpp"
#include "select_tool.hpp"

#include "assets/asset_store.hpp"
#include "core/package_meta.hpp"
#include "game/app_config.hpp"

#include "imgui.h"
#include "rlImGui.h"

#include <raylib.h>
#include <s7.h>

#include <cstdio>
#include <iostream>
#include <string>

namespace {

const char* modeName(slopmap::EditorMode mode) {
    return mode == slopmap::EditorMode::Create ? "Create" : "Select";
}

const char* viewName(slopmap::ViewPlane view) {
    switch (view) {
    case slopmap::ViewPlane::Top:
        return "Top (ortho)";
    case slopmap::ViewPlane::Front:
        return "Front (ortho)";
    case slopmap::ViewPlane::Side:
        return "Side (ortho)";
    case slopmap::ViewPlane::PerspectiveY0:
    default:
        return "Perspective";
    }
}

void drawBottomStatusBar(
    const slopmap::Editor& editor,
    const slopmap::CreateTool& createTool,
    const slopmap::SelectTool& selectTool,
    float height) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - height));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("##statusbar", nullptr, flags)) {
        ImGui::Text(
            "Mode: %s  |  Map: %s%s  |  %s%s  |  grid %.3g  |  brushes %d  |  mat %s",
            modeName(editor.mode),
            editor.doc.mapName.c_str(),
            editor.doc.dirty ? "*" : "",
            viewName(editor.viewPlane),
            editor.wireframe ? " wire" : "",
            editor.gridSize,
            static_cast<int>(editor.doc.brushes.size()),
            editor.doc.defaultMaterial.c_str());

        ImGui::SameLine();
        if (editor.mode == slopmap::EditorMode::Create && createTool.active()) {
            ImGui::Text(
                "  |  create %s  thickness %.3g",
                createTool.phase == slopmap::CreatePhase::DrawingBase ? "footprint" : "extrude",
                createTool.thickness);
            ImGui::SameLine();
        }
        if (selectTool.translating) {
            const char* axis = "free";
            if (selectTool.axisLock == slopmap::TranslateAxis::X) {
                axis = "X";
            } else if (selectTool.axisLock == slopmap::TranslateAxis::Y) {
                axis = "Y";
            } else if (selectTool.axisLock == slopmap::TranslateAxis::Z) {
                axis = "Z";
            }
            ImGui::Text("  |  translate %s", axis);
            ImGui::SameLine();
        }
        if (!editor.numericBuffer.empty()) {
            ImGui::Text("  |  input %s", editor.numericBuffer.c_str());
            ImGui::SameLine();
        }
        if (!editor.statusMessage.empty()) {
            ImGui::Text("  |  %s", editor.statusMessage.c_str());
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void drawScene(
    slopmap::Editor& editor,
    const Camera3D& camera,
    const slopmap::CreateTool& createTool) {
    ClearBackground(Color{32, 34, 38, 255});
    BeginMode3D(camera);
    slopmap::drawGridY0(32.0f, editor.gridSize, Color{70, 74, 80, 255});
    DrawLine3D({-100, 0, 0}, {100, 0, 0}, Color{180, 60, 60, 255});
    DrawLine3D({0, -100, 0}, {0, 100, 0}, Color{60, 180, 60, 255});
    DrawLine3D({0, 0, -100}, {0, 0, 100}, Color{60, 60, 180, 255});
    editor.preview.draw(editor.wireframe, editor.doc.brushes, editor.doc.selectedBrush);
    if (editor.doc.selectedBrush >= 0 &&
        editor.doc.selectedBrush < static_cast<int>(editor.doc.brushes.size())) {
        const auto& brush = editor.doc.brushes[static_cast<std::size_t>(editor.doc.selectedBrush)];
        if (!editor.wireframe) {
            slopmap::drawBrushFaceOutlines(brush, slopmap::brushOutlineColor(brush, true));
        }
        if (editor.doc.scope == slopmap::SelectionScope::Face && editor.doc.selectedFace >= 0 &&
            editor.doc.selectedFace < static_cast<int>(brush.faces.size())) {
            const auto& face = brush.faces[static_cast<std::size_t>(editor.doc.selectedFace)];
            for (std::size_t i = 0; i < face.vertices.size(); ++i) {
                const Vector3& a = face.vertices[i];
                const Vector3& b = face.vertices[(i + 1) % face.vertices.size()];
                DrawLine3D(a, b, Color{80, 220, 255, 255});
            }
        }
    }
    createTool.drawPreview();
    EndMode3D();
}

} // namespace

int main(int argc, char* argv[]) {
    using namespace slopengine;

    auto config = AppConfig::parse(argc, argv);
    if (!config) {
        std::cerr << "Usage: slopmap --base-game <path> [--mod <path>]... [--map <name>]\n";
        return 1;
    }

    SetTraceLogLevel(LOG_INFO);
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1600, 900, "slopmap");
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    rlImGuiSetup(true);

    AssetStore assets(*config);
    s7_scheme* scheme = s7_init();
    if (scheme == nullptr) {
        std::cerr << "slopmap: failed to init scheme\n";
        rlImGuiShutdown();
        CloseWindow();
        return 1;
    }

    slopmap::Editor editor;
    editor.baseGamePath = config->base_game;
    if (auto meta = loadPackageMetaFile(config->base_game / "package.meta")) {
        editor.packageId = meta->id;
    }

    if (config->map) {
        if (!editor.load(assets, scheme, *config->map)) {
            std::cerr << "slopmap: failed to load map '" << *config->map << "'\n";
            s7_quit(scheme);
            rlImGuiShutdown();
            CloseWindow();
            return 1;
        }
    } else {
        editor.newMap("untitled");
    }

    slopmap::CreateTool createTool;
    slopmap::SelectTool selectTool;
    slopmap::MaterialBrowser materialBrowser;
    materialBrowser.rescan(assets);
    bool previewNeedsRebuild = false;
    char mapNameBuf[128] = {};
    RenderTexture2D contentTarget{};

    while (!editor.quitConfirmed) {
        if (WindowShouldClose()) {
            if (editor.doc.dirty) {
                editor.showQuitModal = true;
            } else {
                break;
            }
        }

        rlImGuiBegin();
        ImGuiIO& io = ImGui::GetIO();
        const bool uiWantsMouse = io.WantCaptureMouse;
        const bool uiWantsKeyboard = io.WantCaptureKeyboard;
        const bool rmbFly = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);

        const float chromeHeight = ImGui::GetFrameHeight();
        const slopmap::UiLayout layout = slopmap::computeUiLayout(chromeHeight, chromeHeight);
        editor.contentViewport = layout.content;
        slopmap::ensureContentTarget(contentTarget, layout.content);

        const Vector2 mouse = GetMousePosition();
        const bool mouseInContent = slopmap::pointInRect(mouse, layout.content);
        const bool allowFly =
            !uiWantsKeyboard && !uiWantsMouse && (mouseInContent || IsCursorHidden());
        editor.camera.update(allowFly);
        const Camera3D camera = editor.camera.toRaylib();

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New", "Ctrl+N")) {
                    if (editor.doc.dirty) {
                        editor.showNewModal = true;
                        editor.modalMapName = "untitled";
                    } else {
                        createTool.reset();
                        if (selectTool.translating) {
                            selectTool.cancelTranslate(editor);
                        }
                        editor.newMap("untitled");
                        previewNeedsRebuild = true;
                    }
                }
                if (ImGui::MenuItem("Load...", "Ctrl+O")) {
                    editor.showLoadModal = true;
                    editor.modalMapName = editor.doc.mapName;
                    std::snprintf(mapNameBuf, sizeof(mapNameBuf), "%s", editor.modalMapName.c_str());
                }
                if (ImGui::MenuItem("Save", "Ctrl+S")) {
                    editor.save(assets);
                }
                if (ImGui::MenuItem("Save As...")) {
                    editor.showSaveAsModal = true;
                    editor.modalMapName = editor.doc.mapName == "untitled" ? "" : editor.doc.mapName;
                    std::snprintf(mapNameBuf, sizeof(mapNameBuf), "%s", editor.modalMapName.c_str());
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit")) {
                    if (editor.doc.dirty) {
                        editor.showQuitModal = true;
                    } else {
                        editor.quitConfirmed = true;
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Select Mode", "1", editor.mode == slopmap::EditorMode::Select)) {
                    editor.mode = slopmap::EditorMode::Select;
                    createTool.reset();
                }
                if (ImGui::MenuItem("Create Mode", "2", editor.mode == slopmap::EditorMode::Create)) {
                    if (selectTool.translating) {
                        selectTool.cancelTranslate(editor);
                    }
                    editor.mode = slopmap::EditorMode::Create;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Frame Selection", "Home", false, !editor.doc.brushes.empty())) {
                    editor.frameSelection();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Perspective", nullptr, editor.viewPlane == slopmap::ViewPlane::PerspectiveY0)) {
                    editor.setViewPlane(slopmap::ViewPlane::PerspectiveY0);
                }
                if (ImGui::MenuItem("Top Ortho", "Tab", editor.viewPlane == slopmap::ViewPlane::Top)) {
                    editor.setViewPlane(slopmap::ViewPlane::Top);
                }
                if (ImGui::MenuItem("Front Ortho", nullptr, editor.viewPlane == slopmap::ViewPlane::Front)) {
                    editor.setViewPlane(slopmap::ViewPlane::Front);
                }
                if (ImGui::MenuItem("Side Ortho", nullptr, editor.viewPlane == slopmap::ViewPlane::Side)) {
                    editor.setViewPlane(slopmap::ViewPlane::Side);
                }
                ImGui::Separator();
                ImGui::MenuItem("Wireframe", "Z", &editor.wireframe);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        if (!uiWantsKeyboard && !rmbFly) {
            if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_S)) {
                editor.save(assets);
            }
            if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_N)) {
                if (editor.doc.dirty) {
                    editor.showNewModal = true;
                } else {
                    createTool.reset();
                    editor.newMap("untitled");
                    previewNeedsRebuild = true;
                }
            }
            if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_O)) {
                editor.showLoadModal = true;
                std::snprintf(mapNameBuf, sizeof(mapNameBuf), "%s", editor.doc.mapName.c_str());
            }
            if (IsKeyPressed(KEY_ONE)) {
                editor.mode = slopmap::EditorMode::Select;
                createTool.reset();
            }
            if (IsKeyPressed(KEY_TWO)) {
                if (selectTool.translating) {
                    selectTool.cancelTranslate(editor);
                }
                editor.mode = slopmap::EditorMode::Create;
            }
            if (IsKeyPressed(KEY_TAB)) {
                editor.toggleOrthoTop();
            }
            if (IsKeyPressed(KEY_LEFT_BRACKET)) {
                editor.cycleGrid(1);
            }
            if (IsKeyPressed(KEY_RIGHT_BRACKET)) {
                editor.cycleGrid(-1);
            }
            if (IsKeyPressed(KEY_HOME)) {
                editor.frameSelection();
            }
            if (IsKeyPressed(KEY_Z) && !selectTool.translating) {
                editor.wireframe = !editor.wireframe;
            }
        }

        const bool wasTranslating = selectTool.translating;
        const bool createWasActive = createTool.active();
        const std::size_t brushCountBefore = editor.doc.brushes.size();
        const bool dirtyBefore = editor.doc.dirty;

        const bool toolActive = selectTool.translating || createTool.active();
        const bool blockTools = rmbFly || uiWantsMouse || (!mouseInContent && !toolActive);
        if (editor.mode == slopmap::EditorMode::Create) {
            createTool.update(editor, camera, blockTools, uiWantsKeyboard || rmbFly);
        } else {
            selectTool.update(editor, camera, blockTools, uiWantsKeyboard || rmbFly);
        }

        if (editor.doc.brushes.size() != brushCountBefore || editor.doc.dirty != dirtyBefore ||
            selectTool.translating || wasTranslating || createTool.active() != createWasActive) {
            previewNeedsRebuild = true;
        }
        if (previewNeedsRebuild && (!createTool.active() || selectTool.translating)) {
            editor.rebuildPreview(assets);
            previewNeedsRebuild = selectTool.translating;
        }

        BeginDrawing();
        ClearBackground(Color{28, 30, 34, 255});

        if (contentTarget.id != 0) {
            BeginTextureMode(contentTarget);
            drawScene(editor, camera, createTool);
            EndTextureMode();
            slopmap::drawContentTarget(contentTarget, layout.content);
        }

        {
            const ImGuiWindowFlags panelFlags =
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoBringToFrontOnFocus;
            ImGui::SetNextWindowPos(ImVec2(layout.leftPanel.x, layout.leftPanel.y), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(layout.leftPanel.width, layout.leftPanel.height), ImGuiCond_Always);
            if (ImGui::Begin("Brushes", nullptr, panelFlags)) {
                for (std::size_t i = 0; i < editor.doc.brushes.size(); ++i) {
                    const bool selected = static_cast<int>(i) == editor.doc.selectedBrush;
                    if (ImGui::Selectable(editor.doc.brushes[i].id.c_str(), selected)) {
                        editor.doc.selectedBrush = static_cast<int>(i);
                        editor.doc.selectedFace = -1;
                    }
                }
            }
            ImGui::End();
        }

        {
            const slopmap::MaterialBrowserResult matResult = materialBrowser.draw(
                editor,
                layout.rightPanel.x,
                layout.rightPanel.y,
                layout.rightPanel.width,
                layout.rightPanel.height);
            if (matResult.requestRescan) {
                materialBrowser.rescan(assets);
            }
            if (matResult.applied) {
                editor.rebuildPreview(assets);
            }
        }

        if (editor.showLoadModal) {
            ImGui::OpenPopup("Load Map");
            editor.showLoadModal = false;
        }
        if (ImGui::BeginPopupModal("Load Map", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Map folder name under maps/");
            ImGui::InputText("##loadmap", mapNameBuf, sizeof(mapNameBuf));
            if (ImGui::Button("Load", ImVec2(120, 0))) {
                if (editor.doc.dirty) {
                    editor.statusMessage = "Save or discard changes before loading";
                } else {
                    createTool.reset();
                    if (selectTool.translating) {
                        selectTool.cancelTranslate(editor);
                    }
                    if (editor.load(assets, scheme, mapNameBuf)) {
                        previewNeedsRebuild = false;
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (editor.showSaveAsModal) {
            ImGui::OpenPopup("Save Map As");
            editor.showSaveAsModal = false;
        }
        if (ImGui::BeginPopupModal("Save Map As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Map folder name under maps/");
            ImGui::InputText("##saveas", mapNameBuf, sizeof(mapNameBuf));
            if (ImGui::Button("Save", ImVec2(120, 0))) {
                if (editor.saveAs(assets, mapNameBuf)) {
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (editor.showNewModal) {
            ImGui::OpenPopup("New Map");
            editor.showNewModal = false;
        }
        if (ImGui::BeginPopupModal("New Map", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Discard unsaved changes and create a new map?");
            if (ImGui::Button("Discard & New", ImVec2(140, 0))) {
                createTool.reset();
                if (selectTool.translating) {
                    selectTool.cancelTranslate(editor);
                }
                editor.newMap("untitled");
                previewNeedsRebuild = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (editor.showQuitModal) {
            ImGui::OpenPopup("Unsaved changes");
            editor.showQuitModal = false;
        }
        if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Map has unsaved changes. Quit anyway?");
            if (ImGui::Button("Quit", ImVec2(120, 0))) {
                editor.quitConfirmed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        drawBottomStatusBar(editor, createTool, selectTool, layout.statusHeight);

        rlImGuiEnd();
        EndDrawing();
    }

    if (IsCursorHidden()) {
        EnableCursor();
    }
    if (contentTarget.id != 0) {
        UnloadRenderTexture(contentTarget);
    }
    editor.preview.clear();
    s7_quit(scheme);
    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
