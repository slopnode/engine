#pragma once

#include "map/brush.hpp"

#include <raylib.h>

#include <cstdint>
#include <string>
#include <vector>

namespace slopengine {

namespace BspContents {
constexpr std::uint32_t Solid = 1u << 0;
constexpr std::uint32_t Glass = 1u << 1;
constexpr std::uint32_t Water = 1u << 2;
constexpr std::uint32_t Trigger = 1u << 3;
/** Set alongside Solid on a Door brush's closed-shape leaf. Leaves stay
 *  Solid for flood/leak/radiosity/debug purposes, but this bit lets portal-
 *  graph consumers (nav, sound, PVS) traverse the leaf as a doorway hop
 *  instead of treating it as a permanent wall. */
constexpr std::uint32_t Door = 1u << 4;
}

inline bool leafBlocksFlood(std::uint32_t contents) {
    return (contents & (BspContents::Solid | BspContents::Glass)) != 0;
}

inline bool leafIsOpen(std::uint32_t contents) {
    return !leafBlocksFlood(contents);
}

/** True for open leaves and Door-brush leaves: leaves that should form
 *  BspPortal links in the portal graph used by nav pathing, sound
 *  propagation, and PVS. A closed door still blocks light/leak flood and
 *  physics (via the brush's own block mask), but a door leaf must not sever
 *  the portal graph the way a permanent wall does, since the door can open. */
inline bool leafParticipatesInPortalGraph(std::uint32_t contents) {
    return leafIsOpen(contents) || (contents & BspContents::Door) != 0;
}

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

/** BSP leaf: contents flags, neighbor links, and polyhedron faces. */
struct BspLeaf {
    std::uint32_t contents = 0;
    Vector3 mins{};
    Vector3 maxs{};
    std::vector<std::vector<Vector3>> faces;
    std::vector<std::int32_t> neighbors;
};

/** BspNode::front/back encode a leaf as a negative index; these are the
 *  single source of truth for that convention so callers outside bsp_build.cpp
 *  (e.g. a tree-guided leaf query) don't have to duplicate it. */
inline bool bspIsLeafChild(std::int32_t child) {
    return child < 0;
}

inline std::int32_t bspEncodeLeaf(std::int32_t leafIndex) {
    return -leafIndex - 1;
}

inline std::int32_t bspDecodeLeaf(std::int32_t child) {
    return -child - 1;
}

/** Portal polygon between two open leaves. */
struct BspPortal {
    std::int32_t leafA = -1;
    std::int32_t leafB = -1;
    std::vector<Vector3> vertices;
    /** Brush id of the Door whose closed shape produced this portal boundary, or empty if none. */
    std::string doorBrushId;
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
    std::vector<BspPortal> portals;
    std::vector<BspSurfaceFace> surfaceFaces;
    std::int32_t root = -1;
    Vector3 boundsMins{};
    Vector3 boundsMaxs{};
};

/** Builds a BSP from sealing / split-contributing brushes. */
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
    int emissionPowerLoc = -1;
    Shader lightmapShader{};
    Shader skyShader{};
    std::vector<int> transparentMeshIndices;
    std::vector<int> skyMeshIndices;
    std::vector<int> detailMeshIndices;
    std::vector<int> twoSidedMeshIndices;
};

/** Tag on entities spawned as part of the active map scene. */
struct MapOwned {};

/** Singleton tracking the folder id of the currently loaded map. */
struct CurrentMap {
    std::string id;
};

}
