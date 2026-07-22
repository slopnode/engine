#include "camera.hpp"
#include "compile.hpp"
#include "create_tool.hpp"
#include "editor.hpp"
#include "layout.hpp"
#include "material_browser.hpp"
#include "place_tool.hpp"
#include "punch_tool.hpp"
#include "clip_tool.hpp"
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
#include <rlgl.h>
#include <s7.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <iterator>
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
        << "  --map         Optional map under maps/ to open (default: new untitled map)\n";
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
    ImGui::Text(
        "Mode: %s",
        d.selectionMode == slopmap::SelectionMode::Brush   ? "Brush"
            : d.selectionMode == slopmap::SelectionMode::Face ? "Face"
                                                             : "Entity");
    if (d.selectionMode == slopmap::SelectionMode::Brush && !d.selectedBrushes.empty()) {
        ImGui::Text("Brushes: %d", static_cast<int>(d.selectedBrushes.size()));
        if (d.activeBrush >= 0 && d.activeBrush < static_cast<int>(d.brushes.size())) {
            const slopengine::Brush& brush = d.brushes[static_cast<std::size_t>(d.activeBrush)];
            ImGui::Text("Active: %s", brush.id.c_str());
            ImGui::Text("Role: %s", slopengine::brushRoleName(brush.role));
        }
    } else if (d.selectionMode == slopmap::SelectionMode::Face && !d.selectedFaces.empty()) {
        ImGui::Text("Faces: %d", static_cast<int>(d.selectedFaces.size()));
        if (d.activeFace.valid() && d.activeFace.brush < static_cast<int>(d.brushes.size())) {
            const auto& brush = d.brushes[static_cast<std::size_t>(d.activeFace.brush)];
            if (d.activeFace.face < static_cast<int>(brush.faces.size())) {
                const auto& face = brush.faces[static_cast<std::size_t>(d.activeFace.face)];
                ImGui::Text("Active: %s", face.id.c_str());
                ImGui::Text("Material: %s", face.material.c_str());
            }
        }
    } else if (d.selectionMode == slopmap::SelectionMode::Entity && !d.selectedEntities.empty()) {
        ImGui::Text("Entities: %d", static_cast<int>(d.selectedEntities.size()));
        if (d.activeEntity.valid()) {
            if (d.activeEntity.kind == slopmap::EntityRef::Kind::Thing &&
                d.activeEntity.index < static_cast<int>(d.things.size())) {
                const auto& thing = d.things[static_cast<std::size_t>(d.activeEntity.index)];
                ImGui::Text("Active thing: %s", thing.id.c_str());
            } else if (
                d.activeEntity.kind == slopmap::EntityRef::Kind::Instance &&
                d.activeEntity.index < static_cast<int>(d.instances.size())) {
                const auto& instance = d.instances[static_cast<std::size_t>(d.activeEntity.index)];
                ImGui::Text("Active instance: %s", instance.id.c_str());
            }
        }
    } else {
        ImGui::TextUnformatted("None");
    }

    ImGui::EndMenu();
}

void drawScene(
    slopmap::Editor& editor,
    slopengine::AssetStore& assets,
    const Camera3D& camera,
    const slopmap::CreateTool& createTool,
    const slopmap::PunchTool& punchTool,
    const slopmap::ClipTool& clipTool) {
    ClearBackground(Color{32, 34, 38, 255});
    BeginMode3D(camera);
    const Vector3 eye = camera.position;
    const float lineWidth = std::max(0.015f, editor.gridSize * 0.02f);
    const float axisWidth = lineWidth * 1.5f;
    if (editor.showGrid) {
        slopmap::drawGrid(
            editor.gridPlane, 32.0f, editor.gridSize, Color{70, 74, 80, 255}, eye, lineWidth);
    }
    slopmap::drawThickLine3D({-100, 0, 0}, {100, 0, 0}, Color{180, 60, 60, 255}, axisWidth, eye);
    slopmap::drawThickLine3D({0, -100, 0}, {0, 100, 0}, Color{60, 180, 60, 255}, axisWidth, eye);
    slopmap::drawThickLine3D({0, 0, -100}, {0, 0, 100}, Color{60, 60, 180, 255}, axisWidth, eye);

    const slopmap::EditorDocument& d = editor.doc();
    const std::vector<int> selectedBrushes =
        d.selectionMode == slopmap::SelectionMode::Brush ? d.selectedBrushes
                                                         : std::vector<int>{};
    const bool fillWire = editor.fill == slopmap::PreviewFill::Wireframe;
    const bool xrayAll = editor.wireframe == slopmap::WireframeOverlay::All;
    const bool xrayVisible = editor.wireframe == slopmap::WireframeOverlay::Visible;
    editor.preview.draw(
        editor.fill,
        editor.wireframe,
        d.brushes,
        editor.expandedInstanceBrushes,
        selectedBrushes,
        eye,
        lineWidth);

    if (fillWire) {
        for (std::size_t i = 0; i < editor.expandedInstanceBrushes.size(); ++i) {
            const bool selected = d.selectionMode == slopmap::SelectionMode::Entity &&
                d.isEntitySelected(
                    {slopmap::EntityRef::Kind::Instance, editor.expandedInstanceOwners[i]});
            slopmap::drawBrushFaceOutlines(
                editor.expandedInstanceBrushes[i],
                slopmap::brushOutlineColor(editor.expandedInstanceBrushes[i], selected),
                eye,
                lineWidth);
        }
    }

    const bool drawBrushSelection =
        d.selectionMode == slopmap::SelectionMode::Brush && !fillWire && !xrayAll && !xrayVisible &&
        !d.selectedBrushes.empty();
    const bool drawFaceSelection =
        d.selectionMode == slopmap::SelectionMode::Face && !d.selectedFaces.empty();
    const bool drawEntitySelection =
        !fillWire && !xrayAll && !xrayVisible &&
        d.selectionMode == slopmap::SelectionMode::Entity && !d.selectedEntities.empty();
    if (drawBrushSelection || drawFaceSelection || drawEntitySelection) {
        rlDrawRenderBatchActive();
        rlDisableDepthTest();
        rlDisableDepthMask();

        if (drawBrushSelection) {
            for (int index : d.selectedBrushes) {
                if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
                    continue;
                }
                const auto& brush = d.brushes[static_cast<std::size_t>(index)];
                slopmap::drawBrushFaceOutlines(
                    brush,
                    slopmap::brushOutlineColor(brush, true),
                    eye,
                    lineWidth);
            }
        }

        if (drawFaceSelection) {
            std::unordered_set<int> outlinedBrushes;
            for (const slopmap::FaceRef& ref : d.selectedFaces) {
                if (!ref.valid() || ref.brush >= static_cast<int>(d.brushes.size())) {
                    continue;
                }
                if (!outlinedBrushes.insert(ref.brush).second) {
                    continue;
                }
                const auto& brush = d.brushes[static_cast<std::size_t>(ref.brush)];
                slopmap::drawBrushFaceOutlines(
                    brush, Color{160, 100, 50, 255}, eye, lineWidth);
            }
            for (const slopmap::FaceRef& ref : d.selectedFaces) {
                if (!ref.valid() || ref.brush >= static_cast<int>(d.brushes.size())) {
                    continue;
                }
                const auto& brush = d.brushes[static_cast<std::size_t>(ref.brush)];
                if (ref.face >= static_cast<int>(brush.faces.size())) {
                    continue;
                }
                const auto& face = brush.faces[static_cast<std::size_t>(ref.face)];
                const bool active = ref == d.activeFace;
                const Color faceColor = active
                    ? (face.uvLock ? Color{255, 200, 80, 255} : Color{80, 220, 255, 255})
                    : Color{80, 160, 200, 255};
                for (std::size_t i = 0; i < face.vertices.size(); ++i) {
                    const Vector3& a = face.vertices[i];
                    const Vector3& b = face.vertices[(i + 1) % face.vertices.size()];
                    slopmap::drawThickLine3D(a, b, faceColor, lineWidth * 1.25f, eye);
                }
            }
        }

        if (drawEntitySelection) {
            for (std::size_t i = 0; i < editor.expandedInstanceBrushes.size(); ++i) {
                if (!d.isEntitySelected(
                        {slopmap::EntityRef::Kind::Instance, editor.expandedInstanceOwners[i]})) {
                    continue;
                }
                slopmap::drawBrushFaceOutlines(
                    editor.expandedInstanceBrushes[i],
                    Color{255, 140, 40, 255},
                    eye,
                    lineWidth);
            }
        }

        rlDrawRenderBatchActive();
        rlEnableDepthMask();
        rlEnableDepthTest();
    }

    std::vector<int> selectedThings;
    if (d.selectionMode == slopmap::SelectionMode::Entity) {
        for (const slopmap::EntityRef& ref : d.selectedEntities) {
            if (ref.kind == slopmap::EntityRef::Kind::Thing) {
                selectedThings.push_back(ref.index);
            }
        }
    }
    slopmap::drawThings(assets, d.things, selectedThings, camera);

    createTool.drawPreview();
    punchTool.drawPreview();
    const float clipLineWidth = std::max(0.015f, editor.gridSize * 0.02f);
    clipTool.drawPreview(editor, eye, clipLineWidth);
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

