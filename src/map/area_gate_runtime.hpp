#pragma once

#include "map/map_areas.hpp"

#include <flecs.h>
#include <raylib.h>

#include <cstdint>
#include <vector>

namespace slopengine {

/** Per-frame cache behind areaGateFromPoint: door states are read once per
 *  frame and the area flood once per distinct viewer area. */
struct MapAreaGateCache {
    std::int64_t frame = -1;
    std::vector<std::uint8_t> portalOpen;
    std::int32_t fromArea = -2;
    std::vector<std::uint8_t> open;
};

/** Gate for PVS queries made from @p from. Ungated when the map has no areas,
 *  the point is not in an open leaf, or the point sits inside a door. */
AreaGate areaGateFromPoint(flecs::world& world, Vector3 from);

}
