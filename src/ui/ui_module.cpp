#include "ui/ui_module.hpp"

#include "assets/asset_services.hpp"
#include "camera/components.hpp"
#include "core/frame_perf.hpp"
#include "game/game_state.hpp"
#include "game/user_settings.hpp"
#include "input/action_registry.hpp"
#include "input/actions.hpp"
#include "input/bind_code.hpp"
#include "input/input_context.hpp"
#include "input/input_module.hpp"
#include "input/input_state.hpp"
#include "interact/components.hpp"
#include "map/bsp.hpp"
#include "map/fac.hpp"
#include "map/graph.hpp"
#include "map/light_components.hpp"
#include "map/light_sample.hpp"
#include "map/pvs.hpp"
#include "physics/components.hpp"
#include "physics/physics_module.hpp"
#include "physics/rigid_mover.hpp"
#include "physics/trigger_components.hpp"
#include "render/animation_player.hpp"
#include "render/components.hpp"
#include "render/dynamic_light.hpp"
#include "render/dynamic_light_shadows.hpp"
#include "render/render_context.hpp"
#include "render/sprite_animator.hpp"
#include "script/scheme_harden.hpp"
#include "script/script_context.hpp"
#include "script/ui_script.hpp"
#include "ui/icon_ui.hpp"
#include "ui/ui_state.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <raylib.h>
#include <string>
#include <vector>

