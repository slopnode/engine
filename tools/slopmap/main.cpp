#include "camera.hpp"
#include "compile.hpp"
#include "create_tool.hpp"
#include "editor.hpp"
#include "layout.hpp"
#include "editor_settings.hpp"
#include "material_browser.hpp"
#include "texture_panel.hpp"
#include "brush_panel.hpp"
#include "thing_panel.hpp"
#include "place_tool.hpp"
#include "punch_tool.hpp"
#include "clip_tool.hpp"
#include "thing_draw.hpp"
#include "particle_preview.hpp"
#include "prefab_browser.hpp"
#include "preview.hpp"
#include "select_tool.hpp"

#include "map/thing.hpp"

#include "render/skybox.hpp"
#include "render/skybox_render.hpp"
#include "assets/asset_store.hpp"
#include "core/package.hpp"
#include "core/package_meta.hpp"
#include "core/user_paths.hpp"
#include "game/app_config.hpp"
#include "map/csg_script.hpp"
#include "map/map_handler_registry.hpp"
#include "map/thing_def_registry.hpp"
#include "ui/icon_ui.hpp"
#include "ui/imgui_fonts.hpp"

#include "imgui.h"
#include "rlImGui.h"

#include <raylib.h>
#include <rlgl.h>
#include <s7.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace {

struct ToolConfig {
    slopengine::AppConfig mount;
    std::filesystem::path target;
};

struct ThingCatalogFolder {
    std::map<std::string, ThingCatalogFolder> folders;
    std::vector<const slopengine::ThingDef*> leaves;
};

