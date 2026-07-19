#pragma once

#include "assets/asset_store.hpp"
#include "map/placement.hpp"

#include <raylib.h>

#include <optional>
#include <vector>

namespace slopmap {

void drawPlacements(
    slopengine::AssetStore& assets,
    const std::vector<slopengine::Placement>& placements,
    int selectedPlacement,
    const Camera3D& camera);

std::optional<int> pickPlacement(
    const std::vector<slopengine::Placement>& placements,
    Ray ray,
    float* outDistance = nullptr);

}
