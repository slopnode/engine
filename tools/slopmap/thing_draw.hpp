#pragma once

#include "assets/asset_store.hpp"
#include "map/thing.hpp"

#include <raylib.h>

#include <optional>
#include <vector>

namespace slopmap {

void drawThings(
    slopengine::AssetStore& assets,
    const std::vector<slopengine::Thing>& things,
    const std::vector<int>& selectedThings,
    const Camera3D& camera,
    bool showGizmos = true);

std::optional<int> pickThing(
    const std::vector<slopengine::Thing>& things,
    Ray ray,
    float* outDistance = nullptr);

}
