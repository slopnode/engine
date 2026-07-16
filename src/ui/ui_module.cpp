#include "ui/ui_module.hpp"

#include "game/user_settings.hpp"
#include "input/actions.hpp"
#include "input/input_context.hpp"
#include "input/input_module.hpp"
#include "input/input_state.hpp"
#include "interact/components.hpp"
#include "ui/ui_state.hpp"

#include "imgui.h"

#include <cstdio>
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
        ImGui::MenuItem("BSP Outlines", nullptr, &debugUi.showBspOutlines);
        ImGui::MenuItem("BSP Leaf Faces", nullptr, &debugUi.showBspLeafFaces);
        ImGui::MenuItem("BSP Portals", nullptr, &debugUi.showBspPortals);
        ImGui::MenuItem("BSP Surface Faces", nullptr, &debugUi.showBspSurfaceFaces);
        ImGui::MenuItem("BSP Current Leaf Only", nullptr, &debugUi.showBspCurrentLeafOnly);
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
