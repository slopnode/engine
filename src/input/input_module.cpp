#include "input/input_module.hpp"

#include "game/user_settings.hpp"
#include "input/input_context.hpp"
#include "input/input_state.hpp"

#include <raylib.h>

namespace slopengine {

namespace {

void pollInput(InputState& input, const ControlsSettings& controls) {
    for (int i = 0; i < actionCount; ++i) {
        const int key = controls.keys[i];
        if (key == KEY_NULL) {
            input.actionPressed[i] = false;
            input.actionDown[i] = false;
            continue;
        }
        input.actionDown[i] = IsKeyDown(key);
        input.actionPressed[i] = IsKeyPressed(key);
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
    world.set<InputState>({});
    world.set<InputContextStack>({});
    registerSystems(world);
}

}
