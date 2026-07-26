#include "physics/map_collision.hpp"

#include <vector>

namespace slopengine {

void addStaticBrushes(
    PhysicsWorld& world,
    const std::vector<Brush>& brushes,
    const std::unordered_set<std::string>* skipBrushIds) {
    if (skipBrushIds == nullptr || skipBrushIds->empty()) {
        world.addStaticBrushes(brushes);
        return;
    }
    std::vector<Brush> filtered;
    filtered.reserve(brushes.size());
    for (const Brush& brush : brushes) {
        if (skipBrushIds->count(brush.id) == 0) {
            filtered.push_back(brush);
        }
    }
    world.addStaticBrushes(filtered);
}

}
