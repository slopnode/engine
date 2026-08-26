#pragma once

#include "map/brush.hpp"
#include "map/bsp.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace slopengine {

/** Result of hull sealing analysis (leak path, inferred nodraw, warnings). */
struct MapHullAnalysis {
    bool sealed = false;
    std::vector<std::uint8_t> exteriorEmpty;
    /**
     * World-space leaf-centroid points tracing a path from the world bounds
     * (true exterior) to the leak, in walk order — point 0 is outside,
     * the last point is the leaked interior content. Empty when there's no
     * leak. Populated for both a total leak (sealed == false) and a partial
     * one (sealed == true but detailOutsideWarnings is non-empty); in the
     * partial case the destination is the nearest brush that produced a
     * detailOutsideWarnings entry. Editors can draw this as a connected line
     * (a "pointfile") so a mapper can visually follow it to the exact gap.
     */
    std::vector<Vector3> leakPath;
    std::vector<std::string> leakPathFaceIds;
    std::vector<std::string> inferredNodrawFaceIds;
    std::vector<std::string> detailOutsideWarnings;
    std::vector<std::string> duplicateFaceIdWarnings;
};

/** Analyzes whether hull brushes seal interior empty space. */
MapHullAnalysis analyzeMapHull(const BspTree& tree, const std::vector<Brush>& brushes);

/**
 * Flood-fills from every open leaf touching the world bounds through open
 * neighbors, marking each reached leaf 1 in @p exteriorEmpty (sized to
 * tree.leaves.size(), solid/blocked leaves left 0). Requires tree.leaves[].
 * neighbors to already be populated (buildAdjacency). Shared by
 * analyzeMapHull and the BSP builder's exterior leaf merge pass.
 */
void floodExteriorLeaves(const BspTree& tree, std::vector<std::uint8_t>& exteriorEmpty);

/**
 * Appends a warning for every brush whose role needs interior placement
 * (Detail/Hint/Trigger/Water) but whose centroid resolves to a leaf flagged
 * exterior in @p exteriorEmpty. Used both as an advisory warning list after a
 * confirmed-sealed build, and — before that, on the pre-merge tree — as a
 * leak signal: real placed content inside the "exterior" flood set means the
 * classification reached real interior space rather than true void.
 * @p firstOffendingLeaf, if non-null and still -1 on entry, is set to the
 * leaf index of the first offending brush found (used to aim a leak-path
 * trace at it).
 */
void collectInteriorPlacementWarnings(
    const BspTree& tree,
    const std::vector<std::uint8_t>& exteriorEmpty,
    const std::vector<Brush>& brushes,
    std::vector<std::string>& warnings,
    std::int32_t* firstOffendingLeaf = nullptr);

/** ORs inferred nodraw onto matching brush faces. */
void applyInferredNodraw(std::vector<Brush>& brushes, const MapHullAnalysis& analysis);

}
