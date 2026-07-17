#pragma once

#include "map/brush.hpp"

#include <raylib.h>

#include <cstdint>
#include <string>
#include <vector>

namespace slopengine {

struct BspPlane {
    Vector3 normal{};
    float distance = 0.0f;
};

struct BspNode {
    BspPlane plane{};
    std::int32_t front = -1;
    std::int32_t back = -1;
};

struct BspLeaf {
    bool solid = false;
    Vector3 mins{};
    Vector3 maxs{};
    std::vector<std::vector<Vector3>> faces;
    std::vector<std::int32_t> neighbors;
};

struct BspSurfaceFace {
    std::vector<Vector3> vertices;
    Vector3 normal{};
    std::int32_t emptyLeaf = -1;
    std::string id;
    std::string material;
    Vector2 uvShiftPixels{};
};

struct BspTree {
    std::vector<BspNode> nodes;
    std::vector<BspLeaf> leaves;
    std::vector<BspSurfaceFace> surfaceFaces;
    std::int32_t root = -1;
    Vector3 boundsMins{};
    Vector3 boundsMaxs{};
};

BspTree buildBspFromHullBrushes(const std::vector<Brush>& brushes);

std::int32_t pointLeaf(const BspTree& tree, Vector3 point);
bool leafIsEmpty(const BspTree& tree, std::int32_t leafIndex);
const std::vector<std::int32_t>& leafNeighbors(const BspTree& tree, std::int32_t leafIndex);
Vector3 leafCentroid(const BspLeaf& leaf);

struct MapBsp {
    BspTree tree{};
};

struct MapLightmapState {
    bool available = false;
    int useLightmapLoc = -1;
};

}