std::vector<std::string> splitThingCatalogPath(std::string_view path) {
    std::vector<std::string> segments;
    std::string current;
    for (char c : path) {
        if (c == '/') {
            if (!current.empty()) {
                segments.push_back(std::move(current));
                current.clear();
            }
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty()) {
        segments.push_back(std::move(current));
    }
    return segments;
}

void insertThingCatalogDef(ThingCatalogFolder& root, const slopengine::ThingDef* def) {
    if (def == nullptr) {
        return;
    }
    ThingCatalogFolder* node = &root;
    for (const std::string& segment : splitThingCatalogPath(def->path)) {
        node = &node->folders[segment];
    }
    node->leaves.push_back(def);
}

void sortThingCatalogFolder(ThingCatalogFolder& node) {
    std::sort(
        node.leaves.begin(),
        node.leaves.end(),
        [](const slopengine::ThingDef* a, const slopengine::ThingDef* b) {
            if (a == nullptr || b == nullptr) {
                return a != nullptr;
            }
            if (a->label != b->label) {
                return a->label < b->label;
            }
            return a->id < b->id;
        });
    for (auto& [name, child] : node.folders) {
        (void)name;
        sortThingCatalogFolder(child);
    }
}

ThingCatalogFolder buildThingCatalogFolder(const std::vector<const slopengine::ThingDef*>& defs) {
    ThingCatalogFolder root;
    for (const slopengine::ThingDef* def : defs) {
        insertThingCatalogDef(root, def);
    }
    sortThingCatalogFolder(root);
    return root;
}

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
        d.selectionMode == slopmap::SelectionMode::Brush     ? "Brush"
            : d.selectionMode == slopmap::SelectionMode::Face ? "Face"
            : d.selectionMode == slopmap::SelectionMode::Edge ? "Edge"
            : d.selectionMode == slopmap::SelectionMode::Vert ? "Vert"
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
    } else if (d.selectionMode == slopmap::SelectionMode::Edge && !d.selectedEdges.empty()) {
        ImGui::Text("Edges: %d", static_cast<int>(d.selectedEdges.size()));
        if (d.activeEdge.valid() && d.activeEdge.brush < static_cast<int>(d.brushes.size())) {
            const auto& brush = d.brushes[static_cast<std::size_t>(d.activeEdge.brush)];
            ImGui::Text("Active: %s f%de%d", brush.id.c_str(), d.activeEdge.face, d.activeEdge.edge);
        }
    } else if (d.selectionMode == slopmap::SelectionMode::Vert && !d.selectedVerts.empty()) {
        ImGui::Text("Verts: %d", static_cast<int>(d.selectedVerts.size()));
        if (d.activeVert.valid() && d.activeVert.brush < static_cast<int>(d.brushes.size())) {
            const auto& brush = d.brushes[static_cast<std::size_t>(d.activeVert.brush)];
            ImGui::Text("Active: %s f%dv%d", brush.id.c_str(), d.activeVert.face, d.activeVert.vert);
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
    const slopmap::ClipTool& clipTool,
    const slopmap::InfiniteGrid& infiniteGrid,
    slopmap::ParticlePreviewState& particlePreview) {
    ClearBackground(Color{32, 34, 38, 255});
    const bool ortho = camera.projection == CAMERA_ORTHOGRAPHIC;
    const double prevNear = rlGetCullDistanceNear();
    const double prevFar = rlGetCullDistanceFar();
    if (ortho) {
        rlSetClipPlanes(-4000.0, 4000.0);
    }
    BeginMode3D(camera);
    if (ortho) {
        rlDisableBackfaceCulling();
    }
    const Vector3 eye = camera.position;
    const Vector3 viewDir{
        camera.target.x - camera.position.x,
        camera.target.y - camera.position.y,
        camera.target.z - camera.position.z,
    };
    const Vector3 cameraForward = [&viewDir]() {
        const float lenSq = viewDir.x * viewDir.x + viewDir.y * viewDir.y + viewDir.z * viewDir.z;
        if (lenSq < 1e-8f) {
            return Vector3{0.0f, 0.0f, 1.0f};
        }
        const float inv = 1.0f / std::sqrt(lenSq);
        return Vector3{viewDir.x * inv, viewDir.y * inv, viewDir.z * inv};
    }();
    const Vector3 lineViewDir = ortho ? viewDir : Vector3{0.0f, 0.0f, 0.0f};
    const slopmap::GridPlane drawPlane =
        slopmap::gridPlaneForView(editor.viewPlane, editor.gridPlane);
    const float aspect = editor.contentViewport.height > 0.0f
        ? editor.contentViewport.width / editor.contentViewport.height
        : 1.0f;
    const float metersPerPixel = slopmap::gridMetersPerPixel(
        ortho,
        editor.camera.orthoHalfHeight,
        camera.fovy,
        editor.contentViewport.height,
        drawPlane,
        eye,
        viewDir);
    const float lineWidth = std::clamp(metersPerPixel * 1.25f, 0.002f, 0.08f);
    const float axisWidth = lineWidth * 1.75f;
    if (editor.showGrid && infiniteGrid.ready()) {
        float fadeRadius = 64.0f;
        if (ortho) {
            fadeRadius = std::max(48.0f, editor.camera.orthoHalfHeight * aspect * 4.0f);
        } else {
            float planeDist = std::fabs(eye.y);
            switch (drawPlane) {
            case slopmap::GridPlane::XY:
                planeDist = std::fabs(eye.z);
                break;
            case slopmap::GridPlane::YZ:
                planeDist = std::fabs(eye.x);
                break;
            case slopmap::GridPlane::XZ:
            default:
                planeDist = std::fabs(eye.y);
                break;
            }
            fadeRadius = std::clamp(std::max(planeDist * 40.0f, 80.0f), 80.0f, 2500.0f);
        }
        infiniteGrid.draw(drawPlane, eye, editor.gridSize, fadeRadius);
    }
    slopmap::drawThickLine3D(
        {-100, 0, 0}, {100, 0, 0}, Color{180, 60, 60, 255}, axisWidth, eye, lineViewDir);
    slopmap::drawThickLine3D(
        {0, -100, 0}, {0, 100, 0}, Color{60, 180, 60, 255}, axisWidth, eye, lineViewDir);
    slopmap::drawThickLine3D(
        {0, 0, -100}, {0, 0, 100}, Color{60, 60, 180, 255}, axisWidth, eye, lineViewDir);

    const slopmap::EditorDocument& d = editor.doc();
    const std::vector<int> selectedBrushes =
        d.selectionMode == slopmap::SelectionMode::Brush ? d.selectedBrushes
                                                         : std::vector<int>{};
    const bool fillWire = editor.fill == slopmap::PreviewFill::Wireframe;
    const bool xrayAll = editor.wireframe == slopmap::WireframeOverlay::All;
    const bool xrayVisible = editor.wireframe == slopmap::WireframeOverlay::Visible;
    const slopengine::SkyboxSettings* editorSky = nullptr;
    slopengine::SkyboxSettings editorSkySettings{};
    for (const slopengine::Thing& thing : d.things) {
        if (thing.kind == slopengine::ThingKind::Skybox) {
            editorSkySettings = slopengine::skyboxSettingsFromThing(thing, &assets);
            editorSky = &editorSkySettings;
            break;
        }
    }
    if (editorSky != nullptr) {
        slopengine::SkyboxShaderState& skyShaderState = slopengine::ensureSkyboxShaders(assets);
        slopengine::drawSkyboxBackground(camera, assets, skyShaderState, *editorSky);
    }
    editor.preview.draw(
        editor.fill,
        editor.wireframe,
        d.brushes,
        editor.expandedInstanceBrushes,
        selectedBrushes,
        eye,
        cameraForward,
        lineWidth,
        &camera,
        &assets,
        &d.things);

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
    const bool drawEdgeSelection =
        d.selectionMode == slopmap::SelectionMode::Edge && !d.selectedEdges.empty();
    const bool drawVertSelection =
        d.selectionMode == slopmap::SelectionMode::Vert && !d.selectedVerts.empty();
    const bool drawEntitySelection =
        !fillWire && !xrayAll && !xrayVisible &&
        d.selectionMode == slopmap::SelectionMode::Entity && !d.selectedEntities.empty();
    if (drawBrushSelection || drawFaceSelection || drawEdgeSelection || drawVertSelection ||
        drawEntitySelection) {
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

        if (drawEdgeSelection) {
            std::unordered_set<int> outlinedBrushes;
            for (const slopmap::EdgeRef& ref : d.selectedEdges) {
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
            for (const slopmap::EdgeRef& ref : d.selectedEdges) {
                if (!ref.valid() || ref.brush >= static_cast<int>(d.brushes.size())) {
                    continue;
                }
                const auto& brush = d.brushes[static_cast<std::size_t>(ref.brush)];
                if (ref.face >= static_cast<int>(brush.faces.size())) {
                    continue;
                }
                const auto& face = brush.faces[static_cast<std::size_t>(ref.face)];
                if (face.vertices.size() < 2 || ref.edge < 0 ||
                    ref.edge >= static_cast<int>(face.vertices.size())) {
                    continue;
                }
                const Vector3& a = face.vertices[static_cast<std::size_t>(ref.edge)];
                const Vector3& b =
                    face.vertices[static_cast<std::size_t>((ref.edge + 1) % face.vertices.size())];
                const bool active = ref == d.activeEdge;
                const Color edgeColor =
                    active ? Color{80, 220, 255, 255} : Color{80, 160, 200, 255};
                slopmap::drawThickLine3D(a, b, edgeColor, lineWidth * 1.5f, eye);
            }
        }

        if (drawVertSelection) {
            std::unordered_set<int> outlinedBrushes;
            for (const slopmap::VertRef& ref : d.selectedVerts) {
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
            for (const slopmap::VertRef& ref : d.selectedVerts) {
                if (!ref.valid() || ref.brush >= static_cast<int>(d.brushes.size())) {
                    continue;
                }
                const auto& brush = d.brushes[static_cast<std::size_t>(ref.brush)];
                if (ref.face >= static_cast<int>(brush.faces.size())) {
                    continue;
                }
                const auto& face = brush.faces[static_cast<std::size_t>(ref.face)];
                if (ref.vert < 0 || ref.vert >= static_cast<int>(face.vertices.size())) {
                    continue;
                }
                const Vector3& pos = face.vertices[static_cast<std::size_t>(ref.vert)];
                const bool active = ref == d.activeVert;
                const Color vertColor =
                    active ? Color{255, 220, 80, 255} : Color{80, 220, 255, 255};
                const float radius = active ? 0.08f : 0.06f;
                DrawSphere(pos, radius, vertColor);
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
    slopmap::drawThings(assets, d.things, selectedThings, camera, editor.showGizmos);
    if (particlePreview.enabled) {
        slopmap::drawParticlePreview(particlePreview, assets, d.things, camera);
    }

    createTool.drawPreview();
    punchTool.drawPreview();
    clipTool.drawPreview(editor, eye, lineWidth);
    if (ortho) {
        rlEnableBackfaceCulling();
    }
    EndMode3D();
    if (ortho) {
        rlSetClipPlanes(prevNear, prevFar);
    }
    slopmap::drawOrientationWidget(
        camera,
        static_cast<float>(GetRenderWidth()),
        static_cast<float>(GetRenderHeight()));
}

void beginThingKind(
    slopmap::Editor& editor,
    slopengine::ThingKind kind,
    slopmap::CreateTool& createTool,
    slopmap::PlacePresentation presentation = slopmap::PlacePresentation::None) {
    editor.placeTarget = slopmap::PlaceTarget::Thing;
    editor.placeThingKind = kind;
    editor.placeThingType.clear();
    editor.placePresentation = presentation;
    editor.placePrefabPath.clear();
    editor.mode = slopmap::EditorMode::Place;
    createTool.reset();
    const char* label = slopengine::thingKindName(kind);
    if (kind == slopengine::ThingKind::Prop) {
        if (presentation == slopmap::PlacePresentation::Sprite) {
            label = "sprite";
        } else if (presentation == slopmap::PlacePresentation::Geo) {
            label = "geo";
        }
    }
    if (slopengine::thingKindNeedsPresentation(kind)) {
        editor.statusMessage =
            std::string("Place ") + label + ": click viewport, then set asset in Properties";
    } else {
        editor.statusMessage = std::string("Place ") + label + ": click the viewport";
    }
}

void beginThingDef(
    slopmap::Editor& editor,
    const slopengine::ThingDef& def,
    slopmap::CreateTool& createTool) {
    editor.placeTarget = slopmap::PlaceTarget::Thing;
    editor.placeThingKind = def.kind;
    editor.placeThingType = def.id;
    editor.placePresentation = slopmap::PlacePresentation::None;
    if (def.kind == slopengine::ThingKind::Prop) {
        if (!def.geo.empty()) {
            editor.placePresentation = slopmap::PlacePresentation::Geo;
        } else {
            editor.placePresentation = slopmap::PlacePresentation::Sprite;
        }
    }
    editor.placePrefabPath.clear();
    editor.mode = slopmap::EditorMode::Place;
    createTool.reset();
    editor.statusMessage =
        std::string("Place ") + def.label + " (" + def.id + "): click the viewport";
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
    selectTool.cancelTranslate(editor);
    selectTool.cancelRotate(editor);
}

slopengine::BrushRole nextBrushRole(slopengine::BrushRole role) {
    switch (role) {
    case slopengine::BrushRole::Hull:
        return slopengine::BrushRole::Detail;
    case slopengine::BrushRole::Detail:
        return slopengine::BrushRole::Door;
    case slopengine::BrushRole::Door:
        return slopengine::BrushRole::Hint;
    case slopengine::BrushRole::Hint:
        return slopengine::BrushRole::Trigger;
    case slopengine::BrushRole::Trigger:
        return slopengine::BrushRole::Water;
    case slopengine::BrushRole::Water:
        return slopengine::BrushRole::Window;
    case slopengine::BrushRole::Window:
        return slopengine::BrushRole::Transparent;
    case slopengine::BrushRole::Transparent:
        return slopengine::BrushRole::Hull;
    }
    return slopengine::BrushRole::Hull;
}

const char* brushRoleToolbarLabel(slopengine::BrushRole role) {
    return slopengine::brushRoleName(role);
}

const char* brushRoleToolbarIcon(slopengine::BrushRole role) {
    switch (role) {
    case slopengine::BrushRole::Hull:
        return "cut";
    case slopengine::BrushRole::Detail:
        return "brick";
    case slopengine::BrushRole::Door:
        return "door";
    case slopengine::BrushRole::Hint:
        return "lightning";
    case slopengine::BrushRole::Trigger:
        return "flag_green";
    case slopengine::BrushRole::Water:
        return "weather_rain";
    case slopengine::BrushRole::Window:
        return "contrast";
    case slopengine::BrushRole::Transparent:
        return "palette";
    }
    return "brick";
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
    editor.prepareEdit();
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
        auto next = slopengine::hollowBrushBox(
            source, editor.hollowThickness, allocateId, editor.hollowOutward);
        if (next.empty()) {
            continue;
        }
        role = source.role;
        removeIndices.push_back(index);
        walls.insert(walls.end(), std::make_move_iterator(next.begin()), std::make_move_iterator(next.end()));
    }
    if (walls.empty()) {
        editor.abortEdit();
        editor.statusMessage = editor.hollowOutward
            ? "Hollow failed (need box brushes)"
            : "Hollow failed (need box brushes, thickness < half size)";
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
    editor.endEdit();
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
    const std::string previewRoot = packages[static_cast<std::size_t>(current)].root().string();
    const char* preview = !previewId.empty() ? previewId.c_str() : previewRoot.c_str();
    ImGui::TextUnformatted("Save to package");
    if (ImGui::BeginCombo("##writepackage", preview)) {
        for (std::size_t i = 0; i < packages.size(); ++i) {
            const slopengine::Package& package = packages[i];
            const std::string& id = package.meta().id;
            const std::string rootLabel = package.root().string();
            const char* label = !id.empty() ? id.c_str() : rootLabel.c_str();
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
    if (!IsWindowReady()) {
        std::cerr << "slopmap: failed to initialize window\n";
        return 1;
    }
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

    slopengine::loadPackageMapHandlers(scheme, assets);
    slopengine::loadPackageThings(scheme, assets);

    slopmap::Editor editor;
    editor.writePackageRoot = config->target;
    editor.scheme = scheme;
    slopmap::ParticlePreviewState particlePreview;
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
    slopmap::InfiniteGrid infiniteGrid;
    if (!infiniteGrid.load(assets)) {
        std::cerr << "slopmap: warning: infinite grid shader failed to load\n";
    }
    slopmap::MaterialBrowser materialBrowser;
    slopmap::TexturePanel texturePanel;
    slopmap::ThingPanel thingPanel;
    slopmap::BrushPanel brushPanel;
    slopmap::PrefabBrowser prefabBrowser;
    slopmap::CompileController compile;
    slopmap::EditorSettings editorSettings = slopmap::EditorSettings::loadOrDefault();
    materialBrowser.rescan(assets);
    prefabBrowser.rescan(assets);
    bool previewNeedsRebuild = false;
    bool compileWasRunning = false;
    bool compileRunIncludesRad = false;
    bool showPreferencesModal = false;
    char mapNameBuf[128] = {};
    char prefabPathBuf[256] = {};
    char thumbCachePathBuf[512] = {};
    RenderTexture2D contentTargets[slopmap::kViewportCount]{};

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
        slopmap::ContentViewports panes = slopmap::splitContentViewports(
            viewport, editor.viewportLayout, editor.activeViewport);

        const Vector2 mouse = GetMousePosition();
        const int hoveredPane = slopmap::hitTestContentViewport(mouse, panes);
        const bool mouseInContent = hoveredPane >= 0;
        if (!uiWantsMouse && hoveredPane >= 0 &&
            (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
             IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
            editor.setActiveViewport(hoveredPane);
        }

        int flyPane = hoveredPane;
        if (IsCursorHidden()) {
            flyPane = editor.activeViewport;
        }
        const bool allowFly =
            !uiWantsKeyboard && !uiWantsMouse && (mouseInContent || IsCursorHidden());
        if (flyPane >= 0 && allowFly) {
            editor.viewports[static_cast<std::size_t>(flyPane)].camera.update(true);
        } else {
            editor.viewports[static_cast<std::size_t>(editor.activeViewport)].camera.update(false);
        }
        editor.syncActiveCameraFromBank();

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
                        "arrow_undo",
                        "Undo",
                        "Ctrl+Z",
                        false,
                        editor.canUndo() || selectTool.active())) {
                    if (selectTool.active()) {
                        selectTool.cancelTranslate(editor);
                        selectTool.cancelRotate(editor);
                        previewNeedsRebuild = true;
                    } else if (editor.undo(assets)) {
                        previewNeedsRebuild = true;
                    }
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "arrow_redo",
                        "Redo",
                        "Ctrl+Shift+Z",
                        false,
                        editor.canRedo())) {
                    if (selectTool.active()) {
                        selectTool.cancelTranslate(editor);
                        selectTool.cancelRotate(editor);
                    }
                    if (editor.redo(assets)) {
                        previewNeedsRebuild = true;
                    }
                }
                ImGui::Separator();
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
                    selectTool.cancelTranslate(editor);
                    selectTool.cancelRotate(editor);
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
                    selectTool.cancelTranslate(editor);
                    selectTool.cancelRotate(editor);
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
                        "connect",
                        "Selection: Edge",
                        nullptr,
                        editor.doc().selectionMode == slopmap::SelectionMode::Edge,
                        editor.mode == slopmap::EditorMode::Select)) {
                    editor.setSelectionMode(slopmap::SelectionMode::Edge);
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "vector",
                        "Selection: Vert",
                        nullptr,
                        editor.doc().selectionMode == slopmap::SelectionMode::Vert,
                        editor.mode == slopmap::EditorMode::Select)) {
                    editor.setSelectionMode(slopmap::SelectionMode::Vert);
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
                        "flag_green",
                        "Convert to Trigger",
                        nullptr,
                        false,
                        editor.doc().selectionMode == slopmap::SelectionMode::Brush &&
                            !editor.doc().selectedBrushes.empty())) {
                    editor.convertSelectedBrushesToTriggers();
                    previewNeedsRebuild = true;
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "door",
                        "Set as Door",
                        nullptr,
                        false,
                        editor.doc().selectionMode == slopmap::SelectionMode::Brush &&
                            !editor.doc().selectedBrushes.empty())) {
                    editor.setSelectedBrushesAsDoors();
                    previewNeedsRebuild = true;
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "brick",
                        "Convert to Mover",
                        nullptr,
                        false,
                        editor.doc().selectionMode == slopmap::SelectionMode::Brush &&
                            !editor.doc().selectedBrushes.empty())) {
                    editor.convertSelectedBrushesToMovers();
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
                if (editor.doc().selectionMode == slopmap::SelectionMode::Brush) {
                    if (menuItemWithIcon(
                            assets,
                            kIcons,
                            "box",
                            "Hollow...",
                            nullptr,
                            false,
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
                            !editor.doc().selectedBrushes.empty())) {
                        selectTool.cancelTranslate(editor);
                        selectTool.cancelRotate(editor);
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
                            !editor.doc().selectedBrushes.empty())) {
                        selectTool.cancelTranslate(editor);
                        selectTool.cancelRotate(editor);
                        createTool.reset();
                        punchTool.reset();
                        clipTool.beginFromSelection(editor);
                    }
                }
                ImGui::Separator();
                if (menuItemWithIcon(assets, kIcons, "error", "Validate Brushes")) {
                    editor.showValidateBrushesWindow = true;
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
                ImGui::Separator();
                if (menuItemWithIcon(assets, kIcons, "wrench", "Preferences…")) {
                    const std::string path = editorSettings.resolvedThumbnailCachePath().string();
                    std::snprintf(
                        thumbCachePathBuf,
                        sizeof(thumbCachePathBuf),
                        "%s",
                        path.c_str());
                    showPreferencesModal = true;
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
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "application_view_tile",
                        "Quad View",
                        "Ctrl+Alt+Q",
                        editor.viewportLayout == slopmap::ViewportLayout::Quad)) {
                    editor.toggleViewportLayout();
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
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "shape_handles",
                        "Show Gizmos",
                        nullptr,
                        editor.showGizmos)) {
                    editor.showGizmos = !editor.showGizmos;
                    editor.statusMessage = editor.showGizmos ? "Gizmos: on" : "Gizmos: off";
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "control_play",
                        "Preview Particles",
                        nullptr,
                        editor.particlePreviewEnabled)) {
                    editor.particlePreviewEnabled = !editor.particlePreviewEnabled;
                    editor.statusMessage = editor.particlePreviewEnabled
                        ? "Particle preview: on"
                        : "Particle preview: off";
                }
                if (beginMenuWithIcon(assets, kIcons, "arrow_refresh", "Translate Snap")) {
                    if (menuItemWithIcon(
                            assets,
                            kIcons,
                            "arrow_right",
                            "Offset",
                            "O",
                            editor.translateSnapMode == slopmap::TranslateSnapMode::Offset)) {
                        editor.translateSnapMode = slopmap::TranslateSnapMode::Offset;
                        editor.statusMessage = "Translate snap: Offset";
                    }
                    if (menuItemWithIcon(
                            assets,
                            kIcons,
                            "anchor",
                            "Absolute",
                            "O",
                            editor.translateSnapMode == slopmap::TranslateSnapMode::Absolute)) {
                        editor.translateSnapMode = slopmap::TranslateSnapMode::Absolute;
                        editor.statusMessage = "Translate snap: Absolute";
                    }
                    ImGui::EndMenu();
                }
                if (beginMenuWithIcon(assets, kIcons, "world", "Transform Space")) {
                    if (menuItemWithIcon(
                            assets,
                            kIcons,
                            "world",
                            "Global",
                            nullptr,
                            editor.transformSpace == slopmap::TransformSpace::Global)) {
                        editor.transformSpace = slopmap::TransformSpace::Global;
                        editor.statusMessage = "Transform space: Global";
                    }
                    if (menuItemWithIcon(
                            assets,
                            kIcons,
                            "vector",
                            "Relative",
                            nullptr,
                            editor.transformSpace == slopmap::TransformSpace::Relative)) {
                        editor.transformSpace = slopmap::TransformSpace::Relative;
                        editor.statusMessage = "Transform space: Relative";
                    }
                    ImGui::EndMenu();
                }
                if (beginMenuWithIcon(assets, kIcons, "arrow_rotate_clockwise", "Rotate Snap")) {
                    constexpr float kRotateSnaps[] = {0.0f, 1.0f, 5.0f, 15.0f, 45.0f, 90.0f};
                    constexpr const char* kRotateSnapLabels[] = {
                        "Off", "1 deg", "5 deg", "15 deg", "45 deg", "90 deg"};
                    for (std::size_t i = 0; i < sizeof(kRotateSnaps) / sizeof(kRotateSnaps[0]); ++i) {
                        const float snap = kRotateSnaps[i];
                        if (menuItemWithIcon(
                                assets,
                                kIcons,
                                "arrow_rotate_clockwise",
                                kRotateSnapLabels[i],
                                nullptr,
                                editor.rotateSnapDegrees == snap)) {
                            editor.rotateSnapDegrees = snap;
                            editor.statusMessage = snap <= 0.0f
                                ? "Rotate snap: off"
                                : std::string("Rotate snap: ") + kRotateSnapLabels[i];
                        }
                    }
                    ImGui::EndMenu();
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
                if (beginMenuWithIcon(assets, kIcons, "picture", "Preview")) {
                    ImGui::TextDisabled("CSG");
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
                    ImGui::Separator();
                    ImGui::TextDisabled("VIS / RAD");
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
                    ImGui::EndMenu();
                }
                if (beginMenuWithIcon(assets, kIcons, "eye", "Wires")) {
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
                            "Vis",
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
                const char* runFacLabel =
                    editor.compileDirty.fac ? "Run FAC *" : "Run FAC";
                const char* runVisLabel =
                    editor.compileDirty.vis ? "Run VIS *" : "Run VIS";
                const char* runRadLabel =
                    editor.compileDirty.rad ? "Run RAD *" : "Run RAD";
                const bool anyCompileDirty = editor.compileDirty.bsp || editor.compileDirty.fac ||
                    editor.compileDirty.vis || editor.compileDirty.rad;
                const char* runAllLabel = anyCompileDirty ? "Run All *" : "Run All";
                if (menuItemWithIcon(
                        assets, kIcons, "brick", runBspLabel, nullptr, false, canRun)) {
                    startCompile({slopmap::CompileStage::Bsp});
                }
                if (menuItemWithIcon(
                        assets, kIcons, "shape_handles", runFacLabel, nullptr, false, canRun)) {
                    startCompile({slopmap::CompileStage::Fac});
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
                        slopmap::CompileStage::Fac,
                        slopmap::CompileStage::Vis,
                        slopmap::CompileStage::Rad,
                    });
                }
                ImGui::Separator();
                const bool canClean =
                    canRun && editor.scene == slopmap::EditorScene::Level &&
                    !editor.levelDoc.assetPath.empty() &&
                    editor.levelDoc.assetPath != "untitled";
                if (menuItemWithIcon(
                        assets, kIcons, "brick_delete", "Clean BSP", nullptr, false, canClean)) {
                    editor.cleanCompileData(assets, {slopmap::CompileStage::Bsp});
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "shape_handles",
                        "Clean FAC",
                        nullptr,
                        false,
                        canClean)) {
                    editor.cleanCompileData(assets, {slopmap::CompileStage::Fac});
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "chart_organisation_delete",
                        "Clean VIS",
                        nullptr,
                        false,
                        canClean)) {
                    editor.cleanCompileData(assets, {slopmap::CompileStage::Vis});
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "lightbulb_delete",
                        "Clean RAD",
                        nullptr,
                        false,
                        canClean)) {
                    editor.cleanCompileData(assets, {slopmap::CompileStage::Rad});
                }
                if (menuItemWithIcon(
                        assets, kIcons, "bin", "Clean All Compile Data", nullptr, false, canClean)) {
                    editor.cleanCompileData(
                        assets,
                        {
                            slopmap::CompileStage::Bsp,
                            slopmap::CompileStage::Fac,
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
                if (completedStage == slopmap::CompileStage::Fac) {
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
            const bool ctrlDown =
                IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
            const bool shiftDown =
                IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            if (ctrlDown && IsKeyPressed(KEY_Z)) {
                if (shiftDown) {
                    if (selectTool.active()) {
                        selectTool.cancelTranslate(editor);
                        selectTool.cancelRotate(editor);
                    }
                    if (editor.redo(assets)) {
                        previewNeedsRebuild = true;
                    }
                } else if (selectTool.active()) {
                    selectTool.cancelTranslate(editor);
                    selectTool.cancelRotate(editor);
                    previewNeedsRebuild = true;
                } else if (editor.undo(assets)) {
                    previewNeedsRebuild = true;
                }
            }
            if (ctrlDown && IsKeyPressed(KEY_Y)) {
                if (selectTool.active()) {
                    selectTool.cancelTranslate(editor);
                    selectTool.cancelRotate(editor);
                }
                if (editor.redo(assets)) {
                    previewNeedsRebuild = true;
                }
            }
            if (ctrlDown && IsKeyPressed(KEY_S)) {
                if (editor.save(assets) && editor.scene == slopmap::EditorScene::Prefab) {
                    prefabBrowser.rescan(assets);
                }
            }
            if (ctrlDown && IsKeyPressed(KEY_N)) {
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
            if (ctrlDown && IsKeyPressed(KEY_O)) {
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
            if (shiftDown &&
                IsKeyPressed(KEY_X) && !selectTool.active() &&
                editor.doc().selectionMode == slopmap::SelectionMode::Brush &&
                !editor.doc().selectedBrushes.empty()) {
                createTool.reset();
                punchTool.reset();
                clipTool.beginFromSelection(editor);
            }
            if (IsKeyPressed(KEY_TAB)) {
                editor.toggleOrthoTop();
            }
            if (IsKeyPressed(KEY_Q) &&
                ctrlDown &&
                (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT))) {
                editor.toggleViewportLayout();
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
            if (IsKeyPressed(KEY_Z) && !ctrlDown && !selectTool.active()) {
                if (shiftDown) {
                    switch (editor.wireframe) {
                    case slopmap::WireframeOverlay::Off:
                        editor.wireframe = slopmap::WireframeOverlay::Visible;
                        editor.statusMessage = "Wires: Vis";
                        break;
                    case slopmap::WireframeOverlay::Visible:
                        editor.wireframe = slopmap::WireframeOverlay::All;
                        editor.statusMessage = "Wires: All";
                        break;
                    case slopmap::WireframeOverlay::All:
                        editor.wireframe = slopmap::WireframeOverlay::Off;
                        editor.statusMessage = "Wires: Off";
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

        panes = slopmap::splitContentViewports(
            viewport, editor.viewportLayout, editor.activeViewport);
        for (int i = 0; i < slopmap::kViewportCount; ++i) {
            const Rectangle& paneRect = panes.rects[static_cast<std::size_t>(i)];
            if (paneRect.width >= 1.0f && paneRect.height >= 1.0f) {
                slopmap::ensureContentTarget(contentTargets[i], paneRect);
            }
        }
        editor.syncActiveCameraFromBank();
        {
            const Rectangle& activeRect =
                panes.rects[static_cast<std::size_t>(editor.activeViewport)];
            editor.contentViewport =
                (activeRect.width >= 1.0f && activeRect.height >= 1.0f) ? activeRect : viewport;
        }
        const Camera3D camera = editor.camera.toRaylib();

        const bool wasSelectTransform = selectTool.active();
        const bool createWasActive = createTool.active();
        const bool punchWasActive = punchTool.active();
        const bool clipWasActive = clipTool.active();
        const std::size_t brushCountBefore = editor.doc().brushes.size();
        const std::size_t instanceCountBefore = editor.doc().instances.size();
        const bool dirtyBefore = editor.doc().dirty;

        const bool toolActive = selectTool.active() || createTool.active() ||
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
            editor.doc().dirty != dirtyBefore || selectTool.active() || wasSelectTransform ||
            createTool.active() != createWasActive || punchTool.active() != punchWasActive ||
            clipTool.active() != clipWasActive) {
            previewNeedsRebuild = true;
        }
        if (previewNeedsRebuild &&
            ((!createTool.active() && !punchTool.active() && !clipTool.active()) ||
                selectTool.active())) {
            editor.rebuildPreview(assets);
            previewNeedsRebuild = selectTool.active();
        }

        BeginDrawing();
        ClearBackground(Color{28, 30, 34, 255});

        particlePreview.enabled = editor.particlePreviewEnabled;
        const bool restartParticles = editor.particlePreviewRestartRequest;
        editor.particlePreviewRestartRequest = false;
        slopmap::syncParticlePreview(
            particlePreview, assets, editor.doc().things, restartParticles);
        slopmap::tickParticlePreview(
            particlePreview, assets, editor.doc().things, GetFrameTime());

        {
            const slopmap::ViewPlane drawPlane = editor.viewPlane;
            const slopmap::FlyCamera drawCamera = editor.camera;
            const Rectangle drawViewport = editor.contentViewport;
            for (int i = 0; i < slopmap::kViewportCount; ++i) {
                const Rectangle& paneRect = panes.rects[static_cast<std::size_t>(i)];
                if (paneRect.width < 1.0f || paneRect.height < 1.0f ||
                    contentTargets[i].id == 0) {
                    continue;
                }
                editor.viewPlane = editor.viewports[static_cast<std::size_t>(i)].plane;
                editor.camera = editor.viewports[static_cast<std::size_t>(i)].camera;
                editor.contentViewport = paneRect;
                const Camera3D paneCamera = editor.camera.toRaylib();
                BeginTextureMode(contentTargets[i]);
                drawScene(
                    editor,
                    assets,
                    paneCamera,
                    createTool,
                    punchTool,
                    clipTool,
                    infiniteGrid,
                    particlePreview);
                EndTextureMode();
                slopmap::drawContentTarget(contentTargets[i], paneRect);
            }
            editor.viewPlane = drawPlane;
            editor.camera = drawCamera;
            editor.contentViewport = drawViewport;
            slopmap::drawViewportChrome(panes, editor.activeViewport);
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
                    selectTool.cancelTranslate(editor);
                    selectTool.cancelRotate(editor);
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
                    selectTool.cancelTranslate(editor);
                    selectTool.cancelRotate(editor);
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
                    selectTool.cancelTranslate(editor);
                    selectTool.cancelRotate(editor);
                    createTool.reset();
                    punchTool.reset();
                    clipTool.reset();
                    editor.mode = slopmap::EditorMode::Place;
                }

                toolSep();
                {
                    const char* fillIcon = "picture";
                    const char* fillLabel = "Textures";
                    switch (editor.fill) {
                    case slopmap::PreviewFill::Wireframe:
                        fillIcon = "shape_square";
                        fillLabel = "Wire";
                        break;
                    case slopmap::PreviewFill::Solid:
                        fillIcon = "box";
                        fillLabel = "Solid";
                        break;
                    case slopmap::PreviewFill::Textures:
                        fillIcon = "picture";
                        fillLabel = "Textures";
                        break;
                    case slopmap::PreviewFill::Unlit:
                        fillIcon = "world";
                        fillLabel = "Unlit";
                        break;
                    case slopmap::PreviewFill::Lit:
                        fillIcon = "lightbulb";
                        fillLabel = "Lit";
                        break;
                    case slopmap::PreviewFill::SolidLit:
                        fillIcon = "contrast";
                        fillLabel = "Solid Lit";
                        break;
                    }
                    char fillBtnLabel[64];
                    std::snprintf(fillBtnLabel, sizeof(fillBtnLabel), "Render: %s", fillLabel);
                    if (toolBtn("fill-mode", fillIcon, fillBtnLabel, true)) {
                        ImGui::OpenPopup("##fillModePopup");
                    }
                    if (ImGui::BeginPopup("##fillModePopup")) {
                        ImGui::TextDisabled("CSG");
                        if (menuItemWithIcon(
                                assets,
                                kToolbarIcons,
                                "shape_square",
                                "Wire",
                                "Z",
                                editor.fill == slopmap::PreviewFill::Wireframe)) {
                            editor.fill = slopmap::PreviewFill::Wireframe;
                        }
                        if (menuItemWithIcon(
                                assets,
                                kToolbarIcons,
                                "box",
                                "Solid",
                                nullptr,
                                editor.fill == slopmap::PreviewFill::Solid)) {
                            editor.fill = slopmap::PreviewFill::Solid;
                        }
                        if (menuItemWithIcon(
                                assets,
                                kToolbarIcons,
                                "picture",
                                "Textures",
                                nullptr,
                                editor.fill == slopmap::PreviewFill::Textures)) {
                            editor.fill = slopmap::PreviewFill::Textures;
                        }
                        ImGui::Separator();
                        ImGui::TextDisabled("VIS / RAD");
                        if (menuItemWithIcon(
                                assets,
                                kToolbarIcons,
                                "world",
                                "Unlit",
                                nullptr,
                                editor.fill == slopmap::PreviewFill::Unlit)) {
                            if (editor.preview.visValid || editor.reloadVisPreview(assets)) {
                                editor.fill = slopmap::PreviewFill::Unlit;
                            } else {
                                editor.statusMessage =
                                    "No VIS; run VIS (falling back to Textures)";
                                editor.fill = slopmap::PreviewFill::Textures;
                            }
                        }
                        if (menuItemWithIcon(
                                assets,
                                kToolbarIcons,
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
                                kToolbarIcons,
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
                        ImGui::EndPopup();
                    }
                }
                ImGui::SameLine();
                {
                    const char* wiresLabel = "Off";
                    if (editor.wireframe == slopmap::WireframeOverlay::Visible) {
                        wiresLabel = "Vis";
                    } else if (editor.wireframe == slopmap::WireframeOverlay::All) {
                        wiresLabel = "All";
                    }
                    char wiresBtnLabel[48];
                    std::snprintf(wiresBtnLabel, sizeof(wiresBtnLabel), "Wires: %s", wiresLabel);
                    if (toolBtn(
                            "wires-overlay",
                            "eye",
                            wiresBtnLabel,
                            editor.wireframe != slopmap::WireframeOverlay::Off)) {
                        ImGui::OpenPopup("##wiresModePopup");
                    }
                    if (ImGui::BeginPopup("##wiresModePopup")) {
                        if (menuItemWithIcon(
                                assets,
                                kToolbarIcons,
                                "delete",
                                "Off",
                                "Shift+Z",
                                editor.wireframe == slopmap::WireframeOverlay::Off)) {
                            editor.wireframe = slopmap::WireframeOverlay::Off;
                        }
                        if (menuItemWithIcon(
                                assets,
                                kToolbarIcons,
                                "shape_square",
                                "Vis",
                                nullptr,
                                editor.wireframe == slopmap::WireframeOverlay::Visible)) {
                            editor.wireframe = slopmap::WireframeOverlay::Visible;
                        }
                        if (menuItemWithIcon(
                                assets,
                                kToolbarIcons,
                                "world",
                                "All",
                                nullptr,
                                editor.wireframe == slopmap::WireframeOverlay::All)) {
                            editor.wireframe = slopmap::WireframeOverlay::All;
                        }
                        ImGui::EndPopup();
                    }
                }
                ImGui::SameLine();
                if (toolBtn(
                        "ignore-backfaces",
                        "shape_flip_vertical",
                        "Backfaces",
                        editor.ignoreBackfaces)) {
                    editor.ignoreBackfaces = !editor.ignoreBackfaces;
                    editor.statusMessage = editor.ignoreBackfaces
                        ? "Ignore backfaces: On"
                        : "Ignore backfaces: Off";
                }
                ImGui::SameLine();
                if (toolBtn(
                        "show-gizmos",
                        "shape_handles",
                        "Gizmos",
                        editor.showGizmos)) {
                    editor.showGizmos = !editor.showGizmos;
                    editor.statusMessage = editor.showGizmos ? "Gizmos: on" : "Gizmos: off";
                }
                ImGui::SameLine();
                if (toolBtn(
                        "preview-particles",
                        "control_play",
                        "Particles",
                        editor.particlePreviewEnabled)) {
                    editor.particlePreviewEnabled = !editor.particlePreviewEnabled;
                    editor.statusMessage = editor.particlePreviewEnabled
                        ? "Particle preview: on"
                        : "Particle preview: off";
                }
                ImGui::SameLine();
                {
                    const bool global =
                        editor.transformSpace == slopmap::TransformSpace::Global;
                    if (toolBtn(
                            "transform-space",
                            global ? "world" : "vector",
                            global ? "Global" : "Relative",
                            true)) {
                        editor.transformSpace = global
                            ? slopmap::TransformSpace::Relative
                            : slopmap::TransformSpace::Global;
                        editor.statusMessage = editor.transformSpace ==
                                slopmap::TransformSpace::Global
                            ? "Transform space: Global"
                            : "Transform space: Relative";
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                        ImGui::SetTooltip(
                            "Transform space. Relative: face move along normal. "
                            "Global: world axes.");
                    }
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
                        "%s%s  ·  Grid %s %s%s",
                        viewLabel,
                        editor.viewportLayout == slopmap::ViewportLayout::Quad ? "×4" : "",
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
                            "sel-edge",
                            "connect",
                            "Edge",
                            editor.doc().selectionMode == slopmap::SelectionMode::Edge)) {
                        editor.setSelectionMode(slopmap::SelectionMode::Edge);
                    }
                    ImGui::SameLine();
                    if (toolBtn(
                            "sel-vert",
                            "vector",
                            "Vert",
                            editor.doc().selectionMode == slopmap::SelectionMode::Vert)) {
                        editor.setSelectionMode(slopmap::SelectionMode::Vert);
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
                        toolSep();
                        if (toolBtn("op-hollow", "box", "Hollow", false)) {
                            editor.hollowThickness = editor.gridSize;
                            editor.showHollowModal = true;
                        }
                        ImGui::SameLine();
                        if (toolBtn("op-punch", "cut", "Punch-out", punchTool.active())) {
                            selectTool.cancelTranslate(editor);
                            selectTool.cancelRotate(editor);
                            createTool.reset();
                            clipTool.reset();
                            punchTool.beginFromSelection(editor);
                        }
                        ImGui::SameLine();
                        if (toolBtn("op-clip", "arrow_divide", "Clip", clipTool.active())) {
                            selectTool.cancelTranslate(editor);
                            selectTool.cancelRotate(editor);
                            createTool.reset();
                            punchTool.reset();
                            clipTool.beginFromSelection(editor);
                        }
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

                const ImGuiStyle& style = ImGui::GetStyle();
                const float avail = ImGui::GetContentRegionAvail().y;
                const float frameH = ImGui::GetFrameHeight();
                const float spacing = style.ItemSpacing.y;
                const float listH = std::max(
                    frameH * 4.0f,
                    (avail - frameH * 2.0f - spacing * 3.0f) * 0.55f);

                if (ImGui::BeginTabBar("##sceneTabs", ImGuiTabBarFlags_None)) {
                    if (ImGui::BeginTabItem("Brushes")) {
                        if (ImGui::BeginChild(
                                "##brushes", ImVec2(0.0f, listH), ImGuiChildFlags_Borders)) {
                            if (d.brushes.empty()) {
                                ImGui::TextDisabled("No brushes");
                            }
                            for (std::size_t i = 0; i < d.brushes.size(); ++i) {
                                ImGui::PushID(static_cast<int>(i));
                                const bool selected = d.isBrushSelected(static_cast<int>(i));
                                if (selectableWithIcon(
                                        assets,
                                        kIconSet,
                                        brushRoleToolbarIcon(d.brushes[i].role),
                                        d.brushes[i].id.c_str(),
                                        selected)) {
                                    editor.selectBrush(static_cast<int>(i), ImGui::GetIO().KeyShift);
                                }
                                ImGui::PopID();
                            }
                        }
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Prefabs")) {
                        if (ImGui::BeginChild(
                                "##instances", ImVec2(0.0f, listH), ImGuiChildFlags_Borders)) {
                            if (d.instances.empty()) {
                                ImGui::TextDisabled("No prefab instances");
                            }
                            for (std::size_t i = 0; i < d.instances.size(); ++i) {
                                ImGui::PushID(static_cast<int>(i) + 100000);
                                const bool selected = d.isEntitySelected(
                                    {slopmap::EntityRef::Kind::Instance, static_cast<int>(i)});
                                const std::string label =
                                    d.instances[i].id + " (" + d.instances[i].path + ")";
                                if (selectableWithIcon(
                                        assets, kIconSet, "package", label.c_str(), selected)) {
                                    editor.selectEntity(
                                        {slopmap::EntityRef::Kind::Instance, static_cast<int>(i)},
                                        ImGui::GetIO().KeyShift);
                                }
                                ImGui::PopID();
                            }
                        }
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Things")) {
                        if (ImGui::BeginChild(
                                "##things", ImVec2(0.0f, listH), ImGuiChildFlags_Borders)) {
                            if (d.things.empty()) {
                                ImGui::TextDisabled("No things");
                            }
                            for (std::size_t i = 0; i < d.things.size(); ++i) {
                                ImGui::PushID(static_cast<int>(i) + 200000);
                                const bool selected = d.isEntitySelected(
                                    {slopmap::EntityRef::Kind::Thing, static_cast<int>(i)});
                                const std::string label = d.things[i].id + " (" +
                                    slopengine::thingKindName(d.things[i].kind) + ")";
                                const char* icon = slopengine::thingKindIsLight(d.things[i].kind)
                                    ? (d.things[i].kind == slopengine::ThingKind::AmbientLight
                                           ? "weather_sun"
                                           : "lightbulb")
                                    : (d.things[i].kind == slopengine::ThingKind::SoundSource
                                           ? "sound"
                                           : (d.things[i].kind == slopengine::ThingKind::PlayerStart
                                                  ? "user"
                                                  : (d.things[i].kind ==
                                                             slopengine::ThingKind::Marker
                                                         ? "cross"
                                                         : (d.things[i].kind ==
                                                                    slopengine::ThingKind::Particle
                                                                ? "weather_clouds"
                                                                : (d.things[i].kind ==
                                                                           slopengine::ThingKind::
                                                                               Pickup
                                                                       ? "basket"
                                                                       : "transmit")))));
                                if (selectableWithIcon(
                                        assets, kIconSet, icon, label.c_str(), selected)) {
                                    editor.selectEntity(
                                        {slopmap::EntityRef::Kind::Thing, static_cast<int>(i)},
                                        ImGui::GetIO().KeyShift);
                                }
                                ImGui::PopID();
                            }
                        }
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }

                ImGui::Separator();
                ImGui::TextUnformatted("Properties");
                const float propsH = std::max(0.0f, ImGui::GetContentRegionAvail().y);
                if (d.selectionMode == slopmap::SelectionMode::Entity) {
                    thingPanel.drawSection(editor, assets, propsH);
                } else {
                    brushPanel.drawSection(editor, propsH);
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
                const float bodyH = std::max(0.0f, ImGui::GetContentRegionAvail().y -
                    ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.y);

                if (ImGui::BeginTabBar("##libraryTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
                    if (ImGui::BeginTabItem("Materials")) {
                        const slopmap::MaterialBrowserResult matResult =
                            materialBrowser.drawSection(editor, assets, editorSettings, bodyH);
                        if (matResult.requestRescan) {
                            materialBrowser.rescan(assets);
                        }
                        if (matResult.applied) {
                            editor.rebuildPreview(assets);
                        }
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("UV")) {
                        const slopmap::TexturePanelResult texResult =
                            texturePanel.drawSection(editor, assets, bodyH);
                        if (texResult.changed) {
                            editor.rebuildPreview(assets);
                        }
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Things")) {
                        if (ImGui::BeginChild(
                                "##placekinds", ImVec2(0.0f, bodyH), ImGuiChildFlags_Borders)) {
                            const float btnW = ImGui::GetContentRegionAvail().x;
                            const float iconSz = 16.0f;
                            const float rowH = ImGui::GetFrameHeight();
                            auto placeKindButton =
                                [&](const char* icon,
                                    const char* label,
                                    bool active,
                                    const std::function<void()>& onClick) {
                                    ImGui::PushID(label);
                                    if (active) {
                                        ImGui::PushStyleColor(
                                            ImGuiCol_Button,
                                            ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                                    }
                                    const bool pressed = ImGui::Button("##", ImVec2(btnW, rowH));
                                    if (active) {
                                        ImGui::PopStyleColor();
                                    }
                                    const ImVec2 min = ImGui::GetItemRectMin();
                                    const ImVec2 max = ImGui::GetItemRectMax();
                                    const float pad = ImGui::GetStyle().FramePadding.x;
                                    const float iconY = min.y + (max.y - min.y - iconSz) * 0.5f;
                                    const IconAtlas* atlas = assets.getIconAtlas(kIconSet);
                                    float textX = min.x + pad;
                                    if (atlas != nullptr && atlas->texture.id != 0) {
                                        if (const auto rect = findIconRect(*atlas, icon)) {
                                            const float tw =
                                                static_cast<float>(atlas->texture.width);
                                            const float th =
                                                static_cast<float>(atlas->texture.height);
                                            ImGui::GetWindowDrawList()->AddImage(
                                                (ImTextureID)(intptr_t)atlas->texture.id,
                                                ImVec2(min.x + pad, iconY),
                                                ImVec2(min.x + pad + iconSz, iconY + iconSz),
                                                ImVec2(rect->x / tw, rect->y / th),
                                                ImVec2(
                                                    (rect->x + rect->width) / tw,
                                                    (rect->y + rect->height) / th),
                                                ImGui::GetColorU32(ImGuiCol_Text));
                                            textX = min.x + pad + iconSz +
                                                ImGui::GetStyle().ItemInnerSpacing.x;
                                        }
                                    }
                                    const float textY = min.y +
                                        (max.y - min.y - ImGui::GetTextLineHeight()) * 0.5f;
                                    ImGui::GetWindowDrawList()->AddText(
                                        ImVec2(textX, textY),
                                        ImGui::GetColorU32(ImGuiCol_Text),
                                        label);
                                    ImGui::PopID();
                                    if (pressed) {
                                        onClick();
                                    }
                                };

                            const bool placingThing = editor.mode == slopmap::EditorMode::Place &&
                                editor.placeTarget == slopmap::PlaceTarget::Thing &&
                                editor.placeThingKind.has_value();
                            const auto isKind = [&](slopengine::ThingKind kind,
                                                   slopmap::PlacePresentation presentation =
                                                       slopmap::PlacePresentation::None) {
                                if (!placingThing || !editor.placeThingType.empty() ||
                                    *editor.placeThingKind != kind) {
                                    return false;
                                }
                                if (kind == slopengine::ThingKind::Prop) {
                                    return editor.placePresentation == presentation;
                                }
                                return true;
                            };
                            const auto isDef = [&](std::string_view typeId) {
                                return placingThing && editor.placeThingType == typeId;
                            };
                            const auto drawCatalogFolder =
                                [&](auto&& self,
                                    const ThingCatalogFolder& folder,
                                    const char* fallbackIcon,
                                    slopengine::PackageRole role,
                                    std::string_view packageId,
                                    const std::string& idPrefix,
                                    const std::string& catalogPath) -> void {
                                for (const auto& [name, child] : folder.folders) {
                                    const std::string childPath =
                                        catalogPath.empty() ? name : catalogPath + "/" + name;
                                    const std::string folderId = idPrefix + "/" + name;
                                    const slopengine::ThingFolderDef* folderDef =
                                        slopengine::thingDefRegistry().findFolder(
                                            childPath, role, packageId);
                                    const char* folderIcon = "folder";
                                    const char* folderLabel = name.c_str();
                                    if (folderDef != nullptr) {
                                        if (!folderDef->icon.empty()) {
                                            folderIcon = folderDef->icon.c_str();
                                        }
                                        if (!folderDef->label.empty()) {
                                            folderLabel = folderDef->label.c_str();
                                        }
                                    }
                                    ImGui::PushID(folderId.c_str());
                                    const bool open = ImGui::TreeNodeEx(
                                        "##folder",
                                        ImGuiTreeNodeFlags_SpanAvailWidth |
                                            ImGuiTreeNodeFlags_DefaultOpen |
                                            ImGuiTreeNodeFlags_FramePadding |
                                            ImGuiTreeNodeFlags_AllowOverlap);
                                    {
                                        const ImVec2 min = ImGui::GetItemRectMin();
                                        const ImVec2 max = ImGui::GetItemRectMax();
                                        const float pad = ImGui::GetStyle().FramePadding.x;
                                        float textX = min.x + ImGui::GetTreeNodeToLabelSpacing();
                                        const float iconY =
                                            min.y + (max.y - min.y - iconSz) * 0.5f;
                                        const IconAtlas* atlas = assets.getIconAtlas(kIconSet);
                                        if (atlas != nullptr && atlas->texture.id != 0) {
                                            if (const auto rect =
                                                    findIconRect(*atlas, folderIcon)) {
                                                const float tw =
                                                    static_cast<float>(atlas->texture.width);
                                                const float th =
                                                    static_cast<float>(atlas->texture.height);
                                                ImGui::GetWindowDrawList()->AddImage(
                                                    (ImTextureID)(intptr_t)atlas->texture.id,
                                                    ImVec2(textX, iconY),
                                                    ImVec2(textX + iconSz, iconY + iconSz),
                                                    ImVec2(rect->x / tw, rect->y / th),
                                                    ImVec2(
                                                        (rect->x + rect->width) / tw,
                                                        (rect->y + rect->height) / th),
                                                    ImGui::GetColorU32(ImGuiCol_Text));
                                                textX += iconSz +
                                                    ImGui::GetStyle().ItemInnerSpacing.x;
                                            }
                                        }
                                        (void)pad;
                                        const float textY = min.y +
                                            (max.y - min.y - ImGui::GetTextLineHeight()) * 0.5f;
                                        ImGui::GetWindowDrawList()->AddText(
                                            ImVec2(textX, textY),
                                            ImGui::GetColorU32(ImGuiCol_Text),
                                            folderLabel);
                                    }
                                    if (open) {
                                        self(
                                            self,
                                            child,
                                            fallbackIcon,
                                            role,
                                            packageId,
                                            folderId,
                                            childPath);
                                        ImGui::TreePop();
                                    }
                                    ImGui::PopID();
                                }
                                for (const slopengine::ThingDef* def : folder.leaves) {
                                    if (def == nullptr) {
                                        continue;
                                    }
                                    const char* icon = def->icon.empty() ? fallbackIcon
                                                                         : def->icon.c_str();
                                    placeKindButton(
                                        icon,
                                        def->label.c_str(),
                                        isDef(def->id),
                                        [&, def] { beginThingDef(editor, *def, createTool); });
                                }
                            };
                            const auto drawCatalogDefs =
                                [&](const std::vector<const slopengine::ThingDef*>& defs,
                                    const char* fallbackIcon,
                                    slopengine::PackageRole role,
                                    std::string_view packageId,
                                    const char* idPrefix) {
                                    const ThingCatalogFolder tree = buildThingCatalogFolder(defs);
                                    drawCatalogFolder(
                                        drawCatalogFolder,
                                        tree,
                                        fallbackIcon,
                                        role,
                                        packageId,
                                        idPrefix,
                                        "");
                                };

                            if (ImGui::TreeNodeEx(
                                    "Engine",
                                    ImGuiTreeNodeFlags_SpanAvailWidth |
                                        ImGuiTreeNodeFlags_DefaultOpen)) {
                                placeKindButton(
                                    "user",
                                    "player-start",
                                    isKind(slopengine::ThingKind::PlayerStart),
                                    [&] {
                                        beginThingKind(
                                            editor,
                                            slopengine::ThingKind::PlayerStart,
                                            createTool);
                                    });
                                placeKindButton(
                                    "picture",
                                    "sprite",
                                    isKind(
                                        slopengine::ThingKind::Prop,
                                        slopmap::PlacePresentation::Sprite),
                                    [&] {
                                        beginThingKind(
                                            editor,
                                            slopengine::ThingKind::Prop,
                                            createTool,
                                            slopmap::PlacePresentation::Sprite);
                                    });
                                placeKindButton(
                                    "shape_square",
                                    "geo",
                                    isKind(
                                        slopengine::ThingKind::Prop,
                                        slopmap::PlacePresentation::Geo),
                                    [&] {
                                        beginThingKind(
                                            editor,
                                            slopengine::ThingKind::Prop,
                                            createTool,
                                            slopmap::PlacePresentation::Geo);
                                    });
                                placeKindButton(
                                    "cursor",
                                    "usable",
                                    isKind(slopengine::ThingKind::Usable),
                                    [&] {
                                        beginThingKind(
                                            editor, slopengine::ThingKind::Usable, createTool);
                                    });
                                placeKindButton(
                                    "basket",
                                    "pickup",
                                    isKind(slopengine::ThingKind::Pickup),
                                    [&] {
                                        beginThingKind(
                                            editor, slopengine::ThingKind::Pickup, createTool);
                                    });
                                placeKindButton(
                                    "group",
                                    "actor",
                                    isKind(slopengine::ThingKind::Actor),
                                    [&] {
                                        beginThingKind(
                                            editor, slopengine::ThingKind::Actor, createTool);
                                    });
                                placeKindButton(
                                    "arrow_switch",
                                    "mover",
                                    isKind(slopengine::ThingKind::Mover),
                                    [&] {
                                        beginThingKind(
                                            editor, slopengine::ThingKind::Mover, createTool);
                                    });
                                placeKindButton(
                                    "lightning",
                                    "trigger",
                                    isKind(slopengine::ThingKind::Trigger),
                                    [&] {
                                        beginThingKind(
                                            editor, slopengine::ThingKind::Trigger, createTool);
                                    });
                                placeKindButton(
                                    "flag_blue",
                                    "marker",
                                    isKind(slopengine::ThingKind::Marker),
                                    [&] {
                                        beginThingKind(
                                            editor, slopengine::ThingKind::Marker, createTool);
                                    });
                                placeKindButton(
                                    "weather_clouds",
                                    "particle",
                                    isKind(slopengine::ThingKind::Particle),
                                    [&] {
                                        beginThingKind(
                                            editor, slopengine::ThingKind::Particle, createTool);
                                    });
                                placeKindButton(
                                    "lightbulb",
                                    "point-light",
                                    isKind(slopengine::ThingKind::PointLight),
                                    [&] {
                                        beginThingKind(
                                            editor,
                                            slopengine::ThingKind::PointLight,
                                            createTool);
                                    });
                                placeKindButton(
                                    "lightbulb_off",
                                    "spot-light",
                                    isKind(slopengine::ThingKind::SpotLight),
                                    [&] {
                                        beginThingKind(
                                            editor,
                                            slopengine::ThingKind::SpotLight,
                                            createTool);
                                    });
                                placeKindButton(
                                    "lightbulb_add",
                                    "area-light",
                                    isKind(slopengine::ThingKind::AreaLight),
                                    [&] {
                                        beginThingKind(
                                            editor,
                                            slopengine::ThingKind::AreaLight,
                                            createTool);
                                    });
                                placeKindButton(
                                    "weather_sun",
                                    "sun",
                                    isKind(slopengine::ThingKind::Sun),
                                    [&] {
                                        beginThingKind(
                                            editor, slopengine::ThingKind::Sun, createTool);
                                    });
                                placeKindButton(
                                    "weather_sun",
                                    "ambient-light",
                                    isKind(slopengine::ThingKind::AmbientLight),
                                    [&] {
                                        beginThingKind(
                                            editor,
                                            slopengine::ThingKind::AmbientLight,
                                            createTool);
                                    });
                                placeKindButton(
                                    "image",
                                    "skybox",
                                    isKind(slopengine::ThingKind::Skybox),
                                    [&] {
                                        beginThingKind(
                                            editor, slopengine::ThingKind::Skybox, createTool);
                                    });
                                drawCatalogDefs(
                                    slopengine::thingDefRegistry().defsForRole(
                                        slopengine::PackageRole::Engine),
                                    "brick",
                                    slopengine::PackageRole::Engine,
                                    "",
                                    "engine");
                                ImGui::TreePop();
                            }

                            if (ImGui::TreeNodeEx(
                                    "Base Game",
                                    ImGuiTreeNodeFlags_SpanAvailWidth |
                                        ImGuiTreeNodeFlags_DefaultOpen)) {
                                drawCatalogDefs(
                                    slopengine::thingDefRegistry().defsForRole(
                                        slopengine::PackageRole::Base),
                                    "package",
                                    slopengine::PackageRole::Base,
                                    assets.basePackageId(),
                                    "base");
                                ImGui::TreePop();
                            }

                            std::map<std::string, std::vector<const slopengine::ThingDef*>>
                                modDefsByPackage;
                            for (const slopengine::ThingDef* def :
                                 slopengine::thingDefRegistry().defsForRole(
                                     slopengine::PackageRole::Mod)) {
                                if (def == nullptr) {
                                    continue;
                                }
                                const std::string& packageId =
                                    def->packageId.empty() ? std::string("mod") : def->packageId;
                                modDefsByPackage[packageId].push_back(def);
                            }
                            for (const auto& [packageId, defs] : modDefsByPackage) {
                                ImGui::PushID(packageId.c_str());
                                if (ImGui::TreeNodeEx(
                                        packageId.c_str(),
                                        ImGuiTreeNodeFlags_SpanAvailWidth |
                                            ImGuiTreeNodeFlags_DefaultOpen)) {
                                    drawCatalogDefs(
                                        defs,
                                        "plugin",
                                        slopengine::PackageRole::Mod,
                                        packageId,
                                        packageId.c_str());
                                    ImGui::TreePop();
                                }
                                ImGui::PopID();
                            }

                            if (placingThing) {
                                ImGui::Separator();
                                std::string placing;
                                if (!editor.placeThingType.empty()) {
                                    if (const slopengine::ThingDef* def =
                                            slopengine::thingDefRegistry().find(
                                                editor.placeThingType)) {
                                        placing = def->label + " (" + def->id + ")";
                                    } else {
                                        placing = editor.placeThingType;
                                    }
                                } else {
                                    placing = slopengine::thingKindName(*editor.placeThingKind);
                                    if (*editor.placeThingKind == slopengine::ThingKind::Prop) {
                                        if (editor.placePresentation ==
                                            slopmap::PlacePresentation::Sprite) {
                                            placing = "sprite";
                                        } else if (
                                            editor.placePresentation ==
                                            slopmap::PlacePresentation::Geo) {
                                            placing = "geo";
                                        }
                                    }
                                }
                                ImGui::Text("Placing: %s", placing.c_str());
                                ImGui::TextColored(
                                    ImVec4(0.4f, 0.9f, 0.45f, 1.0f),
                                    "Click the viewport to place");
                            }
                        }
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Prefabs")) {
                        const slopmap::PrefabBrowserResult prefabResult =
                            prefabBrowser.drawSection(editor, assets, bodyH);
                        if (prefabResult.requestRescan) {
                            prefabBrowser.rescan(assets);
                        }
                        if (prefabResult.selected && editor.scene == slopmap::EditorScene::Level) {
                            editor.placeTarget = slopmap::PlaceTarget::PrefabInstance;
                            editor.placeThingKind.reset();
                            editor.placeThingType.clear();
                            editor.placePresentation = slopmap::PlacePresentation::None;
                            editor.mode = slopmap::EditorMode::Place;
                            createTool.reset();
                        }
                        if (prefabResult.openRequested) {
                            if (editor.scene == slopmap::EditorScene::Prefab &&
                                editor.prefabDoc.dirty) {
                                editor.showOpenPrefabModal = true;
                                std::snprintf(
                                    prefabPathBuf,
                                    sizeof(prefabPathBuf),
                                    "%s",
                                    editor.placePrefabPath.c_str());
                            } else if (
                                editor.scene == slopmap::EditorScene::Level &&
                                editor.levelDoc.dirty) {
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
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
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

                const float stageChipW = 180.0f;
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
                        "BSP%s FAC%s VIS%s RAD%s",
                        editor.compileDirty.bsp ? "*" : "",
                        editor.compileDirty.fac ? "*" : "",
                        editor.compileDirty.vis ? "*" : "",
                        editor.compileDirty.rad ? "*" : "");
                    const bool anyDirty = editor.compileDirty.bsp || editor.compileDirty.fac ||
                        editor.compileDirty.vis || editor.compileDirty.rad;
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
            ImGui::TextUnformatted("Walls");
            if (ImGui::RadioButton("Inside", !editor.hollowOutward)) {
                editor.hollowOutward = false;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Outside", editor.hollowOutward)) {
                editor.hollowOutward = true;
            }
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

        if (editor.showValidateBrushesWindow) {
            ImGui::SetNextWindowSize(ImVec2(520, 320), ImGuiCond_FirstUseEver);
            if (ImGui::Begin(
                    "Validate Brushes",
                    &editor.showValidateBrushesWindow,
                    ImGuiWindowFlags_None)) {
                constexpr const char* kIcons = kDefaultIconSet;
                const slopmap::EditorDocument& d = editor.doc();
                int invalidCount = 0;
                for (std::size_t i = 0; i < d.brushes.size(); ++i) {
                    const slopengine::Brush& brush = d.brushes[i];
                    const auto error = slopengine::validateBrushConvex(brush);
                    if (!error) {
                        continue;
                    }
                    ++invalidCount;
                    ImGui::PushID(static_cast<int>(i));
                    const std::string label =
                        brush.id.empty() ? ("brush#" + std::to_string(i)) : brush.id;
                    ImGui::TextUnformatted((label + " — " + error->message).c_str());
                    ImGui::SameLine();
                    if (buttonWithIcon(assets, kIcons, "cursor", "Select", ImVec2(80, 0))) {
                        editor.mode = slopmap::EditorMode::Select;
                        editor.setSelectionMode(slopmap::SelectionMode::Brush);
                        editor.selectBrushes({static_cast<int>(i)}, static_cast<int>(i));
                        editor.statusMessage = label + ": " + error->message;
                        editor.frameSelection();
                    }
                    ImGui::PopID();
                }
                if (invalidCount == 0) {
                    ImGui::TextUnformatted("All brushes valid");
                } else {
                    ImGui::Separator();
                    ImGui::Text("%d invalid brush(es)", invalidCount);
                }
            }
            ImGui::End();
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

        if (showPreferencesModal) {
            ImGui::OpenPopup("Preferences");
            showPreferencesModal = false;
        }
        if (ImGui::BeginPopupModal("Preferences", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            constexpr const char* kIcons = kDefaultIconSet;
            ImGui::TextUnformatted("Material thumbnail cache");
            ImGui::SetNextItemWidth(420.0f);
            ImGui::InputText("##thumbcache", thumbCachePathBuf, sizeof(thumbCachePathBuf));
            ImGui::TextDisabled(
                "Default: %s",
                slopengine::defaultSlopmapThumbnailCacheDirectory().string().c_str());
            if (buttonWithIcon(assets, kIcons, "arrow_undo", "Reset default")) {
                const std::string path =
                    slopengine::defaultSlopmapThumbnailCacheDirectory().string();
                std::snprintf(thumbCachePathBuf, sizeof(thumbCachePathBuf), "%s", path.c_str());
            }
            ImGui::Separator();
            if (buttonWithIcon(assets, kIcons, "accept", "Save", ImVec2(120, 0))) {
                const std::string defaultPath =
                    slopengine::defaultSlopmapThumbnailCacheDirectory().string();
                if (std::strcmp(thumbCachePathBuf, defaultPath.c_str()) == 0) {
                    editorSettings.thumbnailCachePath.clear();
                } else {
                    editorSettings.thumbnailCachePath = thumbCachePathBuf;
                }
                editorSettings.save();
                materialBrowser.thumbs.clear();
                materialBrowser.thumbsDirty = true;
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

        if (!selectTool.active() && !ImGui::IsAnyItemActive()) {
            editor.endEdit();
        }

        rlImGuiEnd();
        EndDrawing();
    }

    compile.shutdown();
    if (IsCursorHidden()) {
        EnableCursor();
    }
    for (int i = 0; i < slopmap::kViewportCount; ++i) {
        if (contentTargets[i].id != 0) {
            UnloadRenderTexture(contentTargets[i]);
        }
    }
    editor.preview.clear();
    infiniteGrid.unload();
    s7_quit(scheme);
    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
