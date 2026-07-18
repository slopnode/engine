#include "ui/ui_module.hpp"

#include "camera/components.hpp"
#include "game/user_settings.hpp"
#include "input/actions.hpp"
#include "input/input_context.hpp"
#include "input/input_module.hpp"
#include "input/input_state.hpp"
#include "interact/components.hpp"
#include "physics/components.hpp"
#include "render/animation_player.hpp"
#include "render/components.hpp"
#include "render/sprite_animator.hpp"
#include "ui/ui_state.hpp"

#include "imgui.h"

#include <cstdio>
#include <cstring>
#include <raylib.h>
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

const char* keyDisplayName(int key, char* buffer, std::size_t bufferSize) {
    if (key == KEY_NULL) {
        return "Unbound";
    }
    if (key == KEY_SPACE) {
        return "Space";
    }
    if (key == KEY_ESCAPE) {
        return "Escape";
    }
    if (key == KEY_ENTER) {
        return "Enter";
    }
    if (key == KEY_TAB) {
        return "Tab";
    }
    if (key == KEY_LEFT_SHIFT || key == KEY_RIGHT_SHIFT) {
        return "Shift";
    }
    if (key == KEY_LEFT_CONTROL || key == KEY_RIGHT_CONTROL) {
        return "Ctrl";
    }
    if (key == KEY_LEFT_ALT || key == KEY_RIGHT_ALT) {
        return "Alt";
    }
    if (key == KEY_UP) {
        return "Up";
    }
    if (key == KEY_DOWN) {
        return "Down";
    }
    if (key == KEY_LEFT) {
        return "Left";
    }
    if (key == KEY_RIGHT) {
        return "Right";
    }
    if (key == KEY_GRAVE) {
        return "`";
    }

    const char* name = GetKeyName(key);
    if (name != nullptr && name[0] != '\0') {
        return name;
    }

    std::snprintf(buffer, bufferSize, "Key %d", key);
    return buffer;
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

void openGraphicsSettings(SettingsUiState& settingsUi, const UserSettings& settings) {
    settingsUi.graphicsOpen = true;
    settingsUi.graphicsDraft = settings.graphics;
}

void openControlsSettings(SettingsUiState& settingsUi, const UserSettings& settings) {
    settingsUi.controlsOpen = true;
    settingsUi.controlsDraft = settings.controls;
    settingsUi.rebindingAction = -1;
}

void applyGraphicsDraft(UserSettings& settings, const GraphicsSettings& draft) {
    settings.graphics = draft;
    applyGraphicsSettings(settings.graphics);
    settings.save();
}

void applyControlsDraft(UserSettings& settings, const ControlsSettings& draft) {
    settings.controls = draft;
    settings.save();
}

void drawPauseMenu(InputContextStack& contexts) {
    ImGui::SetNextWindowSize({320.0f, 180.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
        {ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f},
        ImGuiCond_Always,
        {0.5f, 0.5f});

    if (ImGui::Begin("Paused", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {
        ImGui::TextUnformatted("Simulation continues while paused.");
        if (ImGui::Button("Resume")) {
            contexts.pop(InputContext::PauseMenu);
        }
    }
    ImGui::End();
}

void drawGraphicsSettings(SettingsUiState& settingsUi, UserSettings& settings) {
    if (!settingsUi.graphicsOpen) {
        return;
    }

    ImGui::SetNextWindowSize({420.0f, 260.0f}, ImGuiCond_FirstUseEver);
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

    ImGui::Separator();
    if (ImGui::Button("Apply")) {
        applyGraphicsDraft(settings, draft);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        draft = UserSettings::defaults().graphics;
    }

    ImGui::End();
}

void drawControlsSettings(SettingsUiState& settingsUi, UserSettings& settings) {
    if (!settingsUi.controlsOpen) {
        return;
    }

    ImGui::SetNextWindowSize({420.0f, 420.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Controls", &settingsUi.controlsOpen, ImGuiWindowFlags_NoCollapse)) {
        settingsUi.rebindingAction = -1;
        ImGui::End();
        return;
    }

    if (settingsUi.rebindingAction >= 0) {
        ImGui::Text("Press a key for %s (Esc to cancel)...",
            actionLabel(static_cast<Action>(settingsUi.rebindingAction)));
    } else {
        ImGui::TextUnformatted("Click a binding to reassign it.");
    }

    ImGui::Separator();
    if (ImGui::BeginTable("ControlsTable", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Action");
        ImGui::TableSetupColumn("Key");
        ImGui::TableHeadersRow();

        char keyBuffer[64];
        for (int i = 0; i < actionCount; ++i) {
            const Action action = static_cast<Action>(i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(actionLabel(action));
            ImGui::TableSetColumnIndex(1);

            const char* label = keyDisplayName(settingsUi.controlsDraft.keys[i], keyBuffer, sizeof(keyBuffer));
            ImGui::PushID(i);
            const bool listening = settingsUi.rebindingAction == i;
            if (ImGui::Button(listening ? "..." : label, {140.0f, 0.0f})) {
                settingsUi.rebindingAction = i;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    if (ImGui::Button("Apply")) {
        applyControlsDraft(settings, settingsUi.controlsDraft);
        settingsUi.rebindingAction = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        settingsUi.controlsDraft = ControlsSettings::defaults();
        settingsUi.rebindingAction = -1;
    }

    ImGui::End();

    if (!settingsUi.controlsOpen) {
        settingsUi.rebindingAction = -1;
    }
}

void drawMainMenuBar(
    QuitRequest& quit,
    SettingsUiState& settingsUi,
    DebugUiState& debugUi,
    const UserSettings& settings) {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Quit")) {
            quit.requested = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Config")) {
        if (ImGui::MenuItem("Graphics")) {
            openGraphicsSettings(settingsUi, settings);
        }
        if (ImGui::MenuItem("Controls")) {
            openControlsSettings(settingsUi, settings);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Debug")) {
        if (ImGui::BeginMenu("BSP")) {
            ImGui::MenuItem("Outlines", nullptr, &debugUi.showBspOutlines);
            ImGui::MenuItem("Leaf Faces", nullptr, &debugUi.showBspLeafFaces);
            ImGui::MenuItem("Portals", nullptr, &debugUi.showBspPortals);
            ImGui::MenuItem("Surface Faces", nullptr, &debugUi.showBspSurfaceFaces);
            ImGui::MenuItem("Current Leaf Only", nullptr, &debugUi.showBspCurrentLeafOnly);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Sprites")) {
            ImGui::MenuItem("Masks", nullptr, &debugUi.showSpriteMasks);
            ImGui::MenuItem("Aim", nullptr, &debugUi.showSpriteAim);
            ImGui::EndMenu();
        }
        ImGui::MenuItem("Unlit (disable lightmaps)", nullptr, &debugUi.unlit);
        ImGui::MenuItem("Noclip", nullptr, &debugUi.noclip);
        ImGui::MenuItem("Entities", nullptr, &debugUi.entityListOpen);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("About", nullptr, false, false);
        ImGui::EndMenu();
    }

    char fpsLabel[32];
    std::snprintf(fpsLabel, sizeof(fpsLabel), "%.0f FPS", static_cast<double>(ImGui::GetIO().Framerate));
    const float fpsWidth = ImGui::CalcTextSize(fpsLabel).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - fpsWidth - ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::TextUnformatted(fpsLabel);

    ImGui::EndMainMenuBar();
}

const char* entityKindLabel(flecs::entity entity) {
    if (entity.has<PlayerCamera>()) {
        return "player";
    }
    const char* name = entity.name();
    if (name != nullptr && std::strcmp(name, "MapStatic") == 0) {
        return "map";
    }
    if (entity.has<Interactable>()) {
        return "usable";
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
            ImGui::Text("Event: %s", interactable.eventName.c_str());
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

    if (!debugUi.inspectedEntity.is_alive()) {
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
        if (!target.eventName.empty()) {
            ImGui::Text("Event: %s", target.eventName.c_str());
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
    world.component<SettingsUiState>();
    world.component<DebugUiState>();
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
                const int pressed = GetKeyPressed();
                if (pressed == KEY_ESCAPE) {
                    settingsUi.rebindingAction = -1;
                } else if (pressed != 0) {
                    for (int i = 0; i < actionCount; ++i) {
                        if (i != settingsUi.rebindingAction
                            && settingsUi.controlsDraft.keys[i] == pressed) {
                            settingsUi.controlsDraft.keys[i] = KEY_NULL;
                        }
                    }
                    settingsUi.controlsDraft.keys[settingsUi.rebindingAction] = pressed;
                    settingsUi.rebindingAction = -1;
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

            if (input.pressed(Action::MainMenu)) {
                if (contexts.contains(InputContext::MainMenu)) {
                    contexts.pop(InputContext::MainMenu);
                    settingsUi.graphicsOpen = false;
                    settingsUi.controlsOpen = false;
                    settingsUi.rebindingAction = -1;
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
        });
}

}

void registerUiModule(flecs::world& world) {
    registerComponents(world);
    world.set<ConsoleState>({});
    world.set<QuitRequest>({});
    world.set<SettingsUiState>({});
    world.set<DebugUiState>({});
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

    if (contexts.contains(InputContext::MainMenu)) {
        DebugUiState& debugUi = world.get_mut<DebugUiState>();
        drawMainMenuBar(quit, settingsUi, debugUi, settings);
        drawGraphicsSettings(settingsUi, settings);
        drawControlsSettings(settingsUi, settings);
        drawEntityList(world, debugUi);
        drawEntityDetail(debugUi);
    }

    if (contexts.contains(InputContext::PauseMenu)) {
        drawPauseMenu(contexts);
    }

    if (contexts.contains(InputContext::InteractUI)) {
        drawInteractPanel(contexts, target);
    }

    if (contexts.contains(InputContext::Console)) {
        drawConsole(console, contexts);
    }
}

}
