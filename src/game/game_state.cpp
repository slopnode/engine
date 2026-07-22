#include "game/game_state.hpp"

#include "input/input_context.hpp"

namespace slopengine {

namespace {

std::optional<std::string> g_pendingMapLoad;

}

void enterMenu(flecs::world& world) {
    world.set<GameState>(GameState{GameStateKind::Menu});
    if (world.has<InputContextStack>()) {
        world.get_mut<InputContextStack>().stack = {InputContext::MainMenu};
    } else {
        world.set<InputContextStack>(InputContextStack{{InputContext::MainMenu}});
    }
}

void enterPlaying(flecs::world& world) {
    world.set<GameState>(GameState{GameStateKind::Playing});
    if (world.has<InputContextStack>()) {
        world.get_mut<InputContextStack>().stack = {InputContext::Gameplay};
    } else {
        world.set<InputContextStack>(InputContextStack{{InputContext::Gameplay}});
    }
}

void requestMapLoad(std::string_view mapName) {
    g_pendingMapLoad = std::string(mapName);
}

std::optional<std::string> takeRequestedMapLoad() {
    std::optional<std::string> pending = std::move(g_pendingMapLoad);
    g_pendingMapLoad.reset();
    return pending;
}

}