void cancelTools(
    slopmap::CreateTool& createTool,
    slopmap::SelectTool& selectTool,
    slopmap::PunchTool& punchTool,
    slopmap::ClipTool& clipTool,
    slopmap::Editor& editor) {
    createTool.reset();
    punchTool.reset();
    clipTool.reset();
    editor.showPrimitiveParamsModal = false;
    editor.showHollowModal = false;
    if (selectTool.translating) {
        selectTool.cancelTranslate(editor);
    }
}

slopengine::BrushRole nextBrushRole(slopengine::BrushRole role) {
    switch (role) {
    case slopengine::BrushRole::Hull:
        return slopengine::BrushRole::Detail;
    case slopengine::BrushRole::Detail:
        return slopengine::BrushRole::Hint;
    case slopengine::BrushRole::Hint:
        return slopengine::BrushRole::Trigger;
    case slopengine::BrushRole::Trigger:
        return slopengine::BrushRole::Water;
    case slopengine::BrushRole::Water:
        return slopengine::BrushRole::Window;
    case slopengine::BrushRole::Window:
        return slopengine::BrushRole::Hull;
    }
    return slopengine::BrushRole::Hull;
}

const char* brushRoleToolbarLabel(slopengine::BrushRole role) {
    return slopengine::brushRoleName(role);
}

const char* brushRoleToolbarIcon(slopengine::BrushRole role) {
    return slopengine::brushRoleContributesSplits(role) ? "cut" : "brick";
}

