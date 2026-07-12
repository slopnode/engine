#include "input/input_module.hpp"

#include "input/input_context.hpp"
#include "input/input_state.hpp"

#include <raylib.h>

namespace slopengine {

namespace {

struct ActionBinding {
    Action action;
    int key;
};

constexpr ActionBinding kDefaultBindings[] = {
    {Action::MoveForward, KEY_W},
    {Action::MoveBackward, KEY_S},
    {Action::MoveLeft, KEY_A},
    {Action::MoveRight, KEY_D},
    {Action::Jump, KEY_SPACE},
    {Action::Pause, KEY_ESCAPE},
    {Action::Interact, KEY_E},
    {Action::Console, KEY_GRAVE},
};

void pollInput(InputState& input) {
    for (int i = 0; i < actionCount; ++i) {
        input.actionPressed[i] = false;
        input.actionDown[i] = false;
    }

    for (const ActionBinding& binding : kDefaultBindings) {
        const int index = static_cast<int>(binding.action);
        input.actionDown[index] = IsKeyDown(binding.key);
        input.actionPressed[index] = IsKeyPressed(binding.key);
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
            pollInput(it.world().get_mut<InputState>());
        });
}

}

void registerInputModule(flecs::world& world) {
    registerComponents(world);
    world.set<InputState>({});
    world.set<InputContextStack>({});
    registerSystems(world);
}

}