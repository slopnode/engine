#include "camera.hpp"
#include "create_tool.hpp"
#include "editor.hpp"
#include "layout.hpp"
#include "material_browser.hpp"
#include "place_tool.hpp"
#include "prefab_browser.hpp"
#include "preview.hpp"
#include "select_tool.hpp"

#include "assets/asset_store.hpp"
#include "core/package_meta.hpp"
#include "game/app_config.hpp"

#include "imgui.h"
#include "rlImGui.h"

#include <raylib.h>
#include <s7.h>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>

namespace {

void drawScene(
    slopmap::Editor& editor,
    const Camera3D& camera,
    const slopmap::CreateTool& createTool) {
    ClearBackground(Color{32, 34, 38, 255});
    BeginMode3D(camera);
    const Vector3 eye = camera.position;
    const float lineWidth = std::max(0.015f, editor.gridSize * 0.02f);
    const float axisWidth = lineWidth * 1.5f;
    slopmap::drawGridY0(32.0f, editor.gridSize, Color{70, 74, 80, 255}, eye, lineWidth);
    slopmap::drawThickLine3D({-100, 0, 0}, {100, 0, 0}, Color{180, 60, 60, 255}, axisWidth, eye);
    slopmap::drawThickLine3D({0, -100, 0}, {0, 100, 0}, Color{60, 180, 60, 255}, axisWidth, eye);
    slopmap::drawThickLine3D({0, 0, -100}, {0, 0, 100}, Color{60, 60, 180, 255}, axisWidth, eye);

    const slopmap::EditorDocument& d = editor.doc();
    const int selectedBrush =
        d.selection == slopmap::SelectionTarget::Brush ? d.selectedBrush : -1;
    editor.preview.draw(editor.wireframe, d.brushes, selectedBrush, eye, lineWidth);
    if (editor.wireframe) {
        for (std::size_t i = 0; i < editor.expandedInstanceBrushes.size(); ++i) {
            const bool selected = d.selection == slopmap::SelectionTarget::Instance &&
                editor.expandedInstanceOwners[i] == d.selectedInstance;
            slopmap::drawBrushFaceOutlines(
                editor.expandedInstanceBrushes[i],
                slopmap::brushOutlineColor(editor.expandedInstanceBrushes[i], selected),
                eye,
                lineWidth);
        }
    }

    if (d.selection == slopmap::SelectionTarget::Brush && d.selectedBrush >= 0 &&
        d.selectedBrush < static_cast<int>(d.brushes.size())) {
        const auto& brush = d.brushes[static_cast<std::size_t>(d.selectedBrush)];
        if (!editor.wireframe) {
            slopmap::drawBrushFaceOutlines(
                brush,
                slopmap::brushOutlineColor(brush, true),
                eye,
                lineWidth);
        }
        if (d.scope == slopmap::SelectionScope::Face && d.selectedFace >= 0 &&
            d.selectedFace < static_cast<int>(brush.faces.size())) {
            const auto& face = brush.faces[static_cast<std::size_t>(d.selectedFace)];
            const Color faceColor =
                face.uvLock ? Color{255, 200, 80, 255} : Color{80, 220, 255, 255};
            for (std::size_t i = 0; i < face.vertices.size(); ++i) {
                const Vector3& a = face.vertices[i];
                const Vector3& b = face.vertices[(i + 1) % face.vertices.size()];
                slopmap::drawThickLine3D(a, b, faceColor, lineWidth * 1.25f, eye);
            }
        }
    }

    if (!editor.wireframe && d.selection == slopmap::SelectionTarget::Instance &&
        d.selectedInstance >= 0) {
        for (std::size_t i = 0; i < editor.expandedInstanceBrushes.size(); ++i) {
            if (editor.expandedInstanceOwners[i] != d.selectedInstance) {
                continue;
            }
            slopmap::drawBrushFaceOutlines(
                editor.expandedInstanceBrushes[i],
                Color{255, 140, 40, 255},
                eye,
                lineWidth);
        }
    }

    createTool.drawPreview();
    EndMode3D();
}

void cancelTools(slopmap::CreateTool& createTool, slopmap::SelectTool& selectTool, slopmap::Editor& editor) {
    createTool.reset();
    if (selectTool.translating) {
        selectTool.cancelTranslate(editor);
    }
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
    editor.scheme = scheme;
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
    slopmap::PlaceTool placeTool;
    slopmap::MaterialBrowser materialBrowser;
    slopmap::PrefabBrowser prefabBrowser;
    materialBrowser.rescan(assets);
    prefabBrowser.rescan(assets);
    bool previewNeedsRebuild = false;
    char mapNameBuf[128] = {};
    char prefabPathBuf[256] = {};
    RenderTexture2D contentTarget{};

    while (!editor.quitConfirmed) {
        if (WindowShouldClose()) {
            if (editor.doc().dirty ||
                (editor.scene == slopmap::EditorScene::Level && editor.prefabDoc.dirty) ||
                (editor.scene == slopmap::EditorScene::Prefab && editor.levelDoc.dirty)) {
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
        const slopmap::UiLayout layout = slopmap::computeUiLayout(chromeHeight, 0.0f);
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
                if (ImGui::MenuItem("New Map", "Ctrl+N")) {
                    if (editor.scene != slopmap::EditorScene::Level) {
                        editor.switchScene(slopmap::EditorScene::Level);
                    }
                    if (editor.levelDoc.dirty) {
                        editor.showNewModal = true;
                        editor.modalMapName = "untitled";
                    } else {
                        cancelTools(createTool, selectTool, editor);
                        editor.newMap("untitled");
                        previewNeedsRebuild = true;
                    }
                }
                if (ImGui::MenuItem("Load Map...", "Ctrl+O")) {
                    if (editor.scene != slopmap::EditorScene::Level && !editor.switchScene(slopmap::EditorScene::Level)) {
                        // dirty prompt
                    } else {
                        editor.showLoadModal = true;
                        editor.modalMapName = editor.levelDoc.assetPath;
                        std::snprintf(mapNameBuf, sizeof(mapNameBuf), "%s", editor.modalMapName.c_str());
                    }
                }
                if (ImGui::MenuItem("Save", "Ctrl+S")) {
                    if (editor.save(assets)) {
                        if (editor.scene == slopmap::EditorScene::Prefab) {
                            prefabBrowser.rescan(assets);
                        }
                    }
                }
                if (ImGui::MenuItem("Save Map As...", nullptr, false, editor.scene == slopmap::EditorScene::Level)) {
                    editor.showSaveAsModal = true;
                    editor.modalMapName =
                        editor.levelDoc.assetPath == "untitled" ? "" : editor.levelDoc.assetPath;
                    std::snprintf(mapNameBuf, sizeof(mapNameBuf), "%s", editor.modalMapName.c_str());
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit")) {
                    if (editor.levelDoc.dirty || editor.prefabDoc.dirty) {
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
                if (ImGui::MenuItem(
                        "Place Mode",
                        "3",
                        editor.mode == slopmap::EditorMode::Place,
                        editor.scene == slopmap::EditorScene::Level)) {
                    if (selectTool.translating) {
                        selectTool.cancelTranslate(editor);
                    }
                    createTool.reset();
                    editor.mode = slopmap::EditorMode::Place;
                }
                ImGui::Separator();
                if (ImGui::MenuItem(
                        "Toggle Brush Role",
                        "H",
                        false,
                        editor.doc().selection == slopmap::SelectionTarget::Brush)) {
                    editor.toggleSelectedBrushRole();
                    previewNeedsRebuild = true;
                }
                if (ImGui::MenuItem(
                        "Toggle UV Lock",
                        "L",
                        false,
                        editor.doc().selection == slopmap::SelectionTarget::Brush)) {
                    selectTool.toggleSelectedUvLock(editor);
                    previewNeedsRebuild = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem(
                        "Frame Selection",
                        "Home",
                        false,
                        !editor.doc().brushes.empty() || !editor.doc().instances.empty())) {
                    editor.frameSelection();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Prefab")) {
                if (ImGui::MenuItem("New Prefab")) {
                    editor.modalPrefabPath.clear();
                    if (editor.prefabDoc.dirty && editor.scene == slopmap::EditorScene::Prefab) {
                        editor.pendingScene = slopmap::EditorScene::Prefab;
                        editor.showSwitchSceneModal = true;
                    } else if (editor.scene == slopmap::EditorScene::Level && editor.doc().dirty) {
                        editor.pendingScene = slopmap::EditorScene::Prefab;
                        editor.showSwitchSceneModal = true;
                    } else {
                        cancelTools(createTool, selectTool, editor);
                        editor.newPrefab();
                        previewNeedsRebuild = true;
                    }
                }
                if (ImGui::MenuItem("Open Prefab...")) {
                    editor.showOpenPrefabModal = true;
                    editor.modalPrefabPath = editor.placePrefabPath;
                    std::snprintf(
                        prefabPathBuf,
                        sizeof(prefabPathBuf),
                        "%s",
                        editor.modalPrefabPath.c_str());
                }
                if (ImGui::MenuItem("Save Prefab", nullptr, false, editor.scene == slopmap::EditorScene::Prefab)) {
                    if (editor.savePrefab(assets)) {
                        prefabBrowser.rescan(assets);
                    }
                }
                if (ImGui::MenuItem(
                        "Save Prefab As...",
                        nullptr,
                        false,
                        editor.scene == slopmap::EditorScene::Prefab)) {
                    editor.showSavePrefabAsModal = true;
                    editor.modalPrefabPath = editor.prefabDoc.assetPath;
                    std::snprintf(
                        prefabPathBuf,
                        sizeof(prefabPathBuf),
                        "%s",
                        editor.modalPrefabPath.c_str());
                }
                ImGui::Separator();
                if (ImGui::MenuItem(
                        "Back to Level",
                        nullptr,
                        false,
                        editor.scene == slopmap::EditorScene::Prefab)) {
                    if (editor.switchScene(slopmap::EditorScene::Level)) {
                        cancelTools(createTool, selectTool, editor);
                        editor.rebuildPreview(assets);
                        previewNeedsRebuild = false;
                    }
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
                if (editor.save(assets) && editor.scene == slopmap::EditorScene::Prefab) {
                    prefabBrowser.rescan(assets);
                }
            }
            if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_N)) {
                if (editor.scene != slopmap::EditorScene::Level) {
                    editor.switchScene(slopmap::EditorScene::Level);
                }
                if (editor.levelDoc.dirty) {
                    editor.showNewModal = true;
                } else {
                    cancelTools(createTool, selectTool, editor);
                    editor.newMap("untitled");
                    previewNeedsRebuild = true;
                }
            }
            if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_O)) {
                editor.showLoadModal = true;
                std::snprintf(mapNameBuf, sizeof(mapNameBuf), "%s", editor.levelDoc.assetPath.c_str());
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
            if (IsKeyPressed(KEY_THREE) && editor.scene == slopmap::EditorScene::Level) {
                if (selectTool.translating) {
                    selectTool.cancelTranslate(editor);
                }
                createTool.reset();
                editor.mode = slopmap::EditorMode::Place;
            }
            if (IsKeyPressed(KEY_H) && editor.doc().selection == slopmap::SelectionTarget::Brush) {
                editor.toggleSelectedBrushRole();
                previewNeedsRebuild = true;
            }
            if (IsKeyPressed(KEY_L) && editor.doc().selection == slopmap::SelectionTarget::Brush) {
                selectTool.toggleSelectedUvLock(editor);
                previewNeedsRebuild = true;
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
        const std::size_t brushCountBefore = editor.doc().brushes.size();
        const std::size_t instanceCountBefore = editor.doc().instances.size();
        const bool dirtyBefore = editor.doc().dirty;

        const bool toolActive = selectTool.translating || createTool.active();
        const bool blockTools = rmbFly || uiWantsMouse || (!mouseInContent && !toolActive);
        if (editor.mode == slopmap::EditorMode::Create) {
            createTool.update(editor, camera, blockTools, uiWantsKeyboard || rmbFly);
        } else if (editor.mode == slopmap::EditorMode::Place) {
            placeTool.update(editor, assets, camera, blockTools, uiWantsKeyboard || rmbFly);
        } else {
            selectTool.update(editor, assets, camera, blockTools, uiWantsKeyboard || rmbFly);
        }

        if (editor.doc().brushes.size() != brushCountBefore ||
            editor.doc().instances.size() != instanceCountBefore ||
            editor.doc().dirty != dirtyBefore || selectTool.translating || wasTranslating ||
            createTool.active() != createWasActive) {
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
            if (ImGui::Begin("Scene", nullptr, panelFlags)) {
                slopmap::EditorDocument& d = editor.doc();
                ImGui::TextUnformatted("Brushes");
                if (ImGui::BeginChild("##brushes", ImVec2(0, layout.leftPanel.height * 0.45f), ImGuiChildFlags_Borders)) {
                    for (std::size_t i = 0; i < d.brushes.size(); ++i) {
                        const bool selected = d.selection == slopmap::SelectionTarget::Brush &&
                            static_cast<int>(i) == d.selectedBrush;
                        const char* role =
                            d.brushes[i].role == slopengine::BrushRole::Detail ? " [D]" : " [H]";
                        const std::string label = d.brushes[i].id + role;
                        if (ImGui::Selectable(label.c_str(), selected)) {
                            d.selection = slopmap::SelectionTarget::Brush;
                            d.selectedBrush = static_cast<int>(i);
                            d.selectedFace = -1;
                            d.selectedInstance = -1;
                        }
                    }
                }
                ImGui::EndChild();

                ImGui::TextUnformatted("Prefab instances");
                if (ImGui::BeginChild("##instances", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
                    for (std::size_t i = 0; i < d.instances.size(); ++i) {
                        const bool selected = d.selection == slopmap::SelectionTarget::Instance &&
                            static_cast<int>(i) == d.selectedInstance;
                        const std::string label = d.instances[i].id + " (" + d.instances[i].path + ")";
                        if (ImGui::Selectable(label.c_str(), selected)) {
                            d.selection = slopmap::SelectionTarget::Instance;
                            d.selectedInstance = static_cast<int>(i);
                            d.selectedBrush = -1;
                            d.selectedFace = -1;
                        }
                    }
                }
                ImGui::EndChild();
            }
            ImGui::End();
        }

        {
            const float halfH = layout.rightPanel.height * 0.5f;
            const slopmap::MaterialBrowserResult matResult = materialBrowser.draw(
                editor,
                layout.rightPanel.x,
                layout.rightPanel.y,
                layout.rightPanel.width,
                halfH);
            if (matResult.requestRescan) {
                materialBrowser.rescan(assets);
            }
            if (matResult.applied) {
                editor.rebuildPreview(assets);
            }

            const slopmap::PrefabBrowserResult prefabResult = prefabBrowser.draw(
                editor,
                layout.rightPanel.x,
                layout.rightPanel.y + halfH,
                layout.rightPanel.width,
                layout.rightPanel.height - halfH);
            if (prefabResult.requestRescan) {
                prefabBrowser.rescan(assets);
            }
            if (prefabResult.selected && editor.scene == slopmap::EditorScene::Level) {
                editor.mode = slopmap::EditorMode::Place;
                createTool.reset();
            }
            if (prefabResult.openRequested) {
                if (editor.scene == slopmap::EditorScene::Prefab && editor.prefabDoc.dirty) {
                    editor.showOpenPrefabModal = true;
                    std::snprintf(
                        prefabPathBuf,
                        sizeof(prefabPathBuf),
                        "%s",
                        editor.placePrefabPath.c_str());
                } else if (editor.scene == slopmap::EditorScene::Level && editor.levelDoc.dirty) {
                    editor.pendingScene = slopmap::EditorScene::Prefab;
                    editor.showSwitchSceneModal = true;
                    editor.modalPrefabPath = editor.placePrefabPath;
                } else {
                    cancelTools(createTool, selectTool, editor);
                    if (editor.loadPrefab(assets, scheme, editor.placePrefabPath)) {
                        previewNeedsRebuild = false;
                    }
                }
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
                if (editor.levelDoc.dirty) {
                    editor.statusMessage = "Save or discard changes before loading";
                } else {
                    cancelTools(createTool, selectTool, editor);
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

        if (editor.showOpenPrefabModal) {
            ImGui::OpenPopup("Open Prefab");
            editor.showOpenPrefabModal = false;
        }
        if (ImGui::BeginPopupModal("Open Prefab", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Prefab virtual path under prefabs/ (e.g. furniture/desk)");
            ImGui::InputText("##openprefab", prefabPathBuf, sizeof(prefabPathBuf));
            if (ImGui::Button("Open", ImVec2(120, 0))) {
                if (editor.prefabDoc.dirty && editor.scene == slopmap::EditorScene::Prefab) {
                    editor.statusMessage = "Save or discard prefab changes first";
                } else {
                    cancelTools(createTool, selectTool, editor);
                    if (editor.loadPrefab(assets, scheme, prefabPathBuf)) {
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

        if (editor.showSavePrefabAsModal) {
            ImGui::OpenPopup("Save Prefab As");
            editor.showSavePrefabAsModal = false;
        }
        if (ImGui::BeginPopupModal("Save Prefab As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Prefab virtual path under prefabs/ (e.g. furniture/desk)");
            ImGui::InputText("##saveprefab", prefabPathBuf, sizeof(prefabPathBuf));
            if (ImGui::Button("Save", ImVec2(120, 0))) {
                if (editor.savePrefabAs(assets, prefabPathBuf)) {
                    prefabBrowser.rescan(assets);
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
            ImGui::TextUnformatted("Discard unsaved level changes and create a new map?");
            if (ImGui::Button("Discard & New", ImVec2(140, 0))) {
                cancelTools(createTool, selectTool, editor);
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

        if (editor.showSwitchSceneModal) {
            ImGui::OpenPopup("Switch Scene");
            editor.showSwitchSceneModal = false;
        }
        if (ImGui::BeginPopupModal("Switch Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Discard unsaved changes in the current scene?");
            if (ImGui::Button("Discard & Switch", ImVec2(160, 0))) {
                cancelTools(createTool, selectTool, editor);
                if (editor.pendingScene == slopmap::EditorScene::Prefab &&
                    editor.scene == slopmap::EditorScene::Level &&
                    !editor.modalPrefabPath.empty()) {
                    editor.levelDoc.dirty = false;
                    if (editor.loadPrefab(assets, scheme, editor.modalPrefabPath)) {
                        previewNeedsRebuild = false;
                    }
                } else if (editor.pendingScene == slopmap::EditorScene::Prefab) {
                    editor.doc().dirty = false;
                    editor.newPrefab();
                    previewNeedsRebuild = true;
                } else {
                    editor.doc().dirty = false;
                    editor.switchScene(editor.pendingScene, true);
                    editor.rebuildPreview(assets);
                    previewNeedsRebuild = false;
                }
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
            ImGui::TextUnformatted("There are unsaved changes. Quit anyway?");
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
