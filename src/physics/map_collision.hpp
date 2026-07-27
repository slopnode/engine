#pragma once

#include "map/brush.hpp"
#include "physics/physics_world.hpp"

#include <string>
#include <unordered_set>
#include <vector>

namespace slopengine {

void addStaticBrushes(
    PhysicsWorld& world,
    const std::vector<Brush>& brushes,
    const std::unordered_set<std::string>* skipBrushIds = nullptr);

}
