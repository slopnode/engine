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

void reportError(
    flecs::world& world,
    std::string_view title,
    std::string_view message,
    std::string_view context,
    std::vector<LogEntry> details) {
    ErrorReport report;
    report.open = true;
    report.title = std::string(title);
    report.message = std::string(message);
    report.context = std::string(context);
    report.details = std::move(details);
    world.set<ErrorReport>(std::move(report));
}

void reportMapLoadFailure(
    flecs::world& world,
    std::string_view mapName,
    std::string_view stage,
    std::vector<LogEntry> details) {
    reportError(
        world,
        "Map Load Failed",
        "Failed to read map '" + std::string(mapName) + "'.",
        stage.empty() ? std::string() : "Stage: " + std::string(stage),
        std::move(details));
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
