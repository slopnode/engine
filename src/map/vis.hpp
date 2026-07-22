#pragma once

#include "map/brush.hpp"
#include "map/bsp.hpp"
#include "map/bsp_analyze.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace slopengine {

/** One drawable / lightmapable face fragment after visibility clipping. */
struct VisibleFace {
    std::string id;
    std::string sourceFaceId;
    std::string material;
    Vector3 normal{};
    std::vector<Vector3> vertices;
    Vector2 uvShiftPixels{};
    Vector3 uvUAxis{};
    Vector3 uvVAxis{};
    bool uvLock = false;
    std::int32_t interiorLeaf = -1;
};

/** In-memory / on-disk visible face set (VIS1). */
struct VisFile {
    std::vector<VisibleFace> faces;
};

/** Runtime map VIS blob. */
struct MapVis {
    VisFile vis{};
};

/** Result of building visible faces from BSP + brushes. */
struct VisBuildResult {
    VisFile vis{};
    std::vector<std::string> inferredNodrawFaceIds;
};

/** Clips hull faces to sealed interior empty leaves; detail faces pass through. */
VisBuildResult buildVisibleFaces(
    const BspTree& tree,
    const MapHullAnalysis& analysis,
    const std::vector<Brush>& brushes);

/** Inserts T-junction vertices on shared edges. */
void weldVisibleFaceTJunctions(std::vector<VisibleFace>& faces);

/** Merges coplanar adjacent faces that share material and UV frame. */
void mergeCoplanarVisibleFaces(std::vector<VisibleFace>& faces);

}
