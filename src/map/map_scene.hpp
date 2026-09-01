#pragma once

#include "assets/asset_store.hpp"
#include "map/map_script.hpp"

#include <flecs.h>

#include <string_view>

struct s7_scheme;

namespace slopengine {

void unloadMapScene(flecs::world& world);

/** Registers ECS entities/physics/nav/things for an already-loaded map. */
bool assembleMapScene(
    flecs::world& world,
    AssetStore& assets,
    s7_scheme* scheme,
    std::string_view mapName,
    std::string_view reason,
    LoadedMap&& loaded);

void changeMap(
    flecs::world& world,
    AssetStore& assets,
    s7_scheme* scheme,
    std::string_view mapName,
    std::string_view reason = "fresh");

}
