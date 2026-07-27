#pragma once

#include "map/brush.hpp"

#include <flecs.h>

#include <string>
#include <unordered_set>
#include <vector>

namespace slopengine {

void spawnFaceUseSurfaces(
    flecs::world& world,
    const std::vector<Brush>& brushes,
    const std::unordered_set<std::string>* skipBrushIds = nullptr);

}
