#pragma once

#include "map/nav_graph.hpp"

#include <vector>

namespace slopengine {

/** Floods loudness outward from @p originLeaf across the same leaf/portal
 *  adjacency graph used for nav pathing (@ref MapNavigation), attenuating by
 *  @p falloffPerUnit per unit of cumulative portal-hop distance and hard-
 *  blocking any link gated by a closed door (per @p isDoorOpen, same
 *  semantics as findLeafPath's). Returns one perceived-loudness value per
 *  leaf, sized to nav.leafCount; 0 means unreached/inaudible. */
std::vector<float> floodSound(
    const MapNavigation& nav,
    int originLeaf,
    float loudness,
    float falloffPerUnit,
    const DoorOpenQuery& isDoorOpen = {});

}
