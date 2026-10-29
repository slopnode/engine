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

enum class PathInterp {
    Linear,
    CatmullRom,
};

enum class PathOrient {
    Knots,
    Tangent,
    LookAt,
};

enum class PathEase {
    Linear,
    Smooth,
    In,
    Out,
};

struct PathKnot {
    Vector3 at{0.0f, 0.0f, 0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float fov = 0.0f;
    float duration = 0.0f;
    bool haveEase = false;
    PathEase ease = PathEase::Linear;
};

struct CameraPath {
    std::string id;
    std::vector<PathKnot> knots;
    PathInterp interp = PathInterp::CatmullRom;
    PathOrient orient = PathOrient::Knots;
    Vector3 lookAt{0.0f, 0.0f, 0.0f};
    bool closed = false;
    bool constantSpeed = false;
    float duration = 2.0f;
    float fov = 0.0f;
    PathEase ease = PathEase::Linear;
};

struct GraphDocument {
    std::vector<NamedGraph> graphs;
    std::vector<CameraPath> paths;

    const CameraPath* findPath(const std::string& id) const {
        for (const CameraPath& path : paths) {
            if (path.id == id) {
                return &path;
            }
        }
        return nullptr;
    }
};

struct MapGraphs {
    GraphDocument document{};
};

const char* pathInterpName(PathInterp interp);
const char* pathOrientName(PathOrient orient);
const char* pathEaseName(PathEase ease);
bool parsePathInterp(const char* name, PathInterp& out);
bool parsePathOrient(const char* name, PathOrient& out);
bool parsePathEase(const char* name, PathEase& out);

}
