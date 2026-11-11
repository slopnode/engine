#pragma once

#include "map/bsp.hpp"

#include <cstdint>
#include <vector>

namespace slopengine {

void collectPolygonLeaves(
    const BspTree& tree,
    const std::vector<Vector3>& polygon,
    std::vector<std::int32_t>& out);

}
