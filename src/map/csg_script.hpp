#pragma once

#include "assets/asset_store.hpp"
#include "map/brush.hpp"

#include <raylib.h>

#include <optional>
#include <string_view>
#include <vector>

struct s7_scheme;

namespace slopengine {

struct LoadedMap {
    Model model{};
    std::vector<Brush> brushes;
};

std::optional<LoadedMap> loadAndCompileMap(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName);

}
