#pragma once

#include "assets/asset_store.hpp"
#include "map/placement.hpp"

#include <optional>
#include <string_view>

struct s7_scheme;

namespace slopengine {

/** Loads maps/<name>/entities.s7 into a placement document. */
std::optional<PlacementDocument> loadMapPlacements(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName);

/** Loads prefabs/<path>.s7 sidecar placements when present. */
std::optional<PlacementDocument> loadPrefabPlacements(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view prefabPath);

}
