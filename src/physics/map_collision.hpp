#pragma once

#include "map/brush.hpp"
#include "physics/physics_world.hpp"

#include <vector>

namespace slopengine {

void addStaticBrushes(PhysicsWorld& world, const std::vector<Brush>& brushes);

}
