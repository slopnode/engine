#pragma once

#include "map/brush.hpp"

#include <raylib.h>

#include <cstdint>
#include <string>
#include <vector>

namespace slopengine {

/** Splitting plane in a BSP node. */
struct BspPlane {
    Vector3 normal{};
    float distance = 0.0f;
};

/** Internal BSP node with front/back child indices. */
struct BspNode {
    BspPlane plane{};
    std::int32_t front = -1;
    std::int32_t back = -1;
};

/** BSP leaf: solid or empty, with neighbor links. */
struct BspLeaf {
    bool solid = false;
    Vector3 mins{};
    Vector3 maxs{};
    std::vector<std::vector<Vector3>> faces;
    std::vector<std::int32_t> neighbors;
};

/** Hull face that borders empty space (used for nodraw / debug). */
struct BspSurfaceFace {
    std::vector<Vector3> vertices;
    Vector3 normal{};
    std::int32_t emptyLeaf = -1;
    std::string id;
    std::string material;
    Vector2 uvShiftPixels{};
};

/** Compiled hull BSP tree. */
struct BspTree {
    std::vector<BspNode> nodes;
    std::vector<BspLeaf> leaves;
    std::vector<BspSurfaceFace> surfaceFaces;
    std::int32_t root = -1;
    Vector3 boundsMins{};
    Vector3 boundsMaxs{};
};

/** Builds a BSP from hull brushes only. */
BspTree buildBspFromHullBrushes(const std::vector<Brush>& brushes);

void collectFaceEmptyProbes(
    const std::vector<Vector3>& vertices,
    Vector3 normal,
    std::vector<Vector3>& out);

/** Returns the leaf index containing @p point. */
std::int32_t pointLeaf(const BspTree& tree, Vector3 point);
bool leafIsEmpty(const BspTree& tree, std::int32_t leafIndex);
const std::vector<std::int32_t>& leafNeighbors(const BspTree& tree, std::int32_t leafIndex);
Vector3 leafCentroid(const BspLeaf& leaf);

/** Loaded map BSP blob. */
struct MapBsp {
    BspTree tree{};
};

/** Runtime lightmap shader toggle state on the map entity. */
struct MapLightmapState {
    bool available = false;
    int useLightmapLoc = -1;
    Shader lightmapShader{};
};

/** Tag on entities spawned as part of the active map scene. */
struct MapOwned {};

/** Singleton tracking the folder id of the currently loaded map. */
struct CurrentMap {
    std::string id;
};

}
