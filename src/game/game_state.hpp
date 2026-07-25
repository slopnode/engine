#pragma once

#include <flecs.h>

#include <optional>
#include <string>
#include <string_view>

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

void enterMenu(flecs::world& world);
void enterPlaying(flecs::world& world);

inline bool isPlaying(const flecs::world& world) {
    return world.has<GameState>() && world.get<GameState>().kind == GameStateKind::Playing;
}

inline bool isMenu(const flecs::world& world) {
    return !isPlaying(world);
}

void requestMapLoad(std::string_view mapName, std::string_view reason = "fresh");
bool hasPendingMapLoad();
std::optional<PendingMapLoad> takeRequestedMapLoad();

}
