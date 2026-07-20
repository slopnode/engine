#include "input/input_module.hpp"

#include "game/user_settings.hpp"
#include "input/action_registry.hpp"
#include "input/bind_code.hpp"
#include "input/input_context.hpp"
#include "input/input_state.hpp"
#include "script/scheme_call.hpp"
#include "script/script_context.hpp"

#include <raylib.h>
#include <s7.h>

#include <string>

namespace slopengine {

namespace {

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
}

void registerComponents(flecs::world& world) {
    world.component<InputState>();
    world.component<InputContextStack>();
}

void registerSystems(flecs::world& world) {
    world.system("PollInput")
        .kind(flecs::PreUpdate)
        .run([](flecs::iter& it) {
            pollInput(
                it.world().get_mut<InputState>(),
                it.world().get<UserSettings>().controls);
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
                tryCallSchemeProc(scheme, procName);
            }
        });
}

}

void syncCursorCapture(const InputContextStack& contexts) {
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
        return;
    }

    if (!IsCursorHidden()) {
        DisableCursor();
    }
}

void registerInputModule(flecs::world& world) {
    registerComponents(world);
    InputState input{};
    input.resize(actionRegistry().size());
    world.set<InputState>(std::move(input));
    world.set<InputContextStack>({});
    registerSystems(world);
}

}
