#pragma once

#include "map/bsp.hpp"
#include "map/map_areas.hpp"
#include "map/pvs.hpp"

#include <cstdint>

#include <string>
#include <vector>

namespace slopengine {

struct PvsAuditResult {
    int pairsTested = 0;
    int openPairs = 0;
    int falseInvisible = 0;
    int openThroughGlass = 0;
    int falseInvisibleThroughGlass = 0;
    int portalPairs = 0;
    int portalPairsBlind = 0;
    int areaPairs = 0;
    int areaMismatch = 0;
    int interiorBlindLeaves = 0;
    int exteriorAbovePlayable = 0;
    std::vector<std::string> blindExamples;
    int missingPortals = 0;
    std::vector<std::string> missingPortalExamples;
    std::vector<std::string> examples;
    std::vector<std::string> areaExamples;
};

struct ViewAuditResult {
    std::int32_t cameraLeaf = -1;
    int facesTested = 0;
    int facesVisibleByMarch = 0;
    int facesCulledWrongly = 0;
    int facesWithoutLeaves = 0;
    std::vector<std::string> examples;
};

ViewAuditResult auditView(const BspTree& tree, const PvsFile& pvs, Vector3 camera);

bool segmentOpenThroughTree(const BspTree& tree, Vector3 a, Vector3 b);

PvsAuditResult auditPvs(
    const BspTree& tree,
    const PvsFile& pvs,
    int samplePairs,
    unsigned seed,
    const std::vector<std::uint8_t>* exteriorEmpty = nullptr,
    const MapAreas* areas = nullptr);

}
