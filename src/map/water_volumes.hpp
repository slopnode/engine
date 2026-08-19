#pragma once

#include "map/brush.hpp"

#include <raylib.h>

#include <vector>

namespace slopengine {

/** One Water-role brush's bounds and underwater screen-effect properties. */
struct WaterVolume {
    Vector3 mins{};
    Vector3 maxs{};
    BrushWater water{};
};

/** Runtime map singleton: every Water-role brush, for eye-in-water queries. */
struct MapWaterVolumes {
    std::vector<WaterVolume> volumes;
};

/** Collects Water-role brushes into queryable volumes. */
MapWaterVolumes buildMapWaterVolumes(const std::vector<Brush>& brushes);

}