namespace slopengine {

namespace {

struct ResolutionOption {
    int width = 0;
    int height = 0;
};

constexpr ResolutionOption kResolutionPresets[] = {
    {1280, 720},
    {1366, 768},
    {1600, 900},
    {1920, 1080},
    {2560, 1440},
    {3840, 2160},
};

void logConsoleMessage(ConsoleState& console, const std::string& message) {
    console.log.push_back(message);
    if (console.log.size() > 200) {
        console.log.erase(console.log.begin());
    }
}

std::vector<ResolutionOption> buildResolutionOptions(const GraphicsSettings& draft) {
    std::vector<ResolutionOption> options;
    options.reserve(16);

    const auto addUnique = [&options](int width, int height) {
        if (width <= 0 || height <= 0) {
            return;
        }
        for (const ResolutionOption& option : options) {
            if (option.width == width && option.height == height) {
                return;
            }
        }
        options.push_back({width, height});
    };

    addUnique(draft.width, draft.height);
    for (const ResolutionOption& preset : kResolutionPresets) {
        addUnique(preset.width, preset.height);
    }

    const int monitor = GetCurrentMonitor();
    addUnique(GetMonitorWidth(monitor), GetMonitorHeight(monitor));
    return options;
}

constexpr int kShadowMapResolutionPresets[] = {128, 256, 512, 1024, 2048, 4096, 8192};

std::vector<int> buildShadowMapResolutionOptions(int current) {
    std::vector<int> options;
    options.reserve(8);

    const auto addUnique = [&options](int value) {
        if (value <= 0) {
            return;
        }
        for (const int existing : options) {
            if (existing == value) {
                return;
            }
        }
        options.push_back(value);
    };

    addUnique(current);
    for (const int preset : kShadowMapResolutionPresets) {
        addUnique(preset);
    }
    return options;
}

void openGraphicsSettings(SettingsUiState& settingsUi, const UserSettings& settings) {
    settingsUi.graphicsOpen = true;
    settingsUi.graphicsDraft = settings.graphics;
}

void openControlsSettings(SettingsUiState& settingsUi, const UserSettings& settings) {
    settingsUi.controlsOpen = true;
    settingsUi.controlsDraft = settings.controls;
    settingsUi.rebindingAction = -1;
    settingsUi.rebindingWaitMouseRelease = false;
}

void applyGraphicsDraft(flecs::world world, UserSettings& settings, const GraphicsSettings& draft) {
    const int previousShadowMapResolution = settings.graphics.shadowMapResolution;
    settings.graphics = draft;
    applyGraphicsSettings(settings.graphics);
    settings.save();

    if (draft.shadowMapResolution != previousShadowMapResolution && world.has<DynamicLightShadowState>() &&
        world.has<AssetServices>() && world.get<AssetServices>().store != nullptr) {
        world.set<DynamicLightShadowState>(createDynamicLightShadowState(
            *world.get<AssetServices>().store,
            draft.shadowMapResolution));
    }
}

void applyControlsDraft(UserSettings& settings, const ControlsSettings& draft) {
    settings.controls = draft;
    settings.save();
}

void drawPauseMenu(flecs::world world, AssetStore& assets, InputContextStack& contexts) {
    ImGui::SetNextWindowSize({320.0f, 220.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
        {ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f},
        ImGuiCond_Always,
        {0.5f, 0.5f});

    if (ImGui::Begin("Paused", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {
        ImGui::TextUnformatted("Simulation is paused.");
        if (buttonWithIcon(assets, kDefaultIconSet, "control_play", "Resume")) {
            contexts.pop(InputContext::PauseMenu);
        }
        callDrawPauseMenu(world);
    }
    ImGui::End();
}

void drawGraphicsSettings(
    flecs::world world,
    AssetStore& assets,
    SettingsUiState& settingsUi,
    UserSettings& settings) {
    if (!settingsUi.graphicsOpen) {
        return;
    }

    ImGui::SetNextWindowSize({420.0f, 320.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Graphics", &settingsUi.graphicsOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    GraphicsSettings& draft = settingsUi.graphicsDraft;

    const char* modeLabels[] = {
        windowModeLabel(WindowMode::Windowed),
        windowModeLabel(WindowMode::Fullscreen),
        windowModeLabel(WindowMode::Borderless),
    };
    int modeIndex = static_cast<int>(draft.mode);
    if (ImGui::Combo("Window Mode", &modeIndex, modeLabels, IM_ARRAYSIZE(modeLabels))) {
        draft.mode = static_cast<WindowMode>(modeIndex);
    }

    const std::vector<ResolutionOption> resolutions = buildResolutionOptions(draft);
    int resolutionIndex = 0;
    for (int i = 0; i < static_cast<int>(resolutions.size()); ++i) {
        if (resolutions[static_cast<std::size_t>(i)].width == draft.width
            && resolutions[static_cast<std::size_t>(i)].height == draft.height) {
            resolutionIndex = i;
            break;
        }
    }

    std::vector<std::string> resolutionLabels;
    resolutionLabels.reserve(resolutions.size());
    for (const ResolutionOption& option : resolutions) {
        char label[64];
        std::snprintf(label, sizeof(label), "%dx%d", option.width, option.height);
        resolutionLabels.emplace_back(label);
    }

    std::vector<const char*> resolutionItems;
    resolutionItems.reserve(resolutionLabels.size());
    for (const std::string& label : resolutionLabels) {
        resolutionItems.push_back(label.c_str());
    }

    if (ImGui::Combo(
            "Resolution",
            &resolutionIndex,
            resolutionItems.data(),
            static_cast<int>(resolutionItems.size()))) {
        const ResolutionOption& selected = resolutions[static_cast<std::size_t>(resolutionIndex)];
        draft.width = selected.width;
        draft.height = selected.height;
    }

    ImGui::Checkbox("VSync", &draft.vsync);
    ImGui::Checkbox("Dynamic Lights", &draft.dynamicLights);
    if (!draft.dynamicLights) {
        ImGui::BeginDisabled();
    }
    if (ImGui::SliderInt("Max Dynamic Lights", &draft.maxDynamicLights, 0, kMaxDynamicLights)) {
        draft.maxShadowedDynamicLights =
            std::min(draft.maxShadowedDynamicLights, draft.maxDynamicLights);
    }
    ImGui::Checkbox("Dynamic Light Shadows", &draft.dynamicLightShadows);
    const bool shadowsDisabled = !draft.dynamicLights || !draft.dynamicLightShadows;
    if (shadowsDisabled) {
        ImGui::BeginDisabled();
    }
    ImGui::SliderInt(
        "Max Shadowed Lights",
        &draft.maxShadowedDynamicLights,
        0,
        std::min(draft.maxDynamicLights, kMaxShadowedDynamicLights));
    const std::vector<int> shadowResolutions = buildShadowMapResolutionOptions(draft.shadowMapResolution);
    int shadowResolutionIndex = 0;
    for (int i = 0; i < static_cast<int>(shadowResolutions.size()); ++i) {
        if (shadowResolutions[static_cast<std::size_t>(i)] == draft.shadowMapResolution) {
            shadowResolutionIndex = i;
            break;
        }
    }

    std::vector<std::string> shadowResolutionLabels;
    shadowResolutionLabels.reserve(shadowResolutions.size());
    for (const int value : shadowResolutions) {
        char label[32];
        std::snprintf(label, sizeof(label), "%dx%d", value, value);
        shadowResolutionLabels.emplace_back(label);
    }

    std::vector<const char*> shadowResolutionItems;
    shadowResolutionItems.reserve(shadowResolutionLabels.size());
    for (const std::string& label : shadowResolutionLabels) {
        shadowResolutionItems.push_back(label.c_str());
    }

    if (ImGui::Combo(
            "Shadow Map Resolution",
            &shadowResolutionIndex,
            shadowResolutionItems.data(),
            static_cast<int>(shadowResolutionItems.size()))) {
        draft.shadowMapResolution = shadowResolutions[static_cast<std::size_t>(shadowResolutionIndex)];
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Higher values use much more VRAM and take effect after Apply.\n"
            "Applies to every shadow-casting light slot, so cost scales fast.");
    }
    if (shadowsDisabled) {
        ImGui::EndDisabled();
    }
    if (!draft.dynamicLights) {
        ImGui::EndDisabled();
    }

    ImGui::Separator();
    if (buttonWithIcon(assets, kDefaultIconSet, "accept", "Apply")) {
        applyGraphicsDraft(world, settings, draft);
    }
    ImGui::SameLine();
    if (buttonWithIcon(assets, kDefaultIconSet, "arrow_refresh", "Reset")) {
        draft = UserSettings::defaults().graphics;
    }

    ImGui::End();
}

void drawControlsSettings(AssetStore& assets, SettingsUiState& settingsUi, UserSettings& settings) {
    if (!settingsUi.controlsOpen) {
        return;
    }

    ImGui::SetNextWindowSize({420.0f, 420.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Controls", &settingsUi.controlsOpen, ImGuiWindowFlags_NoCollapse)) {
        settingsUi.rebindingAction = -1;
        settingsUi.rebindingWaitMouseRelease = false;
        ImGui::End();
        return;
    }

    if (settingsUi.rebindingAction >= 0) {
        ImGui::Text("Press a key or mouse button for %s (Esc to cancel)...",
            actionLabelAt(settingsUi.rebindingAction));
    } else {
        ImGui::TextUnformatted("Click a binding to reassign it.");
    }

    ImGui::Separator();
    if (ImGui::BeginTable("ControlsTable", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Action");
        ImGui::TableSetupColumn("Key");
        ImGui::TableHeadersRow();

        char keyBuffer[64];
        const int actionCount = actionRegistry().size();
        for (int i = 0; i < actionCount; ++i) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(actionLabelAt(i));
            ImGui::TableSetColumnIndex(1);

            const int bind = settingsUi.controlsDraft.binds[static_cast<std::size_t>(i)];
            const char* label = bindDisplayName(bind, keyBuffer, sizeof(keyBuffer));
            ImGui::PushID(i);
            const bool listening = settingsUi.rebindingAction == i;
            if (ImGui::Button(listening ? "..." : label, {140.0f, 0.0f})) {
                settingsUi.rebindingAction = i;
                settingsUi.rebindingWaitMouseRelease = true;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    if (buttonWithIcon(assets, kDefaultIconSet, "accept", "Apply")) {
        applyControlsDraft(settings, settingsUi.controlsDraft);
        settingsUi.rebindingAction = -1;
        settingsUi.rebindingWaitMouseRelease = false;
    }
    ImGui::SameLine();
    if (buttonWithIcon(assets, kDefaultIconSet, "arrow_refresh", "Reset")) {
        settingsUi.controlsDraft = ControlsSettings::defaults();
        settingsUi.rebindingAction = -1;
        settingsUi.rebindingWaitMouseRelease = false;
    }

    ImGui::End();

    if (!settingsUi.controlsOpen) {
        settingsUi.rebindingAction = -1;
        settingsUi.rebindingWaitMouseRelease = false;
    }
}

void drawMainMenuBar(
    flecs::world world,
    AssetStore& assets,
    QuitRequest& quit,
    SettingsUiState& settingsUi,
    DebugUiState& debugUi,
    const UserSettings& settings,
    const EngineSessionInfo& sessionInfo) {
    if (!beginMainMenuBar()) {
        return;
    }

    constexpr const char* kIcons = kDefaultIconSet;

    if (beginMenuWithIcon(assets, kIcons, "folder", "File", true)) {
        callDrawFileMenu(world);
        if (menuItemWithIcon(assets, kIcons, "door", "Quit")) {
            quit.requested = true;
        }
        ImGui::EndMenu();
    }

    if (beginMenuWithIcon(assets, kIcons, "cog", "Config", true)) {
        if (menuItemWithIcon(assets, kIcons, "monitor", "Graphics")) {
            openGraphicsSettings(settingsUi, settings);
        }
        if (menuItemWithIcon(assets, kIcons, "keyboard", "Controls")) {
            openControlsSettings(settingsUi, settings);
        }
        ImGui::EndMenu();
    }

    if (debugUi.menuAvailable && beginMenuWithIcon(assets, kIcons, "bug", "Debug", true)) {
        if (beginMenuWithIcon(assets, kIcons, "map", "Map")) {
            const std::string currentId =
                world.has<CurrentMap>() ? world.get<CurrentMap>().id : std::string{};
            const auto maps = assets.listMaps();
            if (maps.empty()) {
                ImGui::MenuItem("(no maps)", nullptr, false, false);
            } else {
                for (const AssetStore::MapListEntry& entry : maps) {
                    const bool selected = entry.id == currentId;
                    if (menuItemWithIcon(
                            assets, kIcons, "world", entry.name.c_str(), nullptr, selected)) {
                        requestMapLoad(entry.id);
                    }
                }
            }
            ImGui::EndMenu();
        }
        if (beginMenuWithIcon(assets, kIcons, "chart_organisation", "BSP")) {
            ImGui::MenuItem("Outlines", nullptr, &debugUi.showBspOutlines);
            ImGui::MenuItem("Leaf Faces", nullptr, &debugUi.showBspLeafFaces);
            ImGui::MenuItem("Portals", nullptr, &debugUi.showBspPortals);
            ImGui::MenuItem("Surface Faces", nullptr, &debugUi.showBspSurfaceFaces);
            ImGui::MenuItem("Current Leaf Only", nullptr, &debugUi.showBspCurrentLeafOnly);
            ImGui::EndMenu();
        }
        if (beginMenuWithIcon(assets, kIcons, "shape_ungroup", "VIS")) {
            ImGui::MenuItem("Faces", nullptr, &debugUi.showVisFaces);
            ImGui::MenuItem("Current Leaf Only", nullptr, &debugUi.showVisCurrentLeafOnly);
            ImGui::EndMenu();
        }
        if (beginMenuWithIcon(assets, kIcons, "film", "Sprites")) {
            ImGui::MenuItem("Masks", nullptr, &debugUi.showSpriteMasks);
            ImGui::MenuItem("Aim", nullptr, &debugUi.showSpriteAim);
            ImGui::EndMenu();
        }
        menuItemWithIcon(assets, kIcons, "chart_line", "Graphs", nullptr, &debugUi.showGraphs);
        menuItemWithIcon(assets, kIcons, "arrow_branch", "Nav Paths", nullptr, &debugUi.showNavPaths);
        menuItemWithIcon(
            assets, kIcons, "chart_curve", "Performance", nullptr, &debugUi.showPerformance);
        menuItemWithIcon(
            assets, kIcons, "lightbulb", "Unlit (disable lightmaps)", nullptr, &debugUi.unlit);
        menuItemWithIcon(assets, kIcons, "user_go", "Noclip", nullptr, &debugUi.noclip);
        menuItemWithIcon(assets, kIcons, "picture_empty", "Hide HUD", nullptr, &debugUi.hideHud);
        menuItemWithIcon(
            assets, kIcons, "user", "Hide FP View", nullptr, &debugUi.hideFpScene);
        menuItemWithIcon(
            assets, kIcons, "application_view_list", "Entities", nullptr, &debugUi.entityListOpen);
        callDrawDebugMenu(world);
        ImGui::EndMenu();
    }

    if (beginMenuWithIcon(assets, kIcons, "information", "Help", true)) {
        menuItemWithIcon(assets, kIcons, "information", "About", nullptr, false, false);
        ImGui::EndMenu();
    }

    const std::vector<Package>& packages = assets.packages();
    std::string versionLabel;
    if (packages.size() >= 2) {
        versionLabel = "engine " + packages[0].meta().version + " - " + packages[1].meta().id
            + " " + packages[1].meta().version + " - " + sessionInfo.profile;
    }
    const float versionWidth = ImGui::CalcTextSize(versionLabel.c_str()).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - versionWidth - ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::TextUnformatted(versionLabel.c_str());

    endMainMenuBar();
}

const char* entityKindLabel(flecs::entity entity) {
    if (entity.has<PlayerCamera>()) {
        return "player";
    }
    const char* name = entity.name();
    if (name != nullptr && std::strcmp(name, "MapStatic") == 0) {
        return "map";
    }
    if (entity.has<DynamicLight>()) {
        return "dynamic-light";
    }
    if (entity.has<PointLight>()) {
        return "point-light";
    }
    if (entity.has<SpotLight>()) {
        return "spot-light";
    }
    if (entity.has<AreaLight>()) {
        return "area-light";
    }
    if (entity.has<SunLight>()) {
        return "sun";
    }
    if (entity.has<AmbientLight>()) {
        return "ambient-light";
    }
    if (entity.has<RigidMover>()) {
        return "mover";
    }
    if (entity.has<Interactable>()) {
        return "usable";
    }
    if (entity.has<Actor>()) {
        return "actor";
    }
    if (entity.has<TriggerVolume>() && !entity.has<SpriteInstance>() && !entity.has<Model3D>()) {
        return "trigger";
    }
    if (entity.has<SpriteInstance>() || entity.has<Model3D>()) {
        return "prop";
    }
    return "entity";
}

Vector3 entityListPosition(flecs::entity entity) {
    if (entity.has<LocalTransformation>()) {
        return entity.get<LocalTransformation>().position;
    }
    if (entity.has<Lens>()) {
        return entity.get<Lens>().camera.position;
    }
    return {0.0f, 0.0f, 0.0f};
}

void drawEntityComponentDetails(flecs::entity entity) {
    if (entity.has<LocalTransformation>()) {
        const LocalTransformation& local = entity.get<LocalTransformation>();
        if (ImGui::TreeNode("LocalTransformation")) {
            ImGui::Text(
                "Position: %.3f, %.3f, %.3f",
                static_cast<double>(local.position.x),
                static_cast<double>(local.position.y),
                static_cast<double>(local.position.z));
            ImGui::Text(
                "Scale: %.3f, %.3f, %.3f",
                static_cast<double>(local.scale.x),
                static_cast<double>(local.scale.y),
                static_cast<double>(local.scale.z));
            ImGui::Text(
                "Rotation: %.3f, %.3f, %.3f, %.3f",
                static_cast<double>(local.rotation.x),
                static_cast<double>(local.rotation.y),
                static_cast<double>(local.rotation.z),
                static_cast<double>(local.rotation.w));
            ImGui::TreePop();
        }
    }
    if (entity.has<Lens>()) {
        const Lens& lens = entity.get<Lens>();
        if (ImGui::TreeNode("Lens")) {
            ImGui::Text(
                "Position: %.3f, %.3f, %.3f",
                static_cast<double>(lens.camera.position.x),
                static_cast<double>(lens.camera.position.y),
                static_cast<double>(lens.camera.position.z));
            ImGui::Text(
                "Target: %.3f, %.3f, %.3f",
                static_cast<double>(lens.camera.target.x),
                static_cast<double>(lens.camera.target.y),
                static_cast<double>(lens.camera.target.z));
            ImGui::Text("Fovy: %.2f", static_cast<double>(lens.camera.fovy));
            ImGui::TreePop();
        }
    }
    if (entity.has<FirstPersonScene>()) {
        const FirstPersonScene& scene = entity.get<FirstPersonScene>();
        if (ImGui::TreeNodeEx("FirstPersonScene", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Root: %llu", static_cast<unsigned long long>(scene.root));
            ImGui::Text("Weapon socket: %llu", static_cast<unsigned long long>(scene.weaponSocket));
            ImGui::Text("Emission socket: %llu", static_cast<unsigned long long>(scene.emissionSocket));
            ImGui::Text("Rad tint: %s", scene.useRadTint ? "true" : "false");
            ImGui::Text("Shading: %s", scene.useShading ? "true" : "false");
            flecs::entity emission{};
            if (scene.emissionSocket != 0) {
                emission = entity.world().entity(scene.emissionSocket);
            }
            flecs::entity light{};
            if (emission.is_valid()) {
                emission.children([&](flecs::entity child) {
                    if (!light.is_valid() && child.has<DynamicLight>()) {
                        light = child;
                    }
                });
            }
            if (light.is_valid() && light.has<DynamicLight>()) {
                const DynamicLight& dyn = light.get<DynamicLight>();
                const Vector3 rgb = dynamicLightLinearRgb(dyn);
                ImGui::Text("Emission intensity: %.3f", static_cast<double>(dyn.intensity));
                ImGui::Text("Emission range: %.3f", static_cast<double>(dyn.range));
                ImGui::Text("Emission cone: %.3f", static_cast<double>(dyn.coneAngle));
                ImGui::Text(
                    "Emission RGB: %.3f, %.3f, %.3f",
                    static_cast<double>(rgb.x),
                    static_cast<double>(rgb.y),
                    static_cast<double>(rgb.z));
                if (light.has<FpLightControl>()) {
                    const FpLightControl& control = light.get<FpLightControl>();
                    ImGui::Text("Enabled: %s", control.enabled ? "true" : "false");
                    ImGui::Text("On intensity: %.3f", static_cast<double>(control.onIntensity));
                }
            } else {
                ImGui::TextUnformatted("Emission light: none");
            }
            ImGui::TreePop();
        }
    }
    if (entity.has<FirstPersonController>()) {
        const FirstPersonController& controller = entity.get<FirstPersonController>();
        if (ImGui::TreeNode("FirstPersonController")) {
            ImGui::Text("Yaw: %.3f", static_cast<double>(controller.yaw));
            ImGui::Text("Pitch: %.3f", static_cast<double>(controller.pitch));
            ImGui::Text("Move Speed: %.2f", static_cast<double>(controller.moveSpeed));
            ImGui::Text("Eye Height: %.2f", static_cast<double>(controller.eyeHeight));
            ImGui::TreePop();
        }
    }
    if (entity.has<CharacterMotor>()) {
        const CharacterMotor& motor = entity.get<CharacterMotor>();
        if (ImGui::TreeNode("CharacterMotor")) {
            ImGui::Text("Radius: %.2f", static_cast<double>(motor.radius));
            ImGui::Text("Height: %.2f", static_cast<double>(motor.height));
            ImGui::Text("Move Speed: %.2f", static_cast<double>(motor.moveSpeed));
            ImGui::Text("Eye Height: %.2f", static_cast<double>(motor.eyeHeight));
            ImGui::TreePop();
        }
    }
    if (entity.has<Interactable>()) {
        const Interactable& interactable = entity.get<Interactable>();
        if (ImGui::TreeNode("Interactable")) {
            ImGui::Text("Prompt: %s", interactable.prompt.c_str());
            ImGui::Text("Event: %s", interactable.onUse.id.c_str());
            ImGui::Text("Max Distance: %.2f", static_cast<double>(interactable.maxDistance));
            ImGui::TreePop();
        }
    }
    if (entity.has<SpriteInstance>()) {
        const SpriteInstance& sprite = entity.get<SpriteInstance>();
        if (ImGui::TreeNode("SpriteInstance")) {
            ImGui::Text("Sprite: %s", sprite.sprite.c_str());
            ImGui::Text("Frame: %s", sprite.frame.c_str());
            ImGui::Text("Facing Yaw: %.3f", static_cast<double>(sprite.facingYaw));
            ImGui::TreePop();
        }
    }
    if (entity.has<SpriteAnimator>()) {
        const SpriteAnimator& animator = entity.get<SpriteAnimator>();
        if (ImGui::TreeNode("SpriteAnimator")) {
            ImGui::Text("Anim: %s", animator.animPath.c_str());
            ImGui::Text("Clip: %s", animator.clipName.c_str());
            ImGui::Text("Time: %.3f", static_cast<double>(animator.time));
            ImGui::Text("Playing: %s", animator.playing ? "true" : "false");
            ImGui::TreePop();
        }
    }
    if (entity.has<Model3D>()) {
        if (ImGui::TreeNode("Model3D")) {
            ImGui::TextUnformatted("Model present");
            ImGui::TreePop();
        }
    }
    if (entity.has<DynamicLight>()) {
        const DynamicLight& light = entity.get<DynamicLight>();
        if (ImGui::TreeNodeEx("DynamicLight", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text(
                "Kind: %s",
                light.kind == DynamicLightKind::Spot ? "spot" : "point");
            const char* space = "rgb";
            if (light.color.space == DynamicLightColorSpace::Hsv) {
                space = "hsv";
            } else if (light.color.space == DynamicLightColorSpace::Hsl) {
                space = "hsl";
            }
            ImGui::Text("Color Space: %s", space);
            ImGui::Text(
                "Color Value: %.3f, %.3f, %.3f",
                static_cast<double>(light.color.value.x),
                static_cast<double>(light.color.value.y),
                static_cast<double>(light.color.value.z));
            const Vector3 rgb = dynamicLightLinearRgb(light);
            ImGui::Text(
                "Resolved RGB: %.3f, %.3f, %.3f",
                static_cast<double>(rgb.x),
                static_cast<double>(rgb.y),
                static_cast<double>(rgb.z));
            ImGui::Text("Intensity: %.3f", static_cast<double>(light.intensity));
            ImGui::Text("Range: %.3f", static_cast<double>(light.range));
            ImGui::Text("Cone: %.3f", static_cast<double>(light.coneAngle));
            ImGui::Text("Cast Shadows: %s", light.castShadows ? "true" : "false");
            ImGui::TreePop();
        }
    }
    if (entity.has<PointLight>()) {
        const PointLight& light = entity.get<PointLight>();
        if (ImGui::TreeNode("PointLight")) {
            ImGui::Text(
                "Color: %.3f, %.3f, %.3f",
                static_cast<double>(light.color.x),
                static_cast<double>(light.color.y),
                static_cast<double>(light.color.z));
            ImGui::Text("Intensity: %.3f", static_cast<double>(light.intensity));
            ImGui::Text("Range: %.3f", static_cast<double>(light.range));
            ImGui::TreePop();
        }
    }
    if (entity.has<SpotLight>()) {
        const SpotLight& light = entity.get<SpotLight>();
        if (ImGui::TreeNode("SpotLight")) {
            ImGui::Text(
                "Color: %.3f, %.3f, %.3f",
                static_cast<double>(light.color.x),
                static_cast<double>(light.color.y),
                static_cast<double>(light.color.z));
            ImGui::Text("Intensity: %.3f", static_cast<double>(light.intensity));
            ImGui::Text("Range: %.3f", static_cast<double>(light.range));
            ImGui::Text("Cone: %.3f", static_cast<double>(light.coneAngle));
            ImGui::TreePop();
        }
    }
    if (entity.has<AreaLight>()) {
        const AreaLight& light = entity.get<AreaLight>();
        if (ImGui::TreeNode("AreaLight")) {
            ImGui::Text(
                "Color: %.3f, %.3f, %.3f",
                static_cast<double>(light.color.x),
                static_cast<double>(light.color.y),
                static_cast<double>(light.color.z));
            ImGui::Text("Intensity: %.3f", static_cast<double>(light.intensity));
            ImGui::Text(
                "Size: %.3f x %.3f",
                static_cast<double>(light.size.x),
                static_cast<double>(light.size.y));
            ImGui::TreePop();
        }
    }
    if (entity.has<SunLight>()) {
        const SunLight& light = entity.get<SunLight>();
        if (ImGui::TreeNode("SunLight")) {
            ImGui::Text(
                "Color: %.3f, %.3f, %.3f",
                static_cast<double>(light.color.x),
                static_cast<double>(light.color.y),
                static_cast<double>(light.color.z));
            ImGui::Text("Intensity: %.3f", static_cast<double>(light.intensity));
            ImGui::TreePop();
        }
    }
    if (entity.has<AmbientLight>()) {
        const AmbientLight& light = entity.get<AmbientLight>();
        if (ImGui::TreeNode("AmbientLight")) {
            ImGui::Text(
                "Color: %.3f, %.3f, %.3f",
                static_cast<double>(light.color.x),
                static_cast<double>(light.color.y),
                static_cast<double>(light.color.z));
            ImGui::Text("Intensity: %.3f", static_cast<double>(light.intensity));
            ImGui::TreePop();
        }
    }
    if (entity.has<AnimationPlayer>()) {
        const AnimationPlayer& player = entity.get<AnimationPlayer>();
        if (ImGui::TreeNode("AnimationPlayer")) {
            ImGui::Text("Bank: %s", player.animBankPath.c_str());
            ImGui::Text("Clip: %s", player.clipName.c_str());
            ImGui::Text("Time: %.3f", static_cast<double>(player.time));
            ImGui::Text("Playing: %s", player.playing ? "true" : "false");
            ImGui::TreePop();
        }
    }
}

void drawEntityDetail(DebugUiState& debugUi) {
    if (!debugUi.entityDetailOpen) {
        return;
    }

    if (!debugUi.inspectedEntity.is_valid()) {
        debugUi.entityDetailOpen = false;
        debugUi.inspectedEntity = {};
        return;
    }

    flecs::entity entity = debugUi.inspectedEntity;
    const char* name = entity.name();
    if (name == nullptr) {
        name = "(anon)";
    }

    char title[256];
    std::snprintf(
        title,
        sizeof(title),
        "Entity: %s###EntityDetail",
        name);

    ImGui::SetNextWindowSize({420.0f, 480.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title, &debugUi.entityDetailOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::Text("Name: %s", name);
    ImGui::Text("Id: %llu", static_cast<unsigned long long>(entity.id()));
    ImGui::Text("Kind: %s", entityKindLabel(entity));

    const Vector3 position = entityListPosition(entity);
    ImGui::Text(
        "Position: %.3f, %.3f, %.3f",
        static_cast<double>(position.x),
        static_cast<double>(position.y),
        static_cast<double>(position.z));

    ImGui::Separator();
    ImGui::TextUnformatted("Components");
    if (ImGui::BeginChild("EntityComponents", {0.0f, 0.0f}, true)) {
        const flecs::entity_t identifierId = entity.world().id<flecs::Identifier>();
        entity.each([&](flecs::id id) {
            if (id.is_pair() && id.first() == identifierId) {
                return;
            }
            flecs::string idStr = id.str();
            ImGui::BulletText("%s", idStr.c_str());
        });

        ImGui::Separator();
        ImGui::TextUnformatted("Details");
        drawEntityComponentDetails(entity);
    }
    ImGui::EndChild();

    ImGui::End();

    if (!debugUi.entityDetailOpen) {
        debugUi.inspectedEntity = {};
    }
}

float historyScaleMax(const float* history, int historyCount, float floorMs) {
    float scaleMax = floorMs;
    for (int i = 0; i < historyCount; ++i) {
        if (history[i] > scaleMax) {
            scaleMax = history[i];
        }
    }
    return scaleMax;
}

void plotMsHistory(
    const char* id,
    const FramePerfStats& perf,
    const float* history,
    float floorMs,
    ImVec4 color) {
    const int valuesOffset = perf.historyCount < kFramePerfHistorySize ? 0 : perf.historyOffset;
    ImGui::PushStyleColor(ImGuiCol_PlotLines, color);
    ImGui::PlotLines(
        id,
        history,
        perf.historyCount,
        valuesOffset,
        nullptr,
        0.0f,
        historyScaleMax(history, perf.historyCount, floorMs),
        ImVec2(-1.0f, 56.0f));
    ImGui::PopStyleColor();
}

void drawPerformanceWindow(flecs::world world, DebugUiState& debugUi) {
    if (!debugUi.showPerformance) {
        return;
    }

    ImGui::SetNextWindowSize({420.0f, 480.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Performance", &debugUi.showPerformance, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    static const FramePerfStats kEmptyPerf{};
    const FramePerfStats& perf =
        world.has<FramePerfStats>() ? world.get<FramePerfStats>() : kEmptyPerf;

    static double displayLastUpdate = 0.0;
    static float displayFps = 0.0f;
    static float displayFrameMs = 0.0f;
    static float displayPhysicsMs = 0.0f;
    static float displayRenderMs = 0.0f;
    static float displayUiMs = 0.0f;
    static float displayRssMb = 0.0f;
    const double now = GetTime();
    if (displayLastUpdate == 0.0 || (now - displayLastUpdate) >= 0.2) {
        displayLastUpdate = now;
        displayFps = ImGui::GetIO().Framerate;
        displayFrameMs = perf.frameMs;
        displayPhysicsMs = perf.physicsMs;
        displayRenderMs = perf.renderMs;
        displayUiMs = perf.uiMs;
        displayRssMb = processRssMb();
    }

    constexpr ImVec4 kFrameColor{0.45f, 0.85f, 1.0f, 1.0f};
    constexpr ImVec4 kPhysicsColor{1.0f, 0.70f, 0.30f, 1.0f};
    constexpr ImVec4 kRenderColor{0.45f, 0.90f, 0.50f, 1.0f};
    constexpr ImVec4 kUiColor{0.90f, 0.50f, 0.95f, 1.0f};

    if (ImGui::CollapsingHeader("Timings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextColored(
            kFrameColor,
            "Frame  %.2f ms  (%.0f FPS)",
            static_cast<double>(displayFrameMs),
            static_cast<double>(displayFps));
        plotMsHistory("##frameMs", perf, perf.frameHistory, 16.7f, kFrameColor);

        ImGui::TextColored(
            kPhysicsColor, "Physics  %.2f ms", static_cast<double>(displayPhysicsMs));
        plotMsHistory("##physicsMs", perf, perf.physicsHistory, 4.0f, kPhysicsColor);

        ImGui::TextColored(
            kRenderColor, "Render  %.2f ms", static_cast<double>(displayRenderMs));
        plotMsHistory("##renderMs", perf, perf.renderHistory, 4.0f, kRenderColor);

        ImGui::TextColored(kUiColor, "UI  %.2f ms", static_cast<double>(displayUiMs));
        plotMsHistory("##uiMs", perf, perf.uiHistory, 2.0f, kUiColor);
    }

    if (ImGui::CollapsingHeader("Memory", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("RSS %.1f MB", static_cast<double>(displayRssMb));
    }

    if (ImGui::CollapsingHeader("Entities", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("WorldSpace %d", world.count<WorldSpace>());
        ImGui::Text("MapOwned %d", world.count<MapOwned>());
        ImGui::Text("Actor %d", world.count<Actor>());
        ImGui::Text("CharacterMotor %d", world.count<CharacterMotor>());
        ImGui::Text("RigidMover %d", world.count<RigidMover>());
        ImGui::Text("SpriteInstance %d", world.count<SpriteInstance>());
        ImGui::Text("Model3D %d", world.count<Model3D>());
        ImGui::Text("DynamicLight %d", world.count<DynamicLight>());
        if (world.has<DynamicLightFrameState>()) {
            ImGui::Text(
                "Active lights %zu",
                world.get<DynamicLightFrameState>().lights.size());
        }
    }

    if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (world.has<PhysicsContext>() && world.get<PhysicsContext>().world != nullptr) {
            PhysicsWorld& physics = *world.get<PhysicsContext>().world;
            const auto stats = physics.system().GetBodyStats();
            ImGui::Text("Bodies %u / %u", stats.mNumBodies, stats.mMaxBodies);
            ImGui::Text(
                "Static %u  Dynamic %u  Kinematic %u",
                stats.mNumBodiesStatic,
                stats.mNumBodiesDynamic,
                stats.mNumBodiesKinematic);
            ImGui::Text(
                "Active dyn %u  Active kin %u",
                stats.mNumActiveBodiesDynamic,
                stats.mNumActiveBodiesKinematic);
            ImGui::Text("CharacterMotor %d", world.count<CharacterMotor>());
        } else {
            ImGui::TextUnformatted("(no physics world)");
        }
    }

    if (ImGui::CollapsingHeader("Map", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (world.has<CurrentMap>()) {
            ImGui::Text("Map %s", world.get<CurrentMap>().id.c_str());
        } else {
            ImGui::TextUnformatted("Map (none)");
        }

        if (world.has<MapBsp>()) {
            const BspTree& tree = world.get<MapBsp>().tree;
            ImGui::Text(
                "BSP nodes %zu  leaves %zu  portals %zu  surfaces %zu",
                tree.nodes.size(),
                tree.leaves.size(),
                tree.portals.size(),
                tree.surfaceFaces.size());

            std::int32_t currentLeaf = -1;
            if (world.has<PlayerEntity>()) {
                flecs::entity camera = world.get<PlayerEntity>().entity;
                if (camera.is_valid() && camera.has<Lens>()) {
                    currentLeaf = pointLeaf(tree, camera.get<Lens>().camera.position);
                }
            }
            ImGui::Text("Current leaf %d", static_cast<int>(currentLeaf));
        }

        if (world.has<MapFac>()) {
            ImGui::Text("FAC faces %zu", world.get<MapFac>().fac.faces.size());
        }
        if (world.has<MapPvs>()) {
            ImGui::Text("PVS leaves %d", world.get<MapPvs>().pvs.leafCount);
        }
        if (world.has<MapGraphs>()) {
            const GraphDocument& document = world.get<MapGraphs>().document;
            std::size_t nodes = 0;
            std::size_t edges = 0;
            for (const NamedGraph& graph : document.graphs) {
                nodes += graph.nodes.size();
                edges += graph.edges.size();
            }
            ImGui::Text(
                "Graphs %zu  nodes %zu  edges %zu",
                document.graphs.size(),
                nodes,
                edges);
        }
        if (world.has<MapLighting>()) {
            const MapLighting& lighting = world.get<MapLighting>();
            if (lighting.available) {
                ImGui::Text(
                    "Lighting charts %zu  atlases %zu",
                    lighting.rad.charts.size(),
                    lighting.rad.atlases.size());
            } else {
                ImGui::TextUnformatted("Lighting unavailable");
            }
        }
    }

    ImGui::End();
}

void drawEntityList(flecs::world world, DebugUiState& debugUi) {
    if (!debugUi.entityListOpen) {
        return;
    }

    ImGui::SetNextWindowSize({520.0f, 360.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Entities", &debugUi.entityListOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Double-click a row to inspect.");

    constexpr ImGuiTableFlags tableFlags =
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders
        | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("EntityListTable", 4, tableFlags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Id");
        ImGui::TableSetupColumn("Kind");
        ImGui::TableSetupColumn("Position");
        ImGui::TableHeadersRow();

        world.query<WorldSpace>().each([&debugUi](flecs::entity entity, WorldSpace) {
            const char* name = entity.name();
            if (name == nullptr) {
                name = "(anon)";
            }

            const Vector3 position = entityListPosition(entity);
            const bool selected =
                debugUi.entityDetailOpen && debugUi.inspectedEntity == entity;

            char label[256];
            std::snprintf(
                label,
                sizeof(label),
                "%s##ent%llu",
                name,
                static_cast<unsigned long long>(entity.id()));

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Selectable(
                label,
                selected,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                debugUi.inspectedEntity = entity;
                debugUi.entityDetailOpen = true;
            }

            ImGui::TableNextColumn();
            ImGui::Text("%llu", static_cast<unsigned long long>(entity.id()));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entityKindLabel(entity));
            ImGui::TableNextColumn();
            ImGui::Text(
                "%.2f, %.2f, %.2f",
                static_cast<double>(position.x),
                static_cast<double>(position.y),
                static_cast<double>(position.z));
        });

        ImGui::EndTable();
    }

    ImGui::End();
}

void drawInteractPanel(InputContextStack& contexts, InteractionTarget& target) {
    ImGui::SetNextWindowSize({360.0f, 160.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
        {ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f},
        ImGuiCond_Always,
        {0.5f, 0.5f});

    const char* title = target.prompt.empty() ? "Interact" : target.prompt.c_str();
    if (ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoCollapse)) {
        if (!target.onUse.empty()) {
            ImGui::Text("Event: %s", target.onUse.id.c_str());
        }
        if (target.entity.is_valid()) {
            ImGui::Text("Entity: %llu", static_cast<unsigned long long>(target.entity.id()));
        }
        if (ImGui::Button("Close")) {
            contexts.pop(InputContext::InteractUI);
        }
    }
    ImGui::End();
}

void drawConsole(ConsoleState& console, InputContextStack& contexts) {
    ImGui::SetNextWindowSize({640.0f, 360.0f}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Console", &console.open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::BeginChild("ConsoleLog", {0.0f, -ImGui::GetFrameHeightWithSpacing()}, false);
        for (const std::string& line : console.log) {
            ImGui::TextUnformatted(line.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::InputText(
                "##ConsoleInput",
                console.inputBuffer,
                sizeof(console.inputBuffer),
                ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (console.inputBuffer[0] != '\0') {
                logConsoleMessage(console, console.inputBuffer);
            }
            console.inputBuffer[0] = '\0';
        }
    } else {
        console.open = false;
    }
    ImGui::End();

    if (!console.open) {
        contexts.pop(InputContext::Console);
    }
}

void drawInteractionPrompt(const InteractionTarget& target, const InputContextStack& contexts) {
    if (!contexts.allowsGameplay() || !target.entity.is_valid()) {
        return;
    }

    const char* prompt = target.prompt.empty() ? "Interact" : target.prompt.c_str();
    ImGui::SetNextWindowBgAlpha(0.35f);
    ImGui::SetNextWindowPos(
        {ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.88f},
        ImGuiCond_Always,
        {0.5f, 0.5f});
    ImGui::Begin(
        "InteractionPrompt",
        nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
    ImGui::Text("[E] %s", prompt);
    ImGui::End();
}

void drawScriptingErrorBanner(AssetStore& assets) {
    if (!scriptingErrorsOccurred()) {
        return;
    }

    constexpr float kPad = 8.0f;
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::SetNextWindowPos(
        {ImGui::GetIO().DisplaySize.x - kPad, mainMenuBarHeight() + kPad},
        ImGuiCond_Always,
        {1.0f, 0.0f});
    ImGui::Begin(
        "ScriptingErrorBanner",
        nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoNav);
    drawIconImGui(assets, kDefaultIconSet, "error");
    ImGui::SameLine();
    ImGui::TextUnformatted("Scripting Errors! See program output.");
    ImGui::End();
}

void applyImGuiCursorPolicy(const InputContextStack& contexts) {
    ImGuiIO& io = ImGui::GetIO();
    if (contexts.blocksWorldInput()) {
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    } else {
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    }
}

void registerComponents(flecs::world& world) {
    world.component<ConsoleState>();
    world.component<QuitRequest>();
    world.component<ScreenshotRequest>();
    world.component<SettingsUiState>();
    world.component<DebugUiState>();
    world.component<FramePerfStats>();
}

void registerSystems(flecs::world& world) {
    world.system("HandleUiActions")
        .kind(flecs::PreUpdate)
        .run([](flecs::iter& it) {
            InputState& input = it.world().get_mut<InputState>();
            InputContextStack& contexts = it.world().get_mut<InputContextStack>();
            ConsoleState& console = it.world().get_mut<ConsoleState>();
            SettingsUiState& settingsUi = it.world().get_mut<SettingsUiState>();

            if (settingsUi.rebindingAction >= 0) {
                const int pressedKey = GetKeyPressed();
                if (pressedKey == KEY_ESCAPE) {
                    settingsUi.rebindingAction = -1;
                    settingsUi.rebindingWaitMouseRelease = false;
                    return;
                }

                if (settingsUi.rebindingWaitMouseRelease) {
                    const bool anyMouseDown =
                        IsMouseButtonDown(MOUSE_BUTTON_LEFT)
                        || IsMouseButtonDown(MOUSE_BUTTON_RIGHT)
                        || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)
                        || IsMouseButtonDown(MOUSE_BUTTON_SIDE)
                        || IsMouseButtonDown(MOUSE_BUTTON_EXTRA);
                    if (!anyMouseDown) {
                        settingsUi.rebindingWaitMouseRelease = false;
                    }
                }

                int assigned = KEY_NULL;
                if (pressedKey != 0) {
                    assigned = pressedKey;
                } else if (!settingsUi.rebindingWaitMouseRelease) {
                    constexpr int kMouseButtons[] = {
                        MOUSE_BUTTON_LEFT,
                        MOUSE_BUTTON_RIGHT,
                        MOUSE_BUTTON_MIDDLE,
                        MOUSE_BUTTON_SIDE,
                        MOUSE_BUTTON_EXTRA,
                    };
                    for (const int button : kMouseButtons) {
                        if (IsMouseButtonPressed(button)) {
                            assigned = bindFromMouseButton(button);
                            break;
                        }
                    }
                }

                if (assigned != KEY_NULL) {
                    const int actionCount = static_cast<int>(settingsUi.controlsDraft.binds.size());
                    for (int i = 0; i < actionCount; ++i) {
                        if (i != settingsUi.rebindingAction
                            && settingsUi.controlsDraft.binds[static_cast<std::size_t>(i)] == assigned) {
                            settingsUi.controlsDraft.binds[static_cast<std::size_t>(i)] = KEY_NULL;
                        }
                    }
                    settingsUi.controlsDraft.binds[static_cast<std::size_t>(settingsUi.rebindingAction)] =
                        assigned;
                    settingsUi.rebindingAction = -1;
                    settingsUi.rebindingWaitMouseRelease = false;
                }
                return;
            }

            if (input.pressed(Action::Console)) {
                if (contexts.contains(InputContext::Console)) {
                    contexts.pop(InputContext::Console);
                    console.open = false;
                } else {
                    contexts.push(InputContext::Console);
                    console.open = true;
                }
            }

            if (input.pressed(Action::MainMenu) && !isMenu(it.world())) {
                if (contexts.contains(InputContext::MainMenu)) {
                    contexts.pop(InputContext::MainMenu);
                    settingsUi.graphicsOpen = false;
                    settingsUi.controlsOpen = false;
                    settingsUi.rebindingAction = -1;
                    settingsUi.rebindingWaitMouseRelease = false;
                } else {
                    contexts.push(InputContext::MainMenu);
                }
            }

            if (input.pressed(Action::Pause)) {
                if (contexts.contains(InputContext::PauseMenu)) {
                    contexts.pop(InputContext::PauseMenu);
                } else if (contexts.top() == InputContext::Gameplay) {
                    contexts.push(InputContext::PauseMenu);
                } else if (contexts.contains(InputContext::InteractUI)) {
                    contexts.pop(InputContext::InteractUI);
                }
            }

            if (input.pressed(Action::Screenshot)) {
                it.world().get_mut<ScreenshotRequest>().pending = true;
            }
        });
}

}

void registerUiModule(flecs::world& world, bool debugEnabled, std::string profile) {
    registerComponents(world);
    world.set<ConsoleState>({});
    world.set<QuitRequest>({});
    world.set<ScreenshotRequest>({});
    world.set<SettingsUiState>({});
    DebugUiState debugUi{};
    debugUi.menuAvailable = debugEnabled;
    world.set<DebugUiState>(debugUi);
    world.set<FramePerfStats>({});
    world.set<EngineSessionInfo>({std::move(profile)});
    registerSystems(world);
}

void prepareUiInput(flecs::world world) {
    const InputContextStack& contexts = world.get<InputContextStack>();
    syncCursorCapture(contexts);
    if (ImGui::GetCurrentContext() != nullptr) {
        applyImGuiCursorPolicy(contexts);
    }
}

void drawUi(flecs::world world) {
    InputContextStack& contexts = world.get_mut<InputContextStack>();
    InteractionTarget& target = world.get_mut<InteractionTarget>();
    ConsoleState& console = world.get_mut<ConsoleState>();
    QuitRequest& quit = world.get_mut<QuitRequest>();
    SettingsUiState& settingsUi = world.get_mut<SettingsUiState>();
    UserSettings& settings = world.get_mut<UserSettings>();

    applyImGuiCursorPolicy(contexts);

    drawInteractionPrompt(target, contexts);

    AssetStore* assets = nullptr;
    if (world.has<AssetServices>() && world.get<AssetServices>().store != nullptr) {
        assets = world.get_mut<AssetServices>().store;
    }

    if (assets != nullptr) {
        drawScriptingErrorBanner(*assets);
    }

    if (contexts.contains(InputContext::MainMenu) && assets != nullptr) {
        DebugUiState& debugUi = world.get_mut<DebugUiState>();
        const EngineSessionInfo& sessionInfo = world.get<EngineSessionInfo>();
        drawMainMenuBar(world, *assets, quit, settingsUi, debugUi, settings, sessionInfo);
        drawGraphicsSettings(world, *assets, settingsUi, settings);
        drawControlsSettings(*assets, settingsUi, settings);
        drawEntityList(world, debugUi);
        drawEntityDetail(debugUi);
    }

    if (world.has<DebugUiState>()) {
        drawPerformanceWindow(world, world.get_mut<DebugUiState>());
    }

    if (contexts.contains(InputContext::PauseMenu) && assets != nullptr) {
        drawPauseMenu(world, *assets, contexts);
    }

    callDrawModals(world);

    if (contexts.contains(InputContext::InteractUI)) {
        drawInteractPanel(contexts, target);
    }

    if (contexts.contains(InputContext::Console)) {
        drawConsole(console, contexts);
    }
}

}
