#include "ui/ui_module.hpp"

#include "input/actions.hpp"
#include "input/input_context.hpp"
#include "input/input_state.hpp"
#include "interact/components.hpp"
#include "ui/ui_state.hpp"

#include "imgui.h"

namespace slopengine {

namespace {

void logConsoleMessage(ConsoleState& console, const std::string& message) {
    console.log.push_back(message);
    if (console.log.size() > 200) {
        console.log.erase(console.log.begin());
    }
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

void registerComponents(flecs::world& world) {
    world.component<ConsoleState>();
}

void registerSystems(flecs::world& world) {
    world.system("HandleUiActions")
        .kind(flecs::PreUpdate)
        .run([](flecs::iter& it) {
            InputState& input = it.world().get_mut<InputState>();
            InputContextStack& contexts = it.world().get_mut<InputContextStack>();
            ConsoleState& console = it.world().get_mut<ConsoleState>();

            if (input.pressed(Action::Console)) {
                if (contexts.contains(InputContext::Console)) {
                    contexts.pop(InputContext::Console);
                    console.open = false;
                } else {
                    contexts.push(InputContext::Console);
                    console.open = true;
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
    registerSystems(world);
}

void drawUi(flecs::world world) {
    InputContextStack& contexts = world.get_mut<InputContextStack>();
    InteractionTarget& target = world.get_mut<InteractionTarget>();
    ConsoleState& console = world.get_mut<ConsoleState>();

    drawInteractionPrompt(target, contexts);

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