#include "map/area_gate_runtime.hpp"

#include "map/pvs.hpp"
#include "physics/rigid_mover.hpp"

namespace slopengine {

namespace {

bool doorPortalOpen(flecs::world& world, const std::string& doorBrushId) {
    if (doorBrushId.empty()) {
        return true;
    }
    flecs::entity door = world.lookup(doorBrushId.c_str());
    if (!door.is_valid() || !door.has<RigidMover>()) {
        return true;
    }
    const RigidMover& mover = door.get<RigidMover>();
    return mover.progress > 1.0e-3f || mover.target >= 0.5f;
}

} // namespace

AreaGate areaGateFromPoint(flecs::world& world, Vector3 from) {
    if (!world.has<MapAreas>() || !world.has<MapBsp>()) {
        return {};
    }
    const MapAreas& areas = world.get<MapAreas>();
    const BspTree& tree = world.get<MapBsp>().tree;
    if (areas.portals.empty()) {
        return {};
    }
    const std::int32_t fromLeaf = pvsSampleLeaf(tree, from);
    if (fromLeaf < 0 || !leafIsOpen(tree.leaves[static_cast<std::size_t>(fromLeaf)].contents)) {
        return {};
    }
    const std::int32_t fromArea = areas.leafArea[static_cast<std::size_t>(fromLeaf)];
    if (fromArea < 0) {
        return {};
    }

    if (!world.has<MapAreaGateCache>()) {
        return {};
    }
    MapAreaGateCache& cache = world.get_mut<MapAreaGateCache>();
    const std::int64_t frame = world.get_info()->frame_count_total;
    if (cache.frame != frame) {
        cache.frame = frame;
        cache.fromArea = -2;
        cache.portalOpen.resize(areas.portals.size());
        for (std::size_t i = 0; i < areas.portals.size(); ++i) {
            cache.portalOpen[i] = doorPortalOpen(world, areas.portals[i].doorBrushId) ? 1 : 0;
        }
    }
    if (cache.fromArea != fromArea) {
        cache.fromArea = fromArea;
        cache.open = floodOpenAreas(areas, fromArea, cache.portalOpen);
    }
    return AreaGate{&areas, &cache.open};
}

}
