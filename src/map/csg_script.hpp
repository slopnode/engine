#pragma once

#include "assets/asset_store.hpp"
#include "map/brush.hpp"
#include "map/bsp.hpp"
#include "map/map_meta.hpp"

#include <raylib.h>

#include <optional>
#include <string_view>
#include <vector>

struct s7_scheme;

namespace slopengine {

struct LoadedMap {
    Model model{};
    std::vector<Brush> brushes;
    BspTree bsp{};
    MapMeta meta{};
};

std::optional<LoadedMap> loadAndCompileMap(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName);

}
