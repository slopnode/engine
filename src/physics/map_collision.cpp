#include "physics/map_collision.hpp"

namespace slopengine {

void addStaticBrushes(PhysicsWorld& world, const std::vector<Brush>& brushes) {
    world.addStaticBrushes(brushes);
}

}
