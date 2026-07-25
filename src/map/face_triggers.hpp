#pragma once

#include "map/brush.hpp"

#include <flecs.h>

#include <vector>

namespace slopengine {

void spawnFaceUseSurfaces(flecs::world& world, const std::vector<Brush>& brushes);

}
