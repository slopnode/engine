#include "camera.hpp"
#include "create_tool.hpp"
#include "editor.hpp"
#include "layout.hpp"
#include "material_browser.hpp"
#include "place_tool.hpp"
#include "thing_draw.hpp"
#include "prefab_browser.hpp"
#include "preview.hpp"
#include "select_tool.hpp"

#include "map/thing.hpp"

#include "assets/asset_store.hpp"
#include "core/package_meta.hpp"
#include "game/app_config.hpp"
#include "ui/icon_ui.hpp"
#include "ui/imgui_fonts.hpp"

#include "imgui.h"
#include "rlImGui.h"

#include <raylib.h>
#include <s7.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace {

struct ToolConfig {
    slopengine::AppConfig mount;
    std::filesystem::path target;
};

void printUsage() {
    std::cerr
        << "Usage: slopmap --base-game <path> [--mod <path>]... --target <path> [--map <name>]\n"
        << "\n"
        << "  --base-game   Base game package directory (required)\n"
        << "  --mod         Additional mod package directory (repeatable)\n"
        << "  --target      Package directory that receives map/prefab saves (required)\n"
        << "  --map         Map folder name under maps/ (loads maps/<name>/static.csg)\n";
}

std::optional<ToolConfig> parseArgs(int argc, char* argv[]) {
    ToolConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto needValue = [&](const char* flag) -> const char* {
            (void)flag;
            if (i + 1 >= argc) {
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--base-game") {
            const char* value = needValue("--base-game");
            if (value == nullptr) {
                return std::nullopt;
            }
            config.mount.base_game = value;
            continue;
        }
        if (arg == "--mod") {
            const char* value = needValue("--mod");
            if (value == nullptr) {
                return std::nullopt;
            }
            config.mount.mods.emplace_back(value);
            continue;
        }
        if (arg == "--target") {
            const char* value = needValue("--target");
            if (value == nullptr) {
                return std::nullopt;
            }
            config.target = value;
            continue;
        }
        if (arg == "--map") {
            const char* value = needValue("--map");
            if (value == nullptr) {
                return std::nullopt;
            }
            config.mount.map = value;
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

struct MapStats {
    int brushes = 0;
    int hullBrushes = 0;
    int detailBrushes = 0;
    int nocollideBrushes = 0;
    int faces = 0;
    int nodrawFaces = 0;
    int uvLockedFaces = 0;
    int uniqueMaterials = 0;
    int prefabInstances = 0;
    int expandedPrefabBrushes = 0;
};

MapStats gatherMapStats(const slopmap::Editor& editor) {
    MapStats stats{};
    const slopmap::EditorDocument& d = editor.doc();
    stats.brushes = static_cast<int>(d.brushes.size());
    stats.prefabInstances = static_cast<int>(d.instances.size());
    stats.expandedPrefabBrushes = static_cast<int>(editor.expandedInstanceBrushes.size());

    std::unordered_set<std::string> materials;
    for (const slopengine::Brush& brush : d.brushes) {
        if (brush.role == slopengine::BrushRole::Hull) {
            ++stats.hullBrushes;
        } else {
            ++stats.detailBrushes;
        }
        if (brush.nocollide) {
            ++stats.nocollideBrushes;
        }
        for (const slopengine::BrushFace& face : brush.faces) {
            ++stats.faces;
            if (face.nodraw) {
                ++stats.nodrawFaces;
            }
            if (face.uvLock) {
                ++stats.uvLockedFaces;
            }
            if (!face.material.empty()) {
                materials.insert(face.material);
            }
        }
    }
    stats.uniqueMaterials = static_cast<int>(materials.size());
    return stats;
}

void drawDiagnosticsMenu(slopmap::Editor& editor, slopengine::AssetStore& assets) {
    constexpr const char* kIcons = slopengine::kDefaultIconSet;
    if (!slopengine::beginMenuWithIcon(assets, kIcons, "chart_bar", "Diagnostics")) {
        return;
    }

    const slopmap::EditorDocument& d = editor.doc();
    const MapStats stats = gatherMapStats(editor);

    ImGui::TextDisabled("Document");
    ImGui::Text("Scene: %s", editor.scene == slopmap::EditorScene::Level ? "Level" : "Prefab");
    ImGui::Text("Path: %s", d.assetPath.empty() ? "(none)" : d.assetPath.c_str());
    ImGui::Text("Dirty: %s", d.dirty ? "yes" : "no");
    ImGui::Text(
        "Mode: %s",
        editor.mode == slopmap::EditorMode::Select     ? "Select"
            : editor.mode == slopmap::EditorMode::Create ? "Create"
                                                        : "Place");

    ImGui::Separator();
    ImGui::TextDisabled("Brushes");
    ImGui::Text("Total: %d", stats.brushes);
    ImGui::Text("Hull (cutting): %d", stats.hullBrushes);
    ImGui::Text("Detail (non-cutting): %d", stats.detailBrushes);
    ImGui::Text("Nocollide: %d", stats.nocollideBrushes);

    ImGui::Separator();
    ImGui::TextDisabled("Faces");
    ImGui::Text("Total: %d", stats.faces);
    ImGui::Text("Nodraw: %d", stats.nodrawFaces);
    ImGui::Text("UV locked: %d", stats.uvLockedFaces);
    ImGui::Text("Unique materials: %d", stats.uniqueMaterials);

    ImGui::Separator();
    ImGui::TextDisabled("Prefabs");
    ImGui::Text("Instances: %d", stats.prefabInstances);
    ImGui::Text("Expanded brushes: %d", stats.expandedPrefabBrushes);

    ImGui::Separator();
    ImGui::TextDisabled("Selection");
    if (d.selection == slopmap::SelectionTarget::Brush && d.selectedBrush >= 0 &&
        d.selectedBrush < static_cast<int>(d.brushes.size())) {
        const slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(d.selectedBrush)];
        ImGui::Text("Brush: %s", brush.id.c_str());
        ImGui::Text("Role: %s", slopengine::brushRoleName(brush.role));
        ImGui::Text("Faces: %d", static_cast<int>(brush.faces.size()));
        if (d.scope == slopmap::SelectionScope::Face && d.selectedFace >= 0 &&
            d.selectedFace < static_cast<int>(brush.faces.size())) {
            const slopengine::BrushFace& face =
                brush.faces[static_cast<std::size_t>(d.selectedFace)];
            ImGui::Text("Face: %s", face.id.c_str());
            ImGui::Text("Material: %s", face.material.c_str());
        }
    } else if (
        d.selection == slopmap::SelectionTarget::Instance && d.selectedInstance >= 0 &&
        d.selectedInstance < static_cast<int>(d.instances.size())) {
        const slopengine::PrefabInstance& instance =
            d.instances[static_cast<std::size_t>(d.selectedInstance)];
        ImGui::Text("Instance: %s", instance.id.c_str());
        ImGui::Text("Prefab: %s", instance.path.c_str());
    } else if (
        d.selection == slopmap::SelectionTarget::Thing && d.selectedThing >= 0 &&
        d.selectedThing < static_cast<int>(d.things.size())) {
        const slopengine::Thing& thing =
            d.things[static_cast<std::size_t>(d.selectedThing)];
        ImGui::Text("Thing: %s", thing.id.c_str());
        ImGui::Text("Kind: %s", slopengine::thingKindName(thing.kind));
    } else {
        ImGui::TextUnformatted("None");
    }

    ImGui::EndMenu();
}

void drawScene(
    slopmap::Editor& editor,
    slopengine::AssetStore& assets,
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

    const int selectedThing =
        d.selection == slopmap::SelectionTarget::Thing ? d.selectedThing : -1;
    slopmap::drawThings(assets, d.things, selectedThing, camera);

    createTool.drawPreview();
    EndMode3D();
}

std::vector<std::string> scanPackageAssets(
    const slopengine::AssetStore& assets,
    const char* folder,
    const char* extension) {
    std::unordered_map<std::string, bool> seen;
    for (const slopengine::Package& package : assets.packages()) {
        const std::filesystem::path root = package.root() / folder;
        if (!std::filesystem::exists(root)) {
            continue;
        }
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec;
             it.increment(ec)) {
            if (ec || !it->is_regular_file()) {
                continue;
            }
            if (it->path().extension() != extension) {
                continue;
            }
            std::error_code relEc;
            std::filesystem::path relative = std::filesystem::relative(it->path(), root, relEc);
            if (relEc) {
                continue;
            }
            relative.replace_extension();
            seen[relative.generic_string()] = true;
        }
    }
    std::vector<std::string> paths;
    paths.reserve(seen.size());
    for (const auto& [path, _] : seen) {
        paths.push_back(path);
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

void beginThingKind(
    slopmap::Editor& editor,
    slopengine::ThingKind kind,
    slopmap::CreateTool& createTool) {
    editor.placeTarget = slopmap::PlaceTarget::Thing;
    editor.placeThingKind = kind;
    editor.placePrefabPath.clear();
    editor.mode = slopmap::EditorMode::Place;
    createTool.reset();
    if (slopengine::thingKindNeedsPresentation(kind) && editor.placeSpritePath.empty() &&
        editor.placeGeoPath.empty()) {
        editor.statusMessage = std::string("Place ") + slopengine::thingKindName(kind) +
            ": select a sprite or geo below, then click the viewport";
    } else {
        editor.statusMessage =
            std::string("Place ") + slopengine::thingKindName(kind) + ": click the viewport";
    }
}

void armPresentationAsset(
    slopmap::Editor& editor,
    slopmap::CreateTool& createTool,
    bool sprite,
    const std::string& path) {
    if (sprite) {
        editor.placeSpritePath = path;
        editor.placeGeoPath.clear();
    } else {
        editor.placeGeoPath = path;
        editor.placeSpritePath.clear();
    }
    if (!editor.placeThingKind.has_value() ||
        !slopengine::thingKindNeedsPresentation(*editor.placeThingKind)) {
        editor.placeThingKind = slopengine::ThingKind::Prop;
    }
    editor.placeTarget = slopmap::PlaceTarget::Thing;
    editor.placePrefabPath.clear();
    editor.mode = slopmap::EditorMode::Place;
    createTool.reset();
    editor.statusMessage = std::string("Place ") +
        slopengine::thingKindName(*editor.placeThingKind) + ": click the viewport (" + path +
        ")";
}

void cancelTools(slopmap::CreateTool& createTool, slopmap::SelectTool& selectTool, slopmap::Editor& editor) {
    createTool.reset();
    if (selectTool.translating) {
        selectTool.cancelTranslate(editor);
    }
}

void drawWritePackagePicker(slopmap::Editor& editor, const slopengine::AssetStore& assets) {
    const auto& packages = assets.packages();
    if (packages.size() <= 1) {
        return;
    }

    int current = 0;
    for (std::size_t i = 0; i < packages.size(); ++i) {
        if (packages[i].root() == editor.writePackageRoot) {
            current = static_cast<int>(i);
            break;
        }
    }

    const std::string& previewId = packages[static_cast<std::size_t>(current)].meta().id;
    const char* preview =
        !previewId.empty() ? previewId.c_str() : packages[static_cast<std::size_t>(current)].root().c_str();
    ImGui::TextUnformatted("Save to package");
    if (ImGui::BeginCombo("##writepackage", preview)) {
        for (std::size_t i = 0; i < packages.size(); ++i) {
            const slopengine::Package& package = packages[i];
            const std::string& id = package.meta().id;
            const char* label = !id.empty() ? id.c_str() : package.root().c_str();
            const bool selected = static_cast<int>(i) == current;
            if (ImGui::Selectable(label, selected)) {
                editor.writePackageRoot = package.root();
                editor.writePackageId = id;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

} // namespace

int main(int argc, char* argv[]) {
    using namespace slopengine;

    const auto config = parseArgs(argc, argv);
    if (!config) {
        printUsage();
        return 1;
    }

    SetTraceLogLevel(LOG_INFO);
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1600, 900, "slopmap");
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    AssetStore assets(config->mount);
    if (!targetIsMounted(assets, config->target)) {
        std::cerr << "slopmap: --target must be one of the mounted packages\n";
        CloseWindow();
        return 1;
    }
    setupImGuiWithUiFont(assets, kDefaultUiFontPath, true);

    s7_scheme* scheme = s7_init();
    if (scheme == nullptr) {
        std::cerr << "slopmap: failed to init scheme\n";
        rlImGuiShutdown();
        CloseWindow();
        return 1;
    }

    slopmap::Editor editor;
    editor.writePackageRoot = config->target;
    editor.scheme = scheme;
    if (auto meta = loadPackageMetaFile(config->target / "package.meta")) {
        editor.writePackageId = meta->id;
    }

    if (config->mount.map) {
        if (!editor.load(assets, scheme, *config->mount.map)) {
            std::cerr << "slopmap: failed to load map '" << *config->mount.map << "'\n";
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
        const float statusHeight = ImGui::GetFrameHeightWithSpacing();
        const slopmap::UiLayout layout = slopmap::computeUiLayout(chromeHeight, statusHeight);
        editor.contentViewport = layout.content;
        slopmap::ensureContentTarget(contentTarget, layout.content);

        const Vector2 mouse = GetMousePosition();
        const bool mouseInContent = slopmap::pointInRect(mouse, layout.content);
        const bool allowFly =
            !uiWantsKeyboard && !uiWantsMouse && (mouseInContent || IsCursorHidden());
        editor.camera.update(allowFly);
        const Camera3D camera = editor.camera.toRaylib();

        if (ImGui::BeginMainMenuBar()) {
            constexpr const char* kIcons = kDefaultIconSet;
            if (beginMenuWithIcon(assets, kIcons, "folder", "File")) {
                if (menuItemWithIcon(assets, kIcons, "page_add", "New Map", "Ctrl+N")) {
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
                if (menuItemWithIcon(assets, kIcons, "folder_page", "Load Map...", "Ctrl+O")) {
                    if (editor.scene != slopmap::EditorScene::Level && !editor.switchScene(slopmap::EditorScene::Level)) {
                        // dirty prompt
                    } else {
                        editor.showLoadModal = true;
                        editor.modalMapName = editor.levelDoc.assetPath;
                        std::snprintf(mapNameBuf, sizeof(mapNameBuf), "%s", editor.modalMapName.c_str());
                    }
                }
                if (menuItemWithIcon(assets, kIcons, "disk", "Save", "Ctrl+S")) {
                    if (editor.save(assets)) {
                        if (editor.scene == slopmap::EditorScene::Prefab) {
                            prefabBrowser.rescan(assets);
                        }
                    }
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "disk_multiple",
                        "Save Map As...",
                        nullptr,
                        false,
                        editor.scene == slopmap::EditorScene::Level)) {
                    editor.showSaveAsModal = true;
                    editor.modalMapName =
                        editor.levelDoc.assetPath == "untitled" ? "" : editor.levelDoc.assetPath;
                    std::snprintf(mapNameBuf, sizeof(mapNameBuf), "%s", editor.modalMapName.c_str());
                }
                ImGui::Separator();
                if (menuItemWithIcon(assets, kIcons, "door", "Quit")) {
                    if (editor.levelDoc.dirty || editor.prefabDoc.dirty) {
                        editor.showQuitModal = true;
                    } else {
                        editor.quitConfirmed = true;
                    }
                }
                ImGui::EndMenu();
            }
            if (beginMenuWithIcon(assets, kIcons, "pencil", "Edit")) {
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "cursor",
                        "Select Mode",
                        "1",
                        editor.mode == slopmap::EditorMode::Select)) {
                    editor.mode = slopmap::EditorMode::Select;
                    createTool.reset();
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "shape_square",
                        "Create Mode",
                        "2",
                        editor.mode == slopmap::EditorMode::Create)) {
                    if (selectTool.translating) {
                        selectTool.cancelTranslate(editor);
                    }
                    editor.mode = slopmap::EditorMode::Create;
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "package",
                        "Place Mode",
                        "3",
                        editor.mode == slopmap::EditorMode::Place,
                        editor.scene == slopmap::EditorScene::Level)) {
                    if (selectTool.translating) {
                        selectTool.cancelTranslate(editor);
                    }
                    createTool.reset();
                    editor.mode = slopmap::EditorMode::Place;
                    if (editor.placeThingKind.has_value()) {
                        editor.placeTarget = slopmap::PlaceTarget::Thing;
                    }
                }
                ImGui::Separator();
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "brick",
                        "Toggle Brush Role",
                        "H",
                        false,
                        editor.doc().selection == slopmap::SelectionTarget::Brush)) {
                    editor.toggleSelectedBrushRole();
                    previewNeedsRebuild = true;
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "lock",
                        "Toggle UV Lock",
                        "L",
                        false,
                        editor.doc().selection == slopmap::SelectionTarget::Brush)) {
                    selectTool.toggleSelectedUvLock(editor);
                    previewNeedsRebuild = true;
                }
                ImGui::Separator();
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "magnifier",
                        "Frame Selection",
                        "Home",
                        false,
                        !editor.doc().brushes.empty() || !editor.doc().instances.empty())) {
                    editor.frameSelection();
                }
                ImGui::EndMenu();
            }
            if (beginMenuWithIcon(assets, kIcons, "package", "Prefab")) {
                if (menuItemWithIcon(assets, kIcons, "page_add", "New Prefab")) {
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
                if (menuItemWithIcon(assets, kIcons, "folder_page", "Open Prefab...")) {
                    editor.showOpenPrefabModal = true;
                    editor.modalPrefabPath = editor.placePrefabPath;
                    std::snprintf(
                        prefabPathBuf,
                        sizeof(prefabPathBuf),
                        "%s",
                        editor.modalPrefabPath.c_str());
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "disk",
                        "Save Prefab",
                        nullptr,
                        false,
                        editor.scene == slopmap::EditorScene::Prefab)) {
                    if (editor.savePrefab(assets)) {
                        prefabBrowser.rescan(assets);
                    }
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "disk_multiple",
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
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "resultset_previous",
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
            if (beginMenuWithIcon(assets, kIcons, "eye", "View")) {
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "world",
                        "Perspective",
                        nullptr,
                        editor.viewPlane == slopmap::ViewPlane::PerspectiveY0)) {
                    editor.setViewPlane(slopmap::ViewPlane::PerspectiveY0);
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "application_view_tile",
                        "Top Ortho",
                        "Tab",
                        editor.viewPlane == slopmap::ViewPlane::Top)) {
                    editor.setViewPlane(slopmap::ViewPlane::Top);
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "application_view_tile",
                        "Front Ortho",
                        nullptr,
                        editor.viewPlane == slopmap::ViewPlane::Front)) {
                    editor.setViewPlane(slopmap::ViewPlane::Front);
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "application_view_tile",
                        "Side Ortho",
                        nullptr,
                        editor.viewPlane == slopmap::ViewPlane::Side)) {
                    editor.setViewPlane(slopmap::ViewPlane::Side);
                }
                ImGui::Separator();
                menuItemWithIcon(assets, kIcons, "shape_square", "Wireframe", "Z", &editor.wireframe);
                ImGui::EndMenu();
            }
            drawDiagnosticsMenu(editor, assets);
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
                if (editor.placeThingKind.has_value()) {
                    editor.placeTarget = slopmap::PlaceTarget::Thing;
                }
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
            drawScene(editor, assets, camera, createTool);
            EndTextureMode();
            slopmap::drawContentTarget(contentTarget, layout.content);
        }

        {
            const ImGuiWindowFlags panelFlags =
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse;
            ImGui::SetNextWindowPos(ImVec2(layout.leftPanel.x, layout.leftPanel.y), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(layout.leftPanel.width, layout.leftPanel.height), ImGuiCond_Always);
            if (ImGui::Begin("Scene", nullptr, panelFlags)) {
                slopmap::EditorDocument& d = editor.doc();
                constexpr const char* kIconSet = kDefaultIconSet;
                static bool sectionOpen[3] = {true, true, true};

                const ImGuiStyle& style = ImGui::GetStyle();
                constexpr int kSectionCount = 3;
                const int openCount =
                    (sectionOpen[0] ? 1 : 0) + (sectionOpen[1] ? 1 : 0) + (sectionOpen[2] ? 1 : 0);
                const float avail = ImGui::GetContentRegionAvail().y;
                const float frameH = ImGui::GetFrameHeight();
                const float spacing = style.ItemSpacing.y;
                const float bodyBudget = std::max(
                    0.0f,
                    avail - static_cast<float>(kSectionCount) * frameH -
                        static_cast<float>(kSectionCount + openCount - 1) * spacing);
                const float bodyH = openCount > 0 ? bodyBudget / static_cast<float>(openCount) : 0.0f;

                sectionOpen[0] = collapsingHeaderWithIcon(
                    assets, kIconSet, "bricks", "Brushes", ImGuiTreeNodeFlags_DefaultOpen);
                if (sectionOpen[0]) {
                    if (ImGui::BeginChild("##brushes", ImVec2(0.0f, bodyH), ImGuiChildFlags_Borders)) {
                        for (std::size_t i = 0; i < d.brushes.size(); ++i) {
                            ImGui::PushID(static_cast<int>(i));
                            const bool selected = d.selection == slopmap::SelectionTarget::Brush &&
                                static_cast<int>(i) == d.selectedBrush;
                            const char* role =
                                d.brushes[i].role == slopengine::BrushRole::Detail ? " [D]" : " [H]";
                            const std::string label = d.brushes[i].id + role;
                            if (selectableWithIcon(assets, kIconSet, "bricks", label.c_str(), selected)) {
                                d.selection = slopmap::SelectionTarget::Brush;
                                d.selectedBrush = static_cast<int>(i);
                                d.selectedFace = -1;
                                d.selectedInstance = -1;
                                d.selectedThing = -1;
                            }
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndChild();
                }

                sectionOpen[1] = collapsingHeaderWithIcon(
                    assets, kIconSet, "package", "Prefab instances", ImGuiTreeNodeFlags_DefaultOpen);
                if (sectionOpen[1]) {
                    if (ImGui::BeginChild("##instances", ImVec2(0.0f, bodyH), ImGuiChildFlags_Borders)) {
                        for (std::size_t i = 0; i < d.instances.size(); ++i) {
                            ImGui::PushID(static_cast<int>(i) + 100000);
                            const bool selected = d.selection == slopmap::SelectionTarget::Instance &&
                                static_cast<int>(i) == d.selectedInstance;
                            const std::string label = d.instances[i].id + " (" + d.instances[i].path + ")";
                            if (selectableWithIcon(assets, kIconSet, "package", label.c_str(), selected)) {
                                d.selection = slopmap::SelectionTarget::Instance;
                                d.selectedInstance = static_cast<int>(i);
                                d.selectedBrush = -1;
                                d.selectedFace = -1;
                                d.selectedThing = -1;
                            }
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndChild();
                }

                sectionOpen[2] = collapsingHeaderWithIcon(
                    assets, kIconSet, "transmit", "Things", ImGuiTreeNodeFlags_DefaultOpen);
                if (sectionOpen[2]) {
                    if (ImGui::BeginChild("##things", ImVec2(0.0f, bodyH), ImGuiChildFlags_Borders)) {
                        if (d.things.empty()) {
                            ImGui::TextDisabled("No things");
                        }
                        for (std::size_t i = 0; i < d.things.size(); ++i) {
                            ImGui::PushID(static_cast<int>(i) + 200000);
                            const bool selected = d.selection == slopmap::SelectionTarget::Thing &&
                                static_cast<int>(i) == d.selectedThing;
                            const std::string label =
                                d.things[i].id + " (" +
                                slopengine::thingKindName(d.things[i].kind) + ")";
                            const char* icon = slopengine::thingKindIsLight(d.things[i].kind)
                                ? "lightbulb"
                                : (d.things[i].kind == slopengine::ThingKind::PlayerStart
                                       ? "user"
                                       : "transmit");
                            if (selectableWithIcon(assets, kIconSet, icon, label.c_str(), selected)) {
                                d.selection = slopmap::SelectionTarget::Thing;
                                d.selectedThing = static_cast<int>(i);
                                d.selectedBrush = -1;
                                d.selectedFace = -1;
                                d.selectedInstance = -1;
                            }
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndChild();
                }
            }
            ImGui::End();
        }

        {
            const ImGuiWindowFlags panelFlags =
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse;
            ImGui::SetNextWindowPos(ImVec2(layout.rightPanel.x, layout.rightPanel.y), ImGuiCond_Always);
            ImGui::SetNextWindowSize(
                ImVec2(layout.rightPanel.width, layout.rightPanel.height),
                ImGuiCond_Always);
            if (ImGui::Begin("Library", nullptr, panelFlags)) {
                constexpr const char* kIconSet = kDefaultIconSet;
                static bool libraryOpen[4] = {true, true, true, false};
                static std::vector<std::string> spritePaths;
                static std::vector<std::string> geoPaths;
                static bool assetListsReady = false;
                if (!assetListsReady) {
                    spritePaths = scanPackageAssets(assets, "sprites", ".spr");
                    geoPaths = scanPackageAssets(assets, "geometry", ".geo");
                    assetListsReady = true;
                }

                const ImGuiStyle& style = ImGui::GetStyle();
                constexpr int kSectionCount = 4;
                const int openCount = (libraryOpen[0] ? 1 : 0) + (libraryOpen[1] ? 1 : 0) +
                    (libraryOpen[2] ? 1 : 0) + (libraryOpen[3] ? 1 : 0);
                const float avail = ImGui::GetContentRegionAvail().y;
                const float frameH = ImGui::GetFrameHeight();
                const float spacing = style.ItemSpacing.y;
                const float bodyBudget = std::max(
                    0.0f,
                    avail - static_cast<float>(kSectionCount) * frameH -
                        static_cast<float>(kSectionCount + openCount - 1) * spacing);
                const float bodyH = openCount > 0 ? bodyBudget / static_cast<float>(openCount) : 0.0f;

                libraryOpen[0] = collapsingHeaderWithIcon(
                    assets, kIconSet, "palette", "Materials", ImGuiTreeNodeFlags_DefaultOpen);
                if (libraryOpen[0]) {
                    const slopmap::MaterialBrowserResult matResult =
                        materialBrowser.drawSection(editor, assets, bodyH);
                    if (matResult.requestRescan) {
                        materialBrowser.rescan(assets);
                    }
                    if (matResult.applied) {
                        editor.rebuildPreview(assets);
                    }
                }

                libraryOpen[1] = collapsingHeaderWithIcon(
                    assets, kIconSet, "package", "Prefabs", ImGuiTreeNodeFlags_DefaultOpen);
                if (libraryOpen[1]) {
                    const slopmap::PrefabBrowserResult prefabResult =
                        prefabBrowser.drawSection(editor, assets, bodyH);
                    if (prefabResult.requestRescan) {
                        prefabBrowser.rescan(assets);
                    }
                    if (prefabResult.selected && editor.scene == slopmap::EditorScene::Level) {
                        editor.placeTarget = slopmap::PlaceTarget::PrefabInstance;
                        editor.placeThingKind.reset();
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
                        } else if (
                            editor.scene == slopmap::EditorScene::Level && editor.levelDoc.dirty) {
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

                libraryOpen[2] = collapsingHeaderWithIcon(
                    assets, kIconSet, "transmit", "Things", ImGuiTreeNodeFlags_DefaultOpen);
                if (libraryOpen[2]) {
                    if (ImGui::BeginChild("##placekinds", ImVec2(0.0f, bodyH), ImGuiChildFlags_Borders)) {
                        if (ImGui::Button("player-start")) {
                            beginThingKind(
                                editor, slopengine::ThingKind::PlayerStart, createTool);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("prop")) {
                            beginThingKind(editor, slopengine::ThingKind::Prop, createTool);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("usable")) {
                            beginThingKind(editor, slopengine::ThingKind::Usable, createTool);
                        }
                        if (ImGui::Button("point-light")) {
                            beginThingKind(
                                editor, slopengine::ThingKind::PointLight, createTool);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("spot-light")) {
                            beginThingKind(
                                editor, slopengine::ThingKind::SpotLight, createTool);
                        }
                        if (ImGui::Button("area-light")) {
                            beginThingKind(
                                editor, slopengine::ThingKind::AreaLight, createTool);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("sun")) {
                            beginThingKind(editor, slopengine::ThingKind::Sun, createTool);
                        }

                        ImGui::Separator();
                        ImGui::TextDisabled("Presentation (required for prop/usable)");
                        if (editor.mode == slopmap::EditorMode::Place &&
                            editor.placeTarget == slopmap::PlaceTarget::Thing &&
                            editor.placeThingKind.has_value()) {
                            ImGui::Text(
                                "Placing: %s",
                                slopengine::thingKindName(*editor.placeThingKind));
                            if (slopengine::thingKindNeedsPresentation(
                                    *editor.placeThingKind)) {
                                const bool ready = (!editor.placeSpritePath.empty()) !=
                                    (!editor.placeGeoPath.empty());
                                if (!ready) {
                                    ImGui::TextColored(
                                        ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
                                        "Select a sprite or geo, then click the viewport");
                                } else {
                                    ImGui::TextColored(
                                        ImVec4(0.4f, 0.9f, 0.45f, 1.0f),
                                        "Click the viewport to place");
                                }
                            } else {
                                ImGui::TextColored(
                                    ImVec4(0.4f, 0.9f, 0.45f, 1.0f),
                                    "Click the viewport to place");
                            }
                        }
                        if (ImGui::Button("Refresh assets")) {
                            spritePaths = scanPackageAssets(assets, "sprites", ".spr");
                            geoPaths = scanPackageAssets(assets, "geometry", ".geo");
                        }
                        ImGui::Text(
                            "Sprite: %s",
                            editor.placeSpritePath.empty() ? "(none)"
                                                          : editor.placeSpritePath.c_str());
                        if (ImGui::BeginListBox("##sprites", ImVec2(-1.0f, 80.0f))) {
                            for (const std::string& path : spritePaths) {
                                const bool selected = editor.placeSpritePath == path;
                                if (ImGui::Selectable(path.c_str(), selected)) {
                                    armPresentationAsset(editor, createTool, true, path);
                                }
                            }
                            ImGui::EndListBox();
                        }
                        ImGui::Text(
                            "Geo: %s",
                            editor.placeGeoPath.empty() ? "(none)" : editor.placeGeoPath.c_str());
                        if (ImGui::BeginListBox("##geos", ImVec2(-1.0f, 80.0f))) {
                            for (const std::string& path : geoPaths) {
                                const bool selected = editor.placeGeoPath == path;
                                if (ImGui::Selectable(path.c_str(), selected)) {
                                    armPresentationAsset(editor, createTool, false, path);
                                }
                            }
                            ImGui::EndListBox();
                        }
                    }
                    ImGui::EndChild();
                }
            }
            ImGui::End();
        }

        {
            const float statusH = ImGui::GetFrameHeightWithSpacing();
            ImGui::SetNextWindowPos(
                ImVec2(0.0f, static_cast<float>(GetScreenHeight()) - statusH),
                ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(static_cast<float>(GetScreenWidth()), statusH), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
            if (ImGui::Begin(
                    "##status",
                    nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus)) {
                ImGui::TextUnformatted(
                    editor.statusMessage.empty() ? "Ready" : editor.statusMessage.c_str());
            }
            ImGui::End();
            ImGui::PopStyleVar();
        }

        if (editor.showLoadModal) {
            ImGui::OpenPopup("Load Map");
            editor.showLoadModal = false;
        }
        if (ImGui::BeginPopupModal("Load Map", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            constexpr const char* kIcons = kDefaultIconSet;
            ImGui::TextUnformatted("Map folder name under maps/");
            ImGui::InputText("##loadmap", mapNameBuf, sizeof(mapNameBuf));
            if (buttonWithIcon(assets, kIcons, "folder_page", "Load", ImVec2(120, 0))) {
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
            if (buttonWithIcon(assets, kIcons, "cancel", "Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (editor.showSaveAsModal) {
            ImGui::OpenPopup("Save Map As");
            editor.showSaveAsModal = false;
        }
        if (ImGui::BeginPopupModal("Save Map As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            constexpr const char* kIcons = kDefaultIconSet;
            ImGui::TextUnformatted("Map folder name under maps/");
            ImGui::InputText("##saveas", mapNameBuf, sizeof(mapNameBuf));
            drawWritePackagePicker(editor, assets);
            if (buttonWithIcon(assets, kIcons, "disk", "Save", ImVec2(120, 0))) {
                if (editor.saveAs(assets, mapNameBuf)) {
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (buttonWithIcon(assets, kIcons, "cancel", "Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (editor.showOpenPrefabModal) {
            ImGui::OpenPopup("Open Prefab");
            editor.showOpenPrefabModal = false;
        }
        if (ImGui::BeginPopupModal("Open Prefab", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            constexpr const char* kIcons = kDefaultIconSet;
            ImGui::TextUnformatted("Prefab virtual path under prefabs/ (e.g. furniture/desk)");
            ImGui::InputText("##openprefab", prefabPathBuf, sizeof(prefabPathBuf));
            if (buttonWithIcon(assets, kIcons, "folder_page", "Open", ImVec2(120, 0))) {
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
            if (buttonWithIcon(assets, kIcons, "cancel", "Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (editor.showSavePrefabAsModal) {
            ImGui::OpenPopup("Save Prefab As");
            editor.showSavePrefabAsModal = false;
        }
        if (ImGui::BeginPopupModal("Save Prefab As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            constexpr const char* kIcons = kDefaultIconSet;
            ImGui::TextUnformatted("Prefab virtual path under prefabs/ (e.g. furniture/desk)");
            ImGui::InputText("##saveprefab", prefabPathBuf, sizeof(prefabPathBuf));
            drawWritePackagePicker(editor, assets);
            if (buttonWithIcon(assets, kIcons, "disk", "Save", ImVec2(120, 0))) {
                if (editor.savePrefabAs(assets, prefabPathBuf)) {
                    prefabBrowser.rescan(assets);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (buttonWithIcon(assets, kIcons, "cancel", "Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (editor.showNewModal) {
            ImGui::OpenPopup("New Map");
            editor.showNewModal = false;
        }
        if (ImGui::BeginPopupModal("New Map", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            constexpr const char* kIcons = kDefaultIconSet;
            ImGui::TextUnformatted("Discard unsaved level changes and create a new map?");
            drawWritePackagePicker(editor, assets);
            if (buttonWithIcon(assets, kIcons, "page_add", "Discard & New", ImVec2(140, 0))) {
                cancelTools(createTool, selectTool, editor);
                editor.newMap("untitled");
                previewNeedsRebuild = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (buttonWithIcon(assets, kIcons, "cancel", "Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (editor.showSwitchSceneModal) {
            ImGui::OpenPopup("Switch Scene");
            editor.showSwitchSceneModal = false;
        }
        if (ImGui::BeginPopupModal("Switch Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            constexpr const char* kIcons = kDefaultIconSet;
            ImGui::TextUnformatted("Discard unsaved changes in the current scene?");
            if (buttonWithIcon(assets, kIcons, "accept", "Discard & Switch", ImVec2(160, 0))) {
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
            if (buttonWithIcon(assets, kIcons, "cancel", "Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (editor.showQuitModal) {
            ImGui::OpenPopup("Unsaved changes");
            editor.showQuitModal = false;
        }
        if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            constexpr const char* kIcons = kDefaultIconSet;
            ImGui::TextUnformatted("There are unsaved changes. Quit anyway?");
            if (buttonWithIcon(assets, kIcons, "door", "Quit", ImVec2(120, 0))) {
                editor.quitConfirmed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (buttonWithIcon(assets, kIcons, "cancel", "Cancel", ImVec2(120, 0))) {
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
