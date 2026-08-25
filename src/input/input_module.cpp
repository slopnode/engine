#include "input/input_module.hpp"

#include "game/user_settings.hpp"
#include "input/action_registry.hpp"
#include "input/actions.hpp"
#include "input/bind_code.hpp"
#include "input/input_command.hpp"
#include "input/input_context.hpp"
#include "input/input_state.hpp"
#include "script/scheme_call.hpp"
#include "script/script_context.hpp"

#include <raylib.h>
#include <s7.h>

#include <string>

namespace slopengine {

namespace {

constexpr int kMouseCaptureGraceFrames = 2;

void pollBind(int bind, bool& down, bool& pressed) {
    if (bind == KEY_NULL) {
        down = false;
        pressed = false;
        return;
    }
    if (isMouseBind(bind)) {
        const int button = mouseButtonFromBind(bind);
        down = IsMouseButtonDown(button);
        pressed = IsMouseButtonPressed(button);
        return;
    }
    down = IsKeyDown(bind);
    pressed = IsKeyPressed(bind);
}

void pollInput(InputState& input, const ControlsSettings& controls) {
    const int count = static_cast<int>(controls.binds.size());
    if (static_cast<int>(input.actionPressed.size()) != count) {
        input.resize(count);
    }

    for (int i = 0; i < count; ++i) {
        bool down = false;
        bool pressed = false;
        pollBind(controls.binds[static_cast<std::size_t>(i)], down, pressed);
        input.actionDown[static_cast<std::size_t>(i)] = down ? 1 : 0;
        input.actionPressed[static_cast<std::size_t>(i)] = pressed ? 1 : 0;
    }

    input.mouseDelta = GetMouseDelta();
    if (input.mouseCaptureGraceFrames > 0) {
        input.mouseDelta = {0.0f, 0.0f};
        input.mouseCaptureGraceFrames -= 1;
    }
}

void registerComponents(flecs::world& world) {
    world.component<InputState>();
    world.component<InputContextStack>();
    world.component<InputCommand>();
}

void registerSystems(flecs::world& world) {
    world.system("PollInput")
        .kind(flecs::PreUpdate)
        .run([](flecs::iter& it) {
            pollInput(
                it.world().get_mut<InputState>(),
                it.world().get<UserSettings>().controls);
        });

    world.system("CaptureInputCommand")
        .kind(flecs::PreUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            const InputState& input = world.get<InputState>();
            InputCommand& command = world.get_mut<InputCommand>();
            command.tick += 1;
            command.moveForward = (input.down(Action::MoveForward) ? 1.0f : 0.0f) -
                                   (input.down(Action::MoveBackward) ? 1.0f : 0.0f);
            command.moveStrafe = (input.down(Action::MoveRight) ? 1.0f : 0.0f) -
                                  (input.down(Action::MoveLeft) ? 1.0f : 0.0f);
            command.moveUp = (input.down(Action::Jump) ? 1.0f : 0.0f) -
                              (input.down(Action::Descend) ? 1.0f : 0.0f);
            command.look = input.mouseDelta;
        });

    world.system("DispatchPackageActions")
        .kind(flecs::OnUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            InputState& input = world.get_mut<InputState>();
            InputContextStack& contexts = world.get_mut<InputContextStack>();

            if (!contexts.allowsGameplay()) {
                return;
            }
            if (!world.has<ScriptContext>() || world.get<ScriptContext>().scheme == nullptr) {
                return;
            }

            s7_scheme* scheme = world.get<ScriptContext>().scheme;
            const ActionRegistry& registry = actionRegistry();
            for (int i = registry.coreCount(); i < registry.size(); ++i) {
                if (!input.pressed(i)) {
                    continue;
                }
                const std::string procName = std::string("on-action-") + registry.at(i).id;
                tryCallSchemeProc(scheme, procName, ScriptScope::World);
            }
        });
}

}

void syncCursorCapture(const InputContextStack& contexts, InputState& input) {
    static bool uiCursorActive = false;
    const bool wantUiCursor = contexts.blocksWorldInput() || !IsWindowFocused();

    if (wantUiCursor) {
        if (!uiCursorActive) {
            EnableCursor();
            uiCursorActive = true;
        } else {
            ShowCursor();
        }
        return;
    }

    if (uiCursorActive) {
        DisableCursor();
        uiCursorActive = false;
        input.mouseCaptureGraceFrames = kMouseCaptureGraceFrames;
        return;
    }

    if (!IsCursorHidden()) {
        DisableCursor();
        input.mouseCaptureGraceFrames = kMouseCaptureGraceFrames;
    }
}

void registerInputModule(flecs::world& world) {
    registerComponents(world);
    InputState input{};
    input.resize(actionRegistry().size());
    world.set<InputState>(std::move(input));
    world.set<InputContextStack>({});
    world.set<InputCommand>({});
    registerSystems(world);
}

}