void applyHollow(slopmap::Editor& editor) {
    slopmap::EditorDocument& d = editor.doc();
    if (d.selectionMode != slopmap::SelectionMode::Brush || d.selectedBrushes.empty()) {
        editor.statusMessage = "Hollow: select box brush(es)";
        return;
    }
    std::vector<int> indices = d.selectedBrushes;
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    std::vector<slopengine::Brush> walls;
    std::vector<int> removeIndices;
    slopengine::BrushRole role = slopengine::BrushRole::Hull;
    auto allocateId = [&]() { return editor.allocateBrushId(); };
    for (int index : indices) {
        if (index < 0 || index >= static_cast<int>(d.brushes.size())) {
            continue;
        }
        const slopengine::Brush& source = d.brushes[static_cast<std::size_t>(index)];
        if (!source.box) {
            continue;
        }
        auto next = slopengine::hollowBrushBox(source, editor.hollowThickness, allocateId);
        if (next.empty()) {
            continue;
        }
        role = source.role;
        removeIndices.push_back(index);
        walls.insert(walls.end(), std::make_move_iterator(next.begin()), std::make_move_iterator(next.end()));
    }
    if (walls.empty()) {
        editor.statusMessage = "Hollow failed (need box brushes, thickness < half size)";
        return;
    }
    std::sort(removeIndices.begin(), removeIndices.end(), std::greater<int>());
    for (int index : removeIndices) {
        d.brushes.erase(d.brushes.begin() + index);
    }
    std::vector<int> created;
    for (slopengine::Brush& wall : walls) {
        d.brushes.push_back(std::move(wall));
        created.push_back(static_cast<int>(d.brushes.size()) - 1);
    }
    editor.selectBrushes(created, created.front());
    editor.markDirty();
    editor.markBrushCompileDirty(role);
    editor.statusMessage = "Hollowed into " + std::to_string(created.size()) + " walls";
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
    ImFont* monoFont = setupImGuiWithUiAndMonoFont(
        assets, kDefaultUiFontPath, kMonoUiFontPath, true);

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
    slopmap::PunchTool punchTool;
    slopmap::ClipTool clipTool;
    slopmap::MaterialBrowser materialBrowser;
    slopmap::PrefabBrowser prefabBrowser;
    slopmap::CompileController compile;
    materialBrowser.rescan(assets);
    prefabBrowser.rescan(assets);
    bool previewNeedsRebuild = false;
    bool compileWasRunning = false;
    bool compileRunIncludesRad = false;
    char mapNameBuf[128] = {};
    char prefabPathBuf[256] = {};
    RenderTexture2D contentTarget{};

    auto syncCompileStatus = [&]() {
        std::string status;
        if (compile.takeStatusUpdate(status)) {
            editor.statusMessage = std::move(status);
        }
    };

    auto startCompile = [&](std::vector<slopmap::CompileStage> stages) {
        if (compile.running()) {
            return;
        }
        const std::string& mapName = editor.levelDoc.assetPath;
        if (mapName.empty() || mapName == "untitled") {
            editor.statusMessage = "Save the map before compiling";
            editor.showSaveAsModal = true;
            mapNameBuf[0] = '\0';
            return;
        }
        if (editor.levelDoc.dirty) {
            if (!editor.save(assets)) {
                editor.statusMessage = "Save failed; compile aborted";
                return;
            }
        }
        compileRunIncludesRad = false;
        for (const slopmap::CompileStage stage : stages) {
            if (stage == slopmap::CompileStage::Rad) {
                compileRunIncludesRad = true;
                break;
            }
        }
        slopmap::CompileMountArgs mounts;
        mounts.baseGame = config->mount.base_game;
        mounts.mods = config->mount.mods;
        mounts.mapName = mapName;
        compile.requestRun(std::move(stages), mounts);
        syncCompileStatus();
    };

    auto playMap = [&]() {
        if (editor.scene != slopmap::EditorScene::Level) {
            editor.statusMessage = "Play is only available in the Level scene";
            return;
        }
        const std::string& mapName = editor.levelDoc.assetPath;
        if (mapName.empty() || mapName == "untitled") {
            editor.statusMessage = "Save the map before playing";
            editor.showSaveAsModal = true;
            mapNameBuf[0] = '\0';
            return;
        }
        if (editor.levelDoc.dirty) {
            if (!editor.save(assets)) {
                editor.statusMessage = "Save failed; play aborted";
                return;
            }
        }
        slopmap::CompileMountArgs mounts;
        mounts.baseGame = config->mount.base_game;
        mounts.mods = config->mount.mods;
        mounts.mapName = mapName;
        std::string error;
        if (!slopmap::launchGame(mounts, error)) {
            editor.statusMessage = error;
            return;
        }
        editor.statusMessage = "Playing " + mapName;
    };

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
        const float toolbarRowH = ImGui::GetFrameHeight() + 8.0f;
        const float toolbarHeight = toolbarRowH * 2.0f;
        const slopmap::UiLayout layout = slopmap::computeUiLayout(chromeHeight, statusHeight);
        Rectangle viewport = layout.content;
        viewport.y += toolbarHeight;
        viewport.height = std::max(1.0f, viewport.height - toolbarHeight);
        editor.contentViewport = viewport;
        slopmap::ensureContentTarget(contentTarget, viewport);

        const Vector2 mouse = GetMousePosition();
        const bool mouseInContent = slopmap::pointInRect(mouse, viewport);
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
                        cancelTools(createTool, selectTool, punchTool, clipTool, editor);
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
                        nullptr,
                        editor.mode == slopmap::EditorMode::Select)) {
                    editor.mode = slopmap::EditorMode::Select;
                    createTool.reset();
                    punchTool.reset();
                    clipTool.reset();
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "shape_square",
                        "Create Mode",
                        nullptr,
                        editor.mode == slopmap::EditorMode::Create)) {
                    if (selectTool.translating) {
                        selectTool.cancelTranslate(editor);
                    }
                    punchTool.reset();
                    clipTool.reset();
                    editor.mode = slopmap::EditorMode::Create;
                    createTool.setStatus(editor);
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "package",
                        "Place Mode",
                        nullptr,
                        editor.mode == slopmap::EditorMode::Place,
                        editor.scene == slopmap::EditorScene::Level)) {
                    if (selectTool.translating) {
                        selectTool.cancelTranslate(editor);
                    }
                    createTool.reset();
                    punchTool.reset();
                    clipTool.reset();
                    editor.mode = slopmap::EditorMode::Place;
                    if (editor.placeThingKind.has_value()) {
                        editor.placeTarget = slopmap::PlaceTarget::Thing;
                    }
                }
                ImGui::Separator();
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "bricks",
                        "Selection: Brush",
                        nullptr,
                        editor.doc().selectionMode == slopmap::SelectionMode::Brush,
                        editor.mode == slopmap::EditorMode::Select)) {
                    editor.setSelectionMode(slopmap::SelectionMode::Brush);
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "shape_handles",
                        "Selection: Face",
                        nullptr,
                        editor.doc().selectionMode == slopmap::SelectionMode::Face,
                        editor.mode == slopmap::EditorMode::Select)) {
                    editor.setSelectionMode(slopmap::SelectionMode::Face);
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "user",
                        "Selection: Entity",
                        nullptr,
                        editor.doc().selectionMode == slopmap::SelectionMode::Entity,
                        editor.mode == slopmap::EditorMode::Select)) {
                    editor.setSelectionMode(slopmap::SelectionMode::Entity);
                }
                ImGui::Separator();
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "brick",
                        "Toggle Brush Role",
                        "H",
                        false,
                        editor.doc().selectionMode == slopmap::SelectionMode::Brush &&
                            !editor.doc().selectedBrushes.empty())) {
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
                        (editor.doc().selectionMode == slopmap::SelectionMode::Brush &&
                         !editor.doc().selectedBrushes.empty()) ||
                            (editor.doc().selectionMode == slopmap::SelectionMode::Face &&
                             !editor.doc().selectedFaces.empty()))) {
                    selectTool.toggleSelectedUvLock(editor);
                    previewNeedsRebuild = true;
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "box",
                        "Hollow...",
                        nullptr,
                        false,
                        editor.doc().selectionMode == slopmap::SelectionMode::Brush &&
                            !editor.doc().selectedBrushes.empty())) {
                    editor.hollowThickness = editor.gridSize;
                    editor.showHollowModal = true;
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "cut",
                        "Punch-out",
                        nullptr,
                        false,
                        editor.doc().selectionMode == slopmap::SelectionMode::Face &&
                            !editor.doc().selectedFaces.empty())) {
                    if (selectTool.translating) {
                        selectTool.cancelTranslate(editor);
                    }
                    createTool.reset();
                    clipTool.reset();
                    punchTool.beginFromSelection(editor);
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "arrow_divide",
                        "Clip",
                        "Shift+X",
                        false,
                        editor.doc().selectionMode == slopmap::SelectionMode::Brush &&
                            !editor.doc().selectedBrushes.empty())) {
                    if (selectTool.translating) {
                        selectTool.cancelTranslate(editor);
                    }
                    createTool.reset();
                    punchTool.reset();
                    clipTool.beginFromSelection(editor);
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
                        cancelTools(createTool, selectTool, punchTool, clipTool, editor);
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
                        cancelTools(createTool, selectTool, punchTool, clipTool, editor);
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
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "table",
                        "Show Grid",
                        "\\",
                        editor.showGrid)) {
                    editor.showGrid = !editor.showGrid;
                    editor.statusMessage = editor.showGrid ? "Grid: on" : "Grid: off";
                }
                if (beginMenuWithIcon(assets, kIcons, "layers", "Grid Plane")) {
                    if (menuItemWithIcon(
                            assets,
                            kIcons,
                            "application_view_tile",
                            "XZ",
                            nullptr,
                            editor.gridPlane == slopmap::GridPlane::XZ)) {
                        editor.gridPlane = slopmap::GridPlane::XZ;
                        editor.statusMessage = "Grid plane: XZ";
                    }
                    if (menuItemWithIcon(
                            assets,
                            kIcons,
                            "application_view_tile",
                            "XY",
                            nullptr,
                            editor.gridPlane == slopmap::GridPlane::XY)) {
                        editor.gridPlane = slopmap::GridPlane::XY;
                        editor.statusMessage = "Grid plane: XY";
                    }
                    if (menuItemWithIcon(
                            assets,
                            kIcons,
                            "application_view_tile",
                            "YZ",
                            nullptr,
                            editor.gridPlane == slopmap::GridPlane::YZ)) {
                        editor.gridPlane = slopmap::GridPlane::YZ;
                        editor.statusMessage = "Grid plane: YZ";
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "shape_square",
                        "Wireframe",
                        "Z",
                        editor.fill == slopmap::PreviewFill::Wireframe)) {
                    editor.fill = slopmap::PreviewFill::Wireframe;
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "box",
                        "Solid",
                        nullptr,
                        editor.fill == slopmap::PreviewFill::Solid)) {
                    editor.fill = slopmap::PreviewFill::Solid;
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "picture",
                        "Textures",
                        nullptr,
                        editor.fill == slopmap::PreviewFill::Textures)) {
                    editor.fill = slopmap::PreviewFill::Textures;
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "world",
                        "Unlit",
                        nullptr,
                        editor.fill == slopmap::PreviewFill::Unlit)) {
                    if (editor.preview.visValid || editor.reloadVisPreview(assets)) {
                        editor.fill = slopmap::PreviewFill::Unlit;
                    } else {
                        editor.statusMessage = "No VIS; run VIS (falling back to Textures)";
                        editor.fill = slopmap::PreviewFill::Textures;
                    }
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "lightbulb",
                        "Lit",
                        nullptr,
                        editor.fill == slopmap::PreviewFill::Lit)) {
                    if (editor.preview.litValid || editor.reloadLitBake(assets)) {
                        editor.fill = slopmap::PreviewFill::Lit;
                    } else {
                        editor.statusMessage = "No lightmap bake; run RAD";
                    }
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "contrast",
                        "Solid Lit",
                        nullptr,
                        editor.fill == slopmap::PreviewFill::SolidLit)) {
                    if (editor.preview.litValid || editor.reloadLitBake(assets)) {
                        editor.fill = slopmap::PreviewFill::SolidLit;
                    } else {
                        editor.statusMessage = "No lightmap bake; run RAD";
                    }
                }
                if (beginMenuWithIcon(assets, kIcons, "eye", "X-Ray Overlay")) {
                    if (menuItemWithIcon(
                            assets,
                            kIcons,
                            "delete",
                            "Off",
                            "Shift+Z",
                            editor.wireframe == slopmap::WireframeOverlay::Off)) {
                        editor.wireframe = slopmap::WireframeOverlay::Off;
                    }
                    if (menuItemWithIcon(
                            assets,
                            kIcons,
                            "shape_square",
                            "Visible",
                            nullptr,
                            editor.wireframe == slopmap::WireframeOverlay::Visible)) {
                        editor.wireframe = slopmap::WireframeOverlay::Visible;
                    }
                    if (menuItemWithIcon(
                            assets,
                            kIcons,
                            "world",
                            "All",
                            nullptr,
                            editor.wireframe == slopmap::WireframeOverlay::All)) {
                        editor.wireframe = slopmap::WireframeOverlay::All;
                    }
                    ImGui::EndMenu();
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "shape_flip_vertical",
                        "Ignore Backfaces",
                        nullptr,
                        editor.ignoreBackfaces)) {
                    editor.ignoreBackfaces = !editor.ignoreBackfaces;
                    editor.statusMessage = editor.ignoreBackfaces
                        ? "Ignore backfaces: On"
                        : "Ignore backfaces: Off";
                }
                ImGui::EndMenu();
            }
            if (beginMenuWithIcon(assets, kIcons, "cog", "Compile")) {
                const bool canRun = !compile.running();
                const char* runBspLabel =
                    editor.compileDirty.bsp ? "Run BSP *" : "Run BSP";
                const char* runVisLabel =
                    editor.compileDirty.vis ? "Run VIS *" : "Run VIS";
                const char* runRadLabel =
                    editor.compileDirty.rad ? "Run RAD *" : "Run RAD";
                const char* runAllLabel =
                    (editor.compileDirty.bsp || editor.compileDirty.vis ||
                        editor.compileDirty.rad)
                    ? "Run All *"
                    : "Run All";
                if (menuItemWithIcon(
                        assets, kIcons, "brick", runBspLabel, nullptr, false, canRun)) {
                    startCompile({slopmap::CompileStage::Bsp});
                }
                if (menuItemWithIcon(
                        assets, kIcons, "chart_organisation", runVisLabel, nullptr, false, canRun)) {
                    startCompile({slopmap::CompileStage::Vis});
                }
                if (menuItemWithIcon(
                        assets, kIcons, "lightbulb", runRadLabel, nullptr, false, canRun)) {
                    startCompile({slopmap::CompileStage::Rad});
                }
                ImGui::Separator();
                if (menuItemWithIcon(
                        assets, kIcons, "script_go", runAllLabel, nullptr, false, canRun)) {
                    startCompile({
                        slopmap::CompileStage::Bsp,
                        slopmap::CompileStage::Vis,
                        slopmap::CompileStage::Rad,
                    });
                }
                ImGui::Separator();
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "control_play",
                        "Play Map",
                        "F5",
                        false,
                        editor.scene == slopmap::EditorScene::Level)) {
                    playMap();
                }
                ImGui::Separator();
                if (menuItemWithIcon(
                        assets, kIcons, "wrench", "RAD Options…", nullptr, false, true)) {
                    compile.showOptionsModal = true;
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "application_osx_terminal",
                        "Show Output",
                        nullptr,
                        false,
                        true)) {
                    compile.showOutputWindow = true;
                }
                ImGui::EndMenu();
            }
            drawDiagnosticsMenu(editor, assets);
            ImGui::EndMainMenuBar();
        }

        compile.tick();
        syncCompileStatus();
        {
            slopmap::CompileStage completedStage = slopmap::CompileStage::Bsp;
            while (compile.takeCompletedStage(completedStage)) {
                editor.clearCompileStage(completedStage);
                if (completedStage == slopmap::CompileStage::Vis) {
                    editor.reloadVisPreview(assets);
                }
            }
        }
        if (compileWasRunning && !compile.running()) {
            if (compileRunIncludesRad && compile.statusSummary() == "Compile finished") {
                if (editor.reloadLitBake(assets)) {
                    editor.fill = slopmap::PreviewFill::Lit;
                    editor.statusMessage = "Compile finished — viewing Lit bake";
                }
            } else if (compile.statusSummary() == "Compile finished") {
                editor.reloadVisPreview(assets);
            }
            compileRunIncludesRad = false;
        }
        compileWasRunning = compile.running();

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
                    cancelTools(createTool, selectTool, punchTool, clipTool, editor);
                    editor.newMap("untitled");
                    previewNeedsRebuild = true;
                }
            }
            if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_O)) {
                editor.showLoadModal = true;
                std::snprintf(mapNameBuf, sizeof(mapNameBuf), "%s", editor.levelDoc.assetPath.c_str());
            }
            if (IsKeyPressed(KEY_F5)) {
                playMap();
            }
            if (IsKeyPressed(KEY_H) &&
                editor.doc().selectionMode == slopmap::SelectionMode::Brush &&
                !editor.doc().selectedBrushes.empty()) {
                editor.toggleSelectedBrushRole();
                previewNeedsRebuild = true;
            }
            if (IsKeyPressed(KEY_L) &&
                ((editor.doc().selectionMode == slopmap::SelectionMode::Brush &&
                  !editor.doc().selectedBrushes.empty()) ||
                 (editor.doc().selectionMode == slopmap::SelectionMode::Face &&
                  !editor.doc().selectedFaces.empty()))) {
                selectTool.toggleSelectedUvLock(editor);
                previewNeedsRebuild = true;
            }
            if ((IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) &&
                IsKeyPressed(KEY_X) && !selectTool.translating &&
                editor.doc().selectionMode == slopmap::SelectionMode::Brush &&
                !editor.doc().selectedBrushes.empty()) {
                createTool.reset();
                punchTool.reset();
                clipTool.beginFromSelection(editor);
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
            if (IsKeyPressed(KEY_BACKSLASH)) {
                editor.showGrid = !editor.showGrid;
                editor.statusMessage = editor.showGrid ? "Grid: on" : "Grid: off";
            }
            if (IsKeyPressed(KEY_HOME)) {
                editor.frameSelection();
            }
            if (IsKeyPressed(KEY_Z) && !selectTool.translating) {
                if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
                    switch (editor.wireframe) {
                    case slopmap::WireframeOverlay::Off:
                        editor.wireframe = slopmap::WireframeOverlay::Visible;
                        editor.statusMessage = "X-Ray: Visible";
                        break;
                    case slopmap::WireframeOverlay::Visible:
                        editor.wireframe = slopmap::WireframeOverlay::All;
                        editor.statusMessage = "X-Ray: All";
                        break;
                    case slopmap::WireframeOverlay::All:
                        editor.wireframe = slopmap::WireframeOverlay::Off;
                        editor.statusMessage = "X-Ray: Off";
                        break;
                    }
                } else {
                    switch (editor.fill) {
                    case slopmap::PreviewFill::Wireframe:
                        editor.fill = slopmap::PreviewFill::Solid;
                        break;
                    case slopmap::PreviewFill::Solid:
                        editor.fill = slopmap::PreviewFill::Textures;
                        break;
                    case slopmap::PreviewFill::Textures:
                        if (editor.preview.visValid || editor.reloadVisPreview(assets)) {
                            editor.fill = slopmap::PreviewFill::Unlit;
                        } else if (editor.preview.litValid || editor.reloadLitBake(assets)) {
                            editor.fill = slopmap::PreviewFill::Lit;
                        } else {
                            editor.fill = slopmap::PreviewFill::Wireframe;
                        }
                        break;
                    case slopmap::PreviewFill::Unlit:
                        if (editor.preview.litValid || editor.reloadLitBake(assets)) {
                            editor.fill = slopmap::PreviewFill::Lit;
                        } else {
                            editor.fill = slopmap::PreviewFill::Wireframe;
                        }
                        break;
                    case slopmap::PreviewFill::Lit:
                        if (editor.preview.litValid || editor.reloadLitBake(assets)) {
                            editor.fill = slopmap::PreviewFill::SolidLit;
                        } else {
                            editor.fill = slopmap::PreviewFill::Wireframe;
                        }
                        break;
                    case slopmap::PreviewFill::SolidLit:
                        editor.fill = slopmap::PreviewFill::Wireframe;
                        break;
                    }
                }
            }
        }

        const bool wasTranslating = selectTool.translating;
        const bool createWasActive = createTool.active();
        const bool punchWasActive = punchTool.active();
        const bool clipWasActive = clipTool.active();
        const std::size_t brushCountBefore = editor.doc().brushes.size();
        const std::size_t instanceCountBefore = editor.doc().instances.size();
        const bool dirtyBefore = editor.doc().dirty;

        const bool toolActive = selectTool.translating || createTool.active() ||
            punchTool.active() || clipTool.active();
        const bool blockTools = rmbFly || uiWantsMouse || (!mouseInContent && !toolActive);
        if (clipTool.active()) {
            clipTool.update(editor, camera, blockTools, uiWantsKeyboard || rmbFly);
        } else if (punchTool.active()) {
            punchTool.update(editor, camera, blockTools, uiWantsKeyboard || rmbFly);
        } else if (editor.mode == slopmap::EditorMode::Create) {
            createTool.update(editor, camera, blockTools, uiWantsKeyboard || rmbFly);
        } else if (editor.mode == slopmap::EditorMode::Place) {
            placeTool.update(editor, assets, camera, blockTools, uiWantsKeyboard || rmbFly);
        } else {
            selectTool.update(editor, assets, camera, blockTools, uiWantsKeyboard || rmbFly);
        }

        if (editor.doc().brushes.size() != brushCountBefore ||
            editor.doc().instances.size() != instanceCountBefore ||
            editor.doc().dirty != dirtyBefore || selectTool.translating || wasTranslating ||
            createTool.active() != createWasActive || punchTool.active() != punchWasActive ||
            clipTool.active() != clipWasActive) {
            previewNeedsRebuild = true;
        }
        if (previewNeedsRebuild &&
            ((!createTool.active() && !punchTool.active() && !clipTool.active()) ||
                selectTool.translating)) {
            editor.rebuildPreview(assets);
            previewNeedsRebuild = selectTool.translating;
        }

        BeginDrawing();
        ClearBackground(Color{28, 30, 34, 255});

        if (contentTarget.id != 0) {
            BeginTextureMode(contentTarget);
            drawScene(editor, assets, camera, createTool, punchTool, clipTool);
            EndTextureMode();
            slopmap::drawContentTarget(contentTarget, viewport);
        }

        {
            constexpr const char* kToolbarIcons = kDefaultIconSet;
            ImGui::SetNextWindowPos(ImVec2(layout.content.x, layout.content.y), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(layout.content.width, toolbarHeight), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
            if (ImGui::Begin(
                    "##editorToolbar",
                    nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse)) {
                auto toolBtn =
                    [&](const char* id,
                        const char* icon,
                        const char* label,
                        bool selected,
                        bool enabled = true) -> bool {
                    ImGui::PushID(id);
                    constexpr float kIcon = 16.0f;
                    const ImGuiStyle& st = ImGui::GetStyle();
                    const ImVec2 textSize = ImGui::CalcTextSize(label);
                    const float width =
                        st.FramePadding.x * 2.0f + kIcon + st.ItemInnerSpacing.x + textSize.x;
                    const float height = ImGui::GetFrameHeight();

                    if (selected) {
                        ImGui::PushStyleColor(
                            ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                        ImGui::PushStyleColor(
                            ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                        ImGui::PushStyleColor(
                            ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
                    }
                    if (!enabled) {
                        ImGui::BeginDisabled();
                    }
                    const bool pressed = ImGui::Button("##tb", ImVec2(width, height));
                    const ImVec2 min = ImGui::GetItemRectMin();
                    const ImVec2 max = ImGui::GetItemRectMax();
                    const float contentW = kIcon + st.ItemInnerSpacing.x + textSize.x;
                    const float x = min.x + (max.x - min.x - contentW) * 0.5f;
                    const float yIcon = min.y + (height - kIcon) * 0.5f;
                    const float yText = min.y + (height - textSize.y) * 0.5f;

                    ImDrawList* draw = ImGui::GetWindowDrawList();
                    const IconAtlas* atlas = assets.getIconAtlas(kToolbarIcons);
                    if (atlas != nullptr && atlas->texture.id != 0) {
                        if (const auto rect = findIconRect(*atlas, icon)) {
                            const float tw = static_cast<float>(atlas->texture.width);
                            const float th = static_cast<float>(atlas->texture.height);
                            draw->AddImage(
                                (ImTextureID)(intptr_t)atlas->texture.id,
                                ImVec2(x, yIcon),
                                ImVec2(x + kIcon, yIcon + kIcon),
                                ImVec2(rect->x / tw, rect->y / th),
                                ImVec2((rect->x + rect->width) / tw, (rect->y + rect->height) / th));
                        }
                    }
                    draw->AddText(
                        ImVec2(x + kIcon + st.ItemInnerSpacing.x, yText),
                        ImGui::GetColorU32(ImGuiCol_Text),
                        label);
                    if (selected) {
                        draw->AddRectFilled(
                            ImVec2(min.x + 3.0f, max.y - 3.0f),
                            ImVec2(max.x - 3.0f, max.y - 1.0f),
                            ImGui::GetColorU32(ImGuiCol_CheckMark));
                    }

                    if (!enabled) {
                        ImGui::EndDisabled();
                    }
                    if (selected) {
                        ImGui::PopStyleColor(3);
                    }
                    ImGui::PopID();
                    return pressed;
                };

                auto toolSep = []() {
                    ImGui::SameLine(0.0f, 10.0f);
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    const float h = ImGui::GetFrameHeight();
                    ImGui::GetWindowDrawList()->AddLine(
                        ImVec2(p.x, p.y + 4.0f),
                        ImVec2(p.x, p.y + h - 4.0f),
                        ImGui::GetColorU32(ImGuiCol_Separator));
                    ImGui::Dummy(ImVec2(1.0f, h));
                    ImGui::SameLine(0.0f, 10.0f);
                };

                if (toolBtn(
                        "mode-select",
                        "cursor",
                        "Select",
                        editor.mode == slopmap::EditorMode::Select)) {
                    if (selectTool.translating) {
                        selectTool.cancelTranslate(editor);
                    }
                    createTool.reset();
                    punchTool.reset();
                    clipTool.reset();
                    editor.mode = slopmap::EditorMode::Select;
                }
                ImGui::SameLine();
                if (toolBtn(
                        "mode-create",
                        "shape_square",
                        "Create",
                        editor.mode == slopmap::EditorMode::Create)) {
                    if (selectTool.translating) {
                        selectTool.cancelTranslate(editor);
                    }
                    punchTool.reset();
                    clipTool.reset();
                    editor.mode = slopmap::EditorMode::Create;
                    createTool.setStatus(editor);
                }
                ImGui::SameLine();
                if (toolBtn(
                        "mode-place",
                        "package",
                        "Place",
                        editor.mode == slopmap::EditorMode::Place,
                        editor.scene == slopmap::EditorScene::Level)) {
                    if (selectTool.translating) {
                        selectTool.cancelTranslate(editor);
                    }
                    createTool.reset();
                    punchTool.reset();
                    clipTool.reset();
                    editor.mode = slopmap::EditorMode::Place;
                }

                toolSep();
                if (toolBtn(
                        "fill-wire",
                        "shape_square",
                        "Wire",
                        editor.fill == slopmap::PreviewFill::Wireframe)) {
                    editor.fill = slopmap::PreviewFill::Wireframe;
                }
                ImGui::SameLine();
                if (toolBtn(
                        "fill-solid",
                        "box",
                        "Solid",
                        editor.fill == slopmap::PreviewFill::Solid)) {
                    editor.fill = slopmap::PreviewFill::Solid;
                }
                ImGui::SameLine();
                if (toolBtn(
                        "fill-tex",
                        "picture",
                        "Textures",
                        editor.fill == slopmap::PreviewFill::Textures)) {
                    editor.fill = slopmap::PreviewFill::Textures;
                }
                ImGui::SameLine();
                if (toolBtn(
                        "fill-unlit",
                        "world",
                        "Unlit",
                        editor.fill == slopmap::PreviewFill::Unlit)) {
                    if (editor.preview.visValid || editor.reloadVisPreview(assets)) {
                        editor.fill = slopmap::PreviewFill::Unlit;
                    } else {
                        editor.statusMessage = "No VIS; run VIS (falling back to Textures)";
                        editor.fill = slopmap::PreviewFill::Textures;
                    }
                }
                ImGui::SameLine();
                if (toolBtn(
                        "fill-lit",
                        "lightbulb",
                        "Lit",
                        editor.fill == slopmap::PreviewFill::Lit)) {
                    if (editor.preview.litValid || editor.reloadLitBake(assets)) {
                        editor.fill = slopmap::PreviewFill::Lit;
                    } else {
                        editor.statusMessage = "No lightmap bake; run RAD";
                    }
                }
                ImGui::SameLine();
                if (toolBtn(
                        "fill-solid-lit",
                        "contrast",
                        "Solid Lit",
                        editor.fill == slopmap::PreviewFill::SolidLit)) {
                    if (editor.preview.litValid || editor.reloadLitBake(assets)) {
                        editor.fill = slopmap::PreviewFill::SolidLit;
                    } else {
                        editor.statusMessage = "No lightmap bake; run RAD";
                    }
                }
                ImGui::SameLine();
                {
                    const char* xrayLabel = "XRay Off";
                    if (editor.wireframe == slopmap::WireframeOverlay::Visible) {
                        xrayLabel = "XRay Vis";
                    } else if (editor.wireframe == slopmap::WireframeOverlay::All) {
                        xrayLabel = "XRay All";
                    }
                    if (toolBtn(
                            "xray-overlay",
                            "eye",
                            xrayLabel,
                            editor.wireframe != slopmap::WireframeOverlay::Off)) {
                        switch (editor.wireframe) {
                        case slopmap::WireframeOverlay::Off:
                            editor.wireframe = slopmap::WireframeOverlay::Visible;
                            break;
                        case slopmap::WireframeOverlay::Visible:
                            editor.wireframe = slopmap::WireframeOverlay::All;
                            break;
                        case slopmap::WireframeOverlay::All:
                            editor.wireframe = slopmap::WireframeOverlay::Off;
                            break;
                        }
                    }
                }
                ImGui::SameLine();
                if (toolBtn(
                        "ignore-backfaces",
                        "shape_flip_vertical",
                        editor.ignoreBackfaces ? "No Backfaces" : "Backfaces",
                        editor.ignoreBackfaces)) {
                    editor.ignoreBackfaces = !editor.ignoreBackfaces;
                    editor.statusMessage = editor.ignoreBackfaces
                        ? "Ignore backfaces: On"
                        : "Ignore backfaces: Off";
                }

                toolSep();
                if (toolBtn(
                        "play-map",
                        "control_play",
                        "Play",
                        false,
                        editor.scene == slopmap::EditorScene::Level)) {
                    playMap();
                }

                {
                    const char* viewLabel = "Persp";
                    switch (editor.viewPlane) {
                    case slopmap::ViewPlane::Top:
                        viewLabel = "Top";
                        break;
                    case slopmap::ViewPlane::Front:
                        viewLabel = "Front";
                        break;
                    case slopmap::ViewPlane::Side:
                        viewLabel = "Side";
                        break;
                    case slopmap::ViewPlane::PerspectiveY0:
                        break;
                    }
                    char canvasLabel[64];
                    std::snprintf(
                        canvasLabel,
                        sizeof(canvasLabel),
                        "%s  ·  Grid %s %s%s",
                        viewLabel,
                        editor.gridPlaneLabel(),
                        editor.gridSizeLabel(),
                        editor.showGrid ? "" : " (off)");
                    const float textW = ImGui::CalcTextSize(canvasLabel).x;
                    const float avail = ImGui::GetContentRegionAvail().x;
                    if (avail > textW + 12.0f) {
                        ImGui::SameLine(0.0f, avail - textW);
                    } else {
                        ImGui::SameLine(0.0f, 12.0f);
                    }
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("%s", canvasLabel);
                }

                if (editor.mode == slopmap::EditorMode::Select) {
                    if (toolBtn(
                            "sel-brush",
                            "bricks",
                            "Brush",
                            editor.doc().selectionMode == slopmap::SelectionMode::Brush)) {
                        editor.setSelectionMode(slopmap::SelectionMode::Brush);
                    }
                    ImGui::SameLine();
                    if (toolBtn(
                            "sel-face",
                            "shape_handles",
                            "Face",
                            editor.doc().selectionMode == slopmap::SelectionMode::Face)) {
                        editor.setSelectionMode(slopmap::SelectionMode::Face);
                    }
                    ImGui::SameLine();
                    if (toolBtn(
                            "sel-entity",
                            "user",
                            "Entity",
                            editor.doc().selectionMode == slopmap::SelectionMode::Entity)) {
                        editor.setSelectionMode(slopmap::SelectionMode::Entity);
                    }
                    if (editor.doc().selectionMode == slopmap::SelectionMode::Brush) {
                        toolSep();
                        slopengine::BrushRole selRole = editor.createBrushRole;
                        bool haveRole = false;
                        if (editor.doc().activeBrush >= 0 &&
                            editor.doc().activeBrush <
                                static_cast<int>(editor.doc().brushes.size())) {
                            selRole = editor.doc()
                                          .brushes[static_cast<std::size_t>(editor.doc().activeBrush)]
                                          .role;
                            haveRole = true;
                        }
                        if (toolBtn(
                                "sel-role",
                                brushRoleToolbarIcon(selRole),
                                brushRoleToolbarLabel(selRole),
                                false,
                                haveRole || !editor.doc().selectedBrushes.empty())) {
                            if (!editor.doc().selectedBrushes.empty()) {
                                editor.toggleSelectedBrushRole();
                                previewNeedsRebuild = true;
                            }
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                            ImGui::SetTooltip(
                                "Brush role (H). Hull/Window/Water/Hint cut BSP; Detail/Trigger do not.");
                        }
                    }
                    toolSep();
                    if (toolBtn("op-hollow", "box", "Hollow", false)) {
                        editor.hollowThickness = editor.gridSize;
                        editor.showHollowModal = true;
                    }
                    ImGui::SameLine();
                    if (toolBtn("op-punch", "cut", "Punch-out", punchTool.active())) {
                        if (selectTool.translating) {
                            selectTool.cancelTranslate(editor);
                        }
                        createTool.reset();
                        clipTool.reset();
                        punchTool.beginFromSelection(editor);
                    }
                    ImGui::SameLine();
                    if (toolBtn("op-clip", "arrow_divide", "Clip", clipTool.active())) {
                        if (selectTool.translating) {
                            selectTool.cancelTranslate(editor);
                        }
                        createTool.reset();
                        punchTool.reset();
                        clipTool.beginFromSelection(editor);
                    }
                } else if (editor.mode == slopmap::EditorMode::Create) {
                    if (toolBtn(
                            "prim-box",
                            "shape_square",
                            "Box",
                            editor.createPrimitive == slopmap::CreatePrimitive::Box)) {
                        editor.createPrimitive = slopmap::CreatePrimitive::Box;
                    }
                    ImGui::SameLine();
                    if (toolBtn(
                            "prim-cyl",
                            "contrast",
                            "Cylinder",
                            editor.createPrimitive == slopmap::CreatePrimitive::Cylinder)) {
                        editor.createPrimitive = slopmap::CreatePrimitive::Cylinder;
                    }
                    ImGui::SameLine();
                    if (toolBtn(
                            "prim-stairs",
                            "chart_organisation",
                            "Stairs",
                            editor.createPrimitive == slopmap::CreatePrimitive::Stairs)) {
                        editor.createPrimitive = slopmap::CreatePrimitive::Stairs;
                    }
                    toolSep();
                    if (toolBtn(
                            "create-role",
                            brushRoleToolbarIcon(editor.createBrushRole),
                            brushRoleToolbarLabel(editor.createBrushRole),
                            false)) {
                        editor.createBrushRole = nextBrushRole(editor.createBrushRole);
                        editor.statusMessage = std::string("Create role: ") +
                            brushRoleToolbarLabel(editor.createBrushRole);
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                        ImGui::SetTooltip(
                            "Role for new brushes. Hull/Window/Water/Hint cut BSP; Detail/Trigger do not.");
                    }
                } else {
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("Pick a prefab or thing in Library, then click the viewport");
                }
            }
            ImGui::End();
            ImGui::PopStyleVar(3);
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
                            const bool selected = d.isBrushSelected(static_cast<int>(i));
                            const std::string label =
                                d.brushes[i].id + " [" + slopengine::brushRoleName(d.brushes[i].role) + "]";
                            if (selectableWithIcon(
                                    assets,
                                    kIconSet,
                                    brushRoleToolbarIcon(d.brushes[i].role),
                                    label.c_str(),
                                    selected)) {
                                const bool additive =
                                    ImGui::GetIO().KeyShift;
                                editor.selectBrush(static_cast<int>(i), additive);
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
                            const bool selected = d.isEntitySelected(
                                {slopmap::EntityRef::Kind::Instance, static_cast<int>(i)});
                            const std::string label = d.instances[i].id + " (" + d.instances[i].path + ")";
                            if (selectableWithIcon(assets, kIconSet, "package", label.c_str(), selected)) {
                                editor.selectEntity(
                                    {slopmap::EntityRef::Kind::Instance, static_cast<int>(i)},
                                    ImGui::GetIO().KeyShift);
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
                            const bool selected = d.isEntitySelected(
                                {slopmap::EntityRef::Kind::Thing, static_cast<int>(i)});
                            const std::string label =
                                d.things[i].id + " (" +
                                slopengine::thingKindName(d.things[i].kind) + ")";
                            const char* icon = slopengine::thingKindIsLight(d.things[i].kind)
                                ? "lightbulb"
                                : (d.things[i].kind == slopengine::ThingKind::PlayerStart
                                       ? "user"
                                       : "transmit");
                            if (selectableWithIcon(assets, kIconSet, icon, label.c_str(), selected)) {
                                editor.selectEntity(
                                    {slopmap::EntityRef::Kind::Thing, static_cast<int>(i)},
                                    ImGui::GetIO().KeyShift);
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
                            cancelTools(createTool, selectTool, punchTool, clipTool, editor);
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
                constexpr const char* kIcons = kDefaultIconSet;
                const float pad = ImGui::GetStyle().WindowPadding.x;
                const float btn = ImGui::GetFrameHeight();
                const float gridLabelW = 72.0f;
                const float planeW = 28.0f;
                const float roleW = 108.0f;
                const float gap = 10.0f;
                const float controlsW = btn + planeW + gridLabelW + btn * 2.0f + gap + roleW;
                const float controlsX = ImGui::GetWindowWidth() - pad - controlsW;

                const float stageChipW = 120.0f;
                ImGui::PushTextWrapPos(controlsX - stageChipW - 12.0f);
                ImGui::TextUnformatted(
                    editor.statusMessage.empty() ? "Ready" : editor.statusMessage.c_str());
                ImGui::PopTextWrapPos();

                ImGui::SameLine(controlsX - stageChipW);
                {
                    char stageLabel[48];
                    std::snprintf(
                        stageLabel,
                        sizeof(stageLabel),
                        "BSP%s VIS%s RAD%s",
                        editor.compileDirty.bsp ? "*" : "",
                        editor.compileDirty.vis ? "*" : "",
                        editor.compileDirty.rad ? "*" : "");
                    const bool anyDirty = editor.compileDirty.bsp || editor.compileDirty.vis ||
                        editor.compileDirty.rad;
                    if (anyDirty) {
                        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "%s", stageLabel);
                    } else {
                        ImGui::TextDisabled("%s", stageLabel);
                    }
                }

                ImGui::SameLine(controlsX);
                ImGui::BeginGroup();
                {
                    auto iconButton = [&](const char* id, const char* icon) -> bool {
                        ImGui::PushID(id);
                        const bool pressed = ImGui::InvisibleButton("##", ImVec2(btn, btn));
                        const IconAtlas* atlas = assets.getIconAtlas(kIcons);
                        if (atlas != nullptr && atlas->texture.id != 0) {
                            if (const auto rect = findIconRect(*atlas, icon)) {
                                const ImVec2 min = ImGui::GetItemRectMin();
                                const ImVec2 max = ImGui::GetItemRectMax();
                                const float x = min.x + (max.x - min.x - 16.0f) * 0.5f;
                                const float y = min.y + (max.y - min.y - 16.0f) * 0.5f;
                                const float tw = static_cast<float>(atlas->texture.width);
                                const float th = static_cast<float>(atlas->texture.height);
                                const float u0 = rect->x / tw;
                                const float v0 = rect->y / th;
                                const float u1 = (rect->x + rect->width) / tw;
                                const float v1 = (rect->y + rect->height) / th;
                                ImU32 tint = ImGui::GetColorU32(ImGuiCol_Text);
                                if (!editor.showGrid && std::strcmp(id, "grid-toggle") == 0) {
                                    tint = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                                }
                                ImGui::GetWindowDrawList()->AddImage(
                                    (ImTextureID)(intptr_t)atlas->texture.id,
                                    ImVec2(x, y),
                                    ImVec2(x + 16.0f, y + 16.0f),
                                    ImVec2(u0, v0),
                                    ImVec2(u1, v1),
                                    tint);
                            }
                        }
                        ImGui::PopID();
                        return pressed;
                    };

                    if (iconButton("grid-toggle", "table")) {
                        editor.showGrid = !editor.showGrid;
                        editor.statusMessage = editor.showGrid ? "Grid: on" : "Grid: off";
                    }

                    ImGui::SameLine(0.0f, 0.0f);
                    if (ImGui::InvisibleButton("##grid-plane", ImVec2(planeW, btn))) {
                        editor.cycleGridPlane();
                    }
                    {
                        const ImVec2 min = ImGui::GetItemRectMin();
                        const float textY =
                            min.y + (btn - ImGui::GetTextLineHeight()) * 0.5f;
                        const float textW = ImGui::CalcTextSize(editor.gridPlaneLabel()).x;
                        ImGui::GetWindowDrawList()->AddText(
                            ImVec2(min.x + (planeW - textW) * 0.5f, textY),
                            ImGui::GetColorU32(ImGuiCol_Text),
                            editor.gridPlaneLabel());
                    }

                    ImGui::SameLine(0.0f, 0.0f);
                    char gridLabel[32];
                    std::snprintf(gridLabel, sizeof(gridLabel), "%s", editor.gridSizeLabel());
                    ImGui::Dummy(ImVec2(gridLabelW, btn));
                    {
                        const ImVec2 min = ImGui::GetItemRectMin();
                        const float textY =
                            min.y + (btn - ImGui::GetTextLineHeight()) * 0.5f;
                        ImGui::GetWindowDrawList()->AddText(
                            ImVec2(min.x, textY),
                            ImGui::GetColorU32(ImGuiCol_Text),
                            gridLabel);
                    }

                    ImGui::SameLine(0.0f, 0.0f);
                    if (iconButton("grid-finer", "bullet_toggle_minus")) {
                        editor.cycleGrid(1);
                    }
                    ImGui::SameLine(0.0f, 0.0f);
                    if (iconButton("grid-coarser", "bullet_toggle_plus")) {
                        editor.cycleGrid(-1);
                    }

                    ImGui::SameLine(0.0f, gap);
                    if (editor.mode == slopmap::EditorMode::Create) {
                        if (ImGui::SmallButton(brushRoleToolbarLabel(editor.createBrushRole))) {
                            editor.createBrushRole = nextBrushRole(editor.createBrushRole);
                            editor.statusMessage = std::string("Create role: ") +
                                brushRoleToolbarLabel(editor.createBrushRole);
                        }
                    } else {
                        ImGui::Dummy(ImVec2(roleW, btn));
                    }
                }
                ImGui::EndGroup();
            }
            ImGui::End();
            ImGui::PopStyleVar();
        }

        if (editor.showHollowModal) {
            ImGui::OpenPopup("Hollow Brush");
            editor.showHollowModal = false;
        }
        if (ImGui::BeginPopupModal("Hollow Brush", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Wall thickness");
            ImGui::DragFloat("##hollowthick", &editor.hollowThickness, 0.01f, 0.01f, 10.0f, "%.3f");
            if (ImGui::Button("Hollow", ImVec2(120, 0))) {
                applyHollow(editor);
                previewNeedsRebuild = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (editor.showPrimitiveParamsModal) {
            ImGui::OpenPopup("Primitive Params");
        }
        if (ImGui::BeginPopupModal("Primitive Params", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (editor.createPrimitive == slopmap::CreatePrimitive::Cylinder) {
                ImGui::TextUnformatted("Cylinder sides");
                ImGui::InputInt("##cylsides", &editor.createCylinderSides);
                if (editor.createCylinderSides < 3) {
                    editor.createCylinderSides = 3;
                }
            } else if (editor.createPrimitive == slopmap::CreatePrimitive::Stairs) {
                ImGui::TextUnformatted("Stair steps");
                ImGui::InputInt("##stairsteps", &editor.createStairsSteps);
                if (editor.createStairsSteps < 1) {
                    editor.createStairsSteps = 1;
                }
            }
            if (ImGui::Button("Create", ImVec2(120, 0))) {
                createTool.commitPending(editor);
                previewNeedsRebuild = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                createTool.reset();
                editor.showPrimitiveParamsModal = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
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
                    cancelTools(createTool, selectTool, punchTool, clipTool, editor);
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
                    cancelTools(createTool, selectTool, punchTool, clipTool, editor);
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
                cancelTools(createTool, selectTool, punchTool, clipTool, editor);
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
                cancelTools(createTool, selectTool, punchTool, clipTool, editor);
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

        if (compile.showOptionsModal) {
            ImGui::OpenPopup("RAD Options");
            compile.showOptionsModal = false;
        }
        if (ImGui::BeginPopupModal("RAD Options", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            constexpr const char* kIcons = kDefaultIconSet;
            ImGui::DragFloat(
                "Luxels per meter",
                &compile.radOptions.luxelsPerMeter,
                0.5f,
                1.0f,
                128.0f,
                "%.1f");
            ImGui::InputInt("Bounces", &compile.radOptions.bounces);
            if (compile.radOptions.bounces < 0) {
                compile.radOptions.bounces = 0;
            }
            ImGui::InputInt("Samples", &compile.radOptions.samples);
            if (compile.radOptions.samples < 1) {
                compile.radOptions.samples = 1;
            }
            if (ImGui::RadioButton("GPU", compile.radOptions.preferGpu)) {
                compile.radOptions.preferGpu = true;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("CPU", !compile.radOptions.preferGpu)) {
                compile.radOptions.preferGpu = false;
            }
            if (buttonWithIcon(assets, kIcons, "accept", "OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (compile.showOutputWindow) {
            ImGui::SetNextWindowSize(ImVec2(720.0f, 360.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Compile Output", &compile.showOutputWindow)) {
                if (monoFont != nullptr) {
                    ImGui::PushFont(monoFont, 0.0f);
                }
                if (ImGui::BeginChild(
                        "##compile_log",
                        ImVec2(0.0f, 0.0f),
                        ImGuiChildFlags_Borders,
                        ImGuiWindowFlags_HorizontalScrollbar)) {
                    for (const std::string& line : compile.logLines()) {
                        ImGui::TextUnformatted(line.c_str());
                    }
                    if (compile.logDirty()) {
                        if (compile.logAutoScroll()) {
                            ImGui::SetScrollHereY(1.0f);
                        }
                        compile.clearLogDirty();
                    } else {
                        const float maxY = ImGui::GetScrollMaxY();
                        compile.setLogAutoScroll(ImGui::GetScrollY() >= maxY - 1.0f);
                    }
                }
                ImGui::EndChild();
                if (monoFont != nullptr) {
                    ImGui::PopFont();
                }
            }
            ImGui::End();
        }

        rlImGuiEnd();
        EndDrawing();
    }

    compile.shutdown();
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
