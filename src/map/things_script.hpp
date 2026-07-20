#pragma once

#include "assets/asset_store.hpp"
#include "map/thing.hpp"

#include <optional>
#include <string_view>

struct s7_scheme;

namespace slopengine {

std::optional<ThingDocument> loadMapThings(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName);

std::optional<ThingDocument> loadPrefabThings(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view prefabPath);

}
