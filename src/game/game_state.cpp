#include "game/game_state.hpp"

#include "assets/asset_services.hpp"
#include "game/menu_background.hpp"
#include "input/input_context.hpp"

namespace slopengine {

namespace {

std::optional<PendingMapLoad> g_pendingMapLoad;

}

void enterMenu(flecs::world& world) {
    world.set<GameState>(GameState{GameStateKind::Menu});
    if (world.has<InputContextStack>()) {
        world.get_mut<InputContextStack>().stack = {InputContext::MainMenu};
    } else {
        world.set<InputContextStack>(InputContextStack{{InputContext::MainMenu}});
    }
    if (world.has<AssetServices>() && world.get<AssetServices>().store != nullptr) {
        applyMenuBackground(world, *world.get<AssetServices>().store);
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

void requestMapLoad(std::string_view mapName, std::string_view reason) {
    PendingMapLoad pending;
    pending.mapName = std::string(mapName);
    pending.reason = reason.empty() ? std::string("fresh") : std::string(reason);
    g_pendingMapLoad = std::move(pending);
}

bool hasPendingMapLoad() {
    return g_pendingMapLoad.has_value();
}

std::optional<PendingMapLoad> takeRequestedMapLoad() {
    std::optional<PendingMapLoad> pending = std::move(g_pendingMapLoad);
    g_pendingMapLoad.reset();
    return pending;
}

}
