#pragma once

#include "map/bsp.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace slopengine {

/** Link between two areas through a Door brush's leaves. */
struct MapAreaPortal {
    std::int32_t areaA = -1;
    std::int32_t areaB = -1;
    std::string doorBrushId;
};

/** Leaves grouped into regions separated only by Door brushes. The PVS says
 *  what could be seen with every door open; area portals say which of those
 *  regions a closed door currently cuts off. */
struct MapAreas {
    std::int32_t areaCount = 0;
    std::vector<std::int32_t> leafArea;
    std::vector<MapAreaPortal> portals;
};

MapAreas buildMapAreas(const BspTree& tree);

/** Areas reachable from @p fromArea through portals flagged open in @p portalOpen. */
std::vector<std::uint8_t> floodOpenAreas(
    const MapAreas& areas,
    std::int32_t fromArea,
    const std::vector<std::uint8_t>& portalOpen);

/** Per-query view of what the viewer's area can currently reach. A null
 *  @p areas or @p open means ungated. */
struct AreaGate {
    const MapAreas* areas = nullptr;
    const std::vector<std::uint8_t>* open = nullptr;
};

/** False only when @p toLeaf is an open leaf in an area the gate cannot reach.
 *  Door leaves always pass so a closed door is never culled from its own side. */
bool areaGateAllows(const AreaGate& gate, const BspTree& tree, std::int32_t toLeaf);

}
