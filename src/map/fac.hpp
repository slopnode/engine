#pragma once

#include "map/brush.hpp"
#include "map/bsp.hpp"
#include "map/bsp_analyze.hpp"

#include <cstdint>
#include <string>
#include <unordered_set>
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
    Vector2 uvScale{1.0f, 1.0f};
    Vector3 uvUAxis{};
    Vector3 uvVAxis{};
    bool uvLock = false;
    std::int32_t interiorLeaf = -1;
    bool transparent = false;
};

/** In-memory / on-disk face set (FAC1). */
struct FacFile {
    std::vector<VisibleFace> faces;
};

/** Runtime map FAC blob. */
struct MapFac {
    FacFile fac{};
};

/** Result of building visible faces from BSP + brushes. */
struct FacBuildResult {
    FacFile fac{};
    std::vector<std::string> inferredNodrawFaceIds;
};

/** Clips hull and detail faces to sealed interior empty leaves; then welds, culls, merges, sorts. */
FacBuildResult buildVisibleFaces(
    const BspTree& tree,
    const MapHullAnalysis& analysis,
    const std::vector<Brush>& brushes,
    const std::unordered_set<std::string>* skipBrushIds = nullptr);

/** Inserts T-junction vertices on shared edges. */
void weldVisibleFaceTJunctions(std::vector<VisibleFace>& faces);

/** Merges coplanar adjacent faces that share material and UV frame. */
void mergeCoplanarVisibleFaces(std::vector<VisibleFace>& faces);

}
