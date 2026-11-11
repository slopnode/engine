#pragma once

#include "core/log.hpp"
#include "core/sim_clock.hpp"
#include "input/input_context.hpp"

#include <flecs.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

enum class GameStateKind {
    Menu,
    Playing,
};

struct GameState {
    GameStateKind kind = GameStateKind::Menu;
};

struct PendingMapLoad {
    std::string mapName;
    std::string reason = "fresh";
};

struct ErrorReport {
    bool open = false;
    std::string title;
    std::string message;
    std::string context;
    std::vector<LogEntry> details;
};

void enterMenu(flecs::world& world);
void reportError(
    flecs::world& world,
    std::string_view title,
    std::string_view message,
    std::string_view context,
    std::vector<LogEntry> details);
void reportMapLoadFailure(
    flecs::world& world,
    std::string_view mapName,
    std::string_view stage,
    std::vector<LogEntry> details);
void enterPlaying(flecs::world& world);

inline bool isPlaying(const flecs::world& world) {
    return world.has<GameState>() && world.get<GameState>().kind == GameStateKind::Playing;
}

inline bool isMenu(const flecs::world& world) {
    return !isPlaying(world);
}

inline bool isSimulationPaused(const flecs::world& world) {
    if (!world.has<InputContextStack>() || world.has<SimShared>() || world.has<SimRemote>()) {
        return false;
    }
    const InputContextStack& contexts = world.get<InputContextStack>();
    if (contexts.contains(InputContext::PauseMenu)) {
        return true;
    }
    return isPlaying(world) && contexts.contains(InputContext::MainMenu);
}

void requestMapLoad(std::string_view mapName, std::string_view reason = "fresh");
bool hasPendingMapLoad();
std::optional<PendingMapLoad> takeRequestedMapLoad();

}
