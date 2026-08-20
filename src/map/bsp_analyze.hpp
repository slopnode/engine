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

/** ORs inferred nodraw onto matching brush faces. */
void applyInferredNodraw(std::vector<Brush>& brushes, const MapHullAnalysis& analysis);

}
