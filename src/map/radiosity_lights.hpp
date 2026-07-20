#pragma once

#include "assets/asset_store.hpp"
#include "map/radiosity.hpp"

#include <string_view>
#include <vector>

struct s7_scheme;

namespace slopengine {

/** Collects point/spot lights from map entities.s7 and prefab sidecars for bake. */
std::vector<RadiosityLight> collectRadiosityLights(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName);

}
