#pragma once

#include "assets/asset_store.hpp"
#include "game/app_config.hpp"

#include <flecs.h>

#include <string_view>

struct s7_scheme;

namespace slopengine {

/** Registers render systems and components on @p world. */
void registerRenderModule(
    flecs::world& world,
    AssetStore& assets,
    const AppConfig& config,
    s7_scheme* scheme);

/** Tears down the active map scene (entities, physics, GPU map resources). */
void unloadMapScene(flecs::world& world);

/** Unloads any active map and loads @p mapName from mounted packages. */
void changeMap(
    flecs::world& world,
    AssetStore& assets,
    s7_scheme* scheme,
    std::string_view mapName,
    std::string_view reason = "fresh");

}
