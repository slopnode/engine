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

void enterMenu(flecs::world& world);
void enterPlaying(flecs::world& world);

inline bool isPlaying(const flecs::world& world) {
    return world.has<GameState>() && world.get<GameState>().kind == GameStateKind::Playing;
}

inline bool isMenu(const flecs::world& world) {
    return !isPlaying(world);
}

void requestMapLoad(std::string_view mapName);
std::optional<std::string> takeRequestedMapLoad();

}
