#include "map/graph_write.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace slopengine {

namespace {

std::string formatFloat(float value) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(value));
    return buf;
}

std::string escapeSchemeString(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (char c : value) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string vec3(Vector3 v) {
    return formatFloat(v.x) + " " + formatFloat(v.y) + " " + formatFloat(v.z);
}

void writeGraph(std::ostringstream& out, const NamedGraph& graph) {
    out << "(graph " << escapeSchemeString(graph.id) << "\n";
    for (const GraphNode& node : graph.nodes) {
        out << "  (node " << escapeSchemeString(node.id) << " (at " << vec3(node.at) << ")";
        if (!node.tags.empty()) {
            out << " (tags";
            for (const std::string& tag : node.tags) {
                out << " " << escapeSchemeString(tag);
            }
            out << ")";
        }
        out << ")\n";
    }
    for (const GraphEdge& edge : graph.edges) {
        out << "  (edge " << escapeSchemeString(edge.from) << " " << escapeSchemeString(edge.to);
        if (edge.cost != 1.0f) {
            out << " (cost " << formatFloat(edge.cost) << ")";
        }
        if (edge.bidir) {
            out << " (bidir #t)";
        }
        out << ")\n";
    }
    out << ")\n";
}

void writePath(std::ostringstream& out, const CameraPath& path) {
    out << "(path " << escapeSchemeString(path.id) << "\n";
    out << "  (interp \"" << pathInterpName(path.interp) << "\")\n";
    out << "  (orient \"" << pathOrientName(path.orient) << "\")\n";
    if (path.orient == PathOrient::LookAt) {
        out << "  (look-at " << vec3(path.lookAt) << ")\n";
    }
    if (path.closed) {
        out << "  (closed #t)\n";
    }
    if (path.constantSpeed) {
        out << "  (constant-speed #t)\n";
    }
    out << "  (duration " << formatFloat(path.duration) << ")\n";
    if (path.fov > 0.0f) {
        out << "  (fov " << formatFloat(path.fov) << ")\n";
    }
    if (path.ease != PathEase::Linear) {
        out << "  (ease \"" << pathEaseName(path.ease) << "\")\n";
    }
    for (const PathKnot& knot : path.knots) {
        out << "  (knot (at " << vec3(knot.at) << ") (look " << formatFloat(knot.yaw) << " "
            << formatFloat(knot.pitch) << ")";
        if (knot.fov > 0.0f) {
            out << " (fov " << formatFloat(knot.fov) << ")";
        }
        if (knot.duration > 0.0f) {
            out << " (duration " << formatFloat(knot.duration) << ")";
        }
        if (knot.haveEase) {
            out << " (ease \"" << pathEaseName(knot.ease) << "\")";
        }
        out << ")\n";
    }
    out << ")\n";
}

} // namespace

std::string formatMapGraphs(const GraphDocument& doc) {
    std::ostringstream out;
    bool first = true;
    for (const NamedGraph& graph : doc.graphs) {
        if (!first) {
            out << "\n";
        }
        first = false;
        writeGraph(out, graph);
    }
    for (const CameraPath& path : doc.paths) {
        if (!first) {
            out << "\n";
        }
        first = false;
        writePath(out, path);
    }
    return out.str();
}

bool writeMapGraphs(const std::filesystem::path& path, const GraphDocument& doc) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << formatMapGraphs(doc);
    return static_cast<bool>(out);
}

}
