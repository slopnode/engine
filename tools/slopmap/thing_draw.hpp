#pragma once

#include "assets/asset_store.hpp"
#include "map/thing.hpp"

#include <raylib.h>

#include <optional>
#include <vector>

namespace slopmap {

bool drawBillboardIcon(
    slopengine::AssetStore& assets,
    const Camera3D& camera,
    Vector3 pos,
    const char* iconId,
    Color tint,
    float size = 0.4f);

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
