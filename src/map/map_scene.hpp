#pragma once

#include "assets/asset_store.hpp"

#include <flecs.h>

#include <string_view>

struct s7_scheme;

namespace slopengine {

void unloadMapScene(flecs::world& world);

void changeMap(
    flecs::world& world,
    AssetStore& assets,
    s7_scheme* scheme,
    std::string_view mapName,
    std::string_view reason = "fresh");

}
