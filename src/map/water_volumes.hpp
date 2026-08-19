#pragma once

#include "map/brush.hpp"

#include <raylib.h>

#include <vector>

namespace slopengine {

/** One Water-role brush's bounds and underwater screen-effect properties.
 *  boundaryFaces holds every convex face except the upward-facing top, used
 *  for an accurate point-in-volume test — the AABB alone over-approximates
 *  non-box brushes and can overlap a neighboring/unrelated water volume's
 *  AABB, which reads as the tint flickering between them near the surface. */
struct WaterVolume {
    Vector3 mins{};
    Vector3 maxs{};
    std::vector<BrushFace> boundaryFaces;
    BrushWater water{};
};

/** Runtime map singleton: every Water-role brush, for eye-in-water queries. */
struct MapWaterVolumes {
    std::vector<WaterVolume> volumes;
};

/** Collects Water-role brushes into queryable volumes. */
MapWaterVolumes buildMapWaterVolumes(const std::vector<Brush>& brushes);

}
