#pragma once

#include "map/brush.hpp"
#include "map/bsp.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace slopengine {

struct MapHullAnalysis {
    bool sealed = false;
    std::vector<std::uint8_t> exteriorEmpty;
    std::vector<std::string> leakPathFaceIds;
    std::vector<std::string> inferredNodrawFaceIds;
    std::vector<std::string> detailOutsideWarnings;
};

MapHullAnalysis analyzeMapHull(const BspTree& tree, const std::vector<Brush>& brushes);
void applyInferredNodraw(std::vector<Brush>& brushes, const MapHullAnalysis& analysis);

}
