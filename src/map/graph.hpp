#pragma once

#include <raylib.h>

#include <string>
#include <vector>

namespace slopengine {

struct GraphNode {
    std::string id;
    Vector3 at{0.0f, 0.0f, 0.0f};
    bool haveAt = false;
    std::vector<std::string> tags;
};

struct GraphEdge {
    std::string from;
    std::string to;
    float cost = 1.0f;
    bool bidir = false;
};

struct NamedGraph {
    std::string id;
    std::vector<GraphNode> nodes;
    std::vector<GraphEdge> edges;
};

struct GraphDocument {
    std::vector<NamedGraph> graphs;
};

struct MapGraphs {
    GraphDocument document{};
};

}
