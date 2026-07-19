#pragma once

#include "assets/asset_store.hpp"
#include "map/radiosity.hpp"

#include <string_view>
#include <vector>

struct s7_scheme;

namespace slopengine {

std::vector<RadiosityLight> collectRadiosityLights(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName);

}
