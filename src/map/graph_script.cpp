#include "map/graph_script.hpp"

#include "map/camera_path.hpp"
#include "script/script_scope.hpp"

#include <raylib.h>
#include <s7.h>

#include <cstring>
#include <string>
#include <unordered_set>

namespace slopengine {

namespace {

struct GraphLoadContext {
    GraphDocument* doc = nullptr;
    std::unordered_set<std::string> usedGraphIds;
    std::unordered_set<std::string> usedNodeIds;
    std::unordered_set<std::string> usedPathIds;
};

GraphLoadContext* g_context = nullptr;

s7_pointer makeTaggedList(s7_scheme* sc, const char* tag, s7_pointer rest) {
    return s7_cons(sc, s7_make_symbol(sc, tag), rest);
}

bool readString(s7_scheme* sc, s7_pointer value, std::string& out) {
    if (s7_is_string(value)) {
        out = s7_string(value);
        return true;
    }
    if (s7_is_symbol(value)) {
        out = s7_symbol_name(value);
        return true;
    }
    (void)sc;
    return false;
}

bool readVec3(s7_scheme* sc, s7_pointer x, s7_pointer y, s7_pointer z, Vector3& out) {
    if (!s7_is_number(x) || !s7_is_number(y) || !s7_is_number(z)) {
        return false;
    }
    out.x = static_cast<float>(s7_number_to_real(sc, x));
    out.y = static_cast<float>(s7_number_to_real(sc, y));
    out.z = static_cast<float>(s7_number_to_real(sc, z));
    return true;
}

s7_pointer g_at(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args)) || !s7_is_pair(s7_cddr(args))) {
        return s7_wrong_type_arg_error(sc, "at", 0, args, "x y z");
    }
    return makeTaggedList(sc, "at", s7_list(sc, 3, s7_car(args), s7_cadr(args), s7_caddr(args)));
}

s7_pointer g_tags(s7_scheme* sc, s7_pointer args) {
    return makeTaggedList(sc, "tags", args);
}

s7_pointer g_cost(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "cost", 1, args, "number");
    }
    return makeTaggedList(sc, "cost", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_bidir(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "bidir", 1, args, "boolean");
    }
    return makeTaggedList(sc, "bidir", s7_cons(sc, s7_car(args), s7_nil(sc)));
}

void applyNodeClauses(s7_scheme* sc, s7_pointer clauses, GraphNode& node) {
    for (s7_pointer cursor = clauses; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer clause = s7_car(cursor);
        if (!s7_is_pair(clause) || !s7_is_symbol(s7_car(clause))) {
            continue;
        }
        const char* tag = s7_symbol_name(s7_car(clause));
        s7_pointer rest = s7_cdr(clause);
        if (std::strcmp(tag, "at") == 0 &&
            s7_is_pair(rest) &&
            s7_is_pair(s7_cdr(rest)) &&
            s7_is_pair(s7_cddr(rest))) {
            node.haveAt = readVec3(sc, s7_car(rest), s7_cadr(rest), s7_caddr(rest), node.at);
        } else if (std::strcmp(tag, "tags") == 0) {
            for (s7_pointer tagCursor = rest; s7_is_pair(tagCursor); tagCursor = s7_cdr(tagCursor)) {
                std::string tagValue;
                if (readString(sc, s7_car(tagCursor), tagValue) && !tagValue.empty()) {
                    node.tags.push_back(std::move(tagValue));
                }
            }
        }
    }
}

void applyEdgeClauses(s7_scheme* sc, s7_pointer clauses, GraphEdge& edge) {
    for (s7_pointer cursor = clauses; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer clause = s7_car(cursor);
        if (!s7_is_pair(clause) || !s7_is_symbol(s7_car(clause))) {
            continue;
        }
        const char* tag = s7_symbol_name(s7_car(clause));
        s7_pointer rest = s7_cdr(clause);
        if (std::strcmp(tag, "cost") == 0 && s7_is_pair(rest) && s7_is_number(s7_car(rest))) {
            edge.cost = static_cast<float>(s7_number_to_real(sc, s7_car(rest)));
        } else if (std::strcmp(tag, "bidir") == 0 && s7_is_pair(rest)) {
            s7_pointer value = s7_car(rest);
            if (s7_is_boolean(value)) {
                edge.bidir = s7_boolean(sc, value);
            } else if (s7_is_integer(value)) {
                edge.bidir = s7_integer(value) != 0;
            }
        }
    }
}

s7_pointer g_node(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "node", 1, args, "id");
    }

    GraphNode node{};
    if (!readString(sc, s7_car(args), node.id) || node.id.empty()) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "graph-error"),
            s7_list(sc, 1, s7_make_string(sc, "node requires id")));
    }

    applyNodeClauses(sc, s7_cdr(args), node);
    if (!node.haveAt) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "graph-error"),
            s7_list(sc, 1, s7_make_string(sc, "node requires at")));
    }

    s7_pointer tags = s7_nil(sc);
    for (auto it = node.tags.rbegin(); it != node.tags.rend(); ++it) {
        tags = s7_cons(sc, s7_make_string(sc, it->c_str()), tags);
    }

    return makeTaggedList(
        sc,
        "node",
        s7_list(
            sc,
            5,
            s7_make_string(sc, node.id.c_str()),
            s7_make_real(sc, static_cast<double>(node.at.x)),
            s7_make_real(sc, static_cast<double>(node.at.y)),
            s7_make_real(sc, static_cast<double>(node.at.z)),
            tags));
}

s7_pointer g_edge(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args))) {
        return s7_wrong_type_arg_error(sc, "edge", 0, args, "from to");
    }

    GraphEdge edge{};
    if (!readString(sc, s7_car(args), edge.from) || edge.from.empty() ||
        !readString(sc, s7_cadr(args), edge.to) || edge.to.empty()) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "graph-error"),
            s7_list(sc, 1, s7_make_string(sc, "edge requires from and to")));
    }

    applyEdgeClauses(sc, s7_cddr(args), edge);

    return makeTaggedList(
        sc,
        "edge",
        s7_list(
            sc,
            4,
            s7_make_string(sc, edge.from.c_str()),
            s7_make_string(sc, edge.to.c_str()),
            s7_make_real(sc, static_cast<double>(edge.cost)),
            s7_make_boolean(sc, edge.bidir)));
}

bool appendNodeFromClause(s7_scheme* sc, s7_pointer clause, NamedGraph& graph) {
    if (!s7_is_pair(clause) || !s7_is_symbol(s7_car(clause))) {
        return false;
    }
    if (std::strcmp(s7_symbol_name(s7_car(clause)), "node") != 0) {
        return false;
    }

    s7_pointer rest = s7_cdr(clause);
    if (!s7_is_pair(rest) || !s7_is_pair(s7_cdr(rest)) || !s7_is_pair(s7_cddr(rest)) ||
        !s7_is_pair(s7_cdddr(rest))) {
        return false;
    }

    GraphNode node{};
    if (!readString(sc, s7_car(rest), node.id) || node.id.empty()) {
        return false;
    }
    if (!readVec3(sc, s7_cadr(rest), s7_caddr(rest), s7_car(s7_cdddr(rest)), node.at)) {
        return false;
    }
    node.haveAt = true;

    s7_pointer tagsCell = s7_cdr(s7_cdddr(rest));
    if (s7_is_pair(tagsCell)) {
        for (s7_pointer tagCursor = s7_car(tagsCell); s7_is_pair(tagCursor); tagCursor = s7_cdr(tagCursor)) {
            std::string tagValue;
            if (readString(sc, s7_car(tagCursor), tagValue) && !tagValue.empty()) {
                node.tags.push_back(std::move(tagValue));
            }
        }
    }

    if (g_context != nullptr && !g_context->usedNodeIds.insert(graph.id + "/" + node.id).second) {
        TraceLog(
            LOG_WARNING,
            "GRAPH: duplicate node id '%s' in graph '%s'",
            node.id.c_str(),
            graph.id.c_str());
        return false;
    }

    graph.nodes.push_back(std::move(node));
    return true;
}

bool appendEdgeFromClause(s7_scheme* sc, s7_pointer clause, NamedGraph& graph) {
    if (!s7_is_pair(clause) || !s7_is_symbol(s7_car(clause))) {
        return false;
    }
    if (std::strcmp(s7_symbol_name(s7_car(clause)), "edge") != 0) {
        return false;
    }

    s7_pointer rest = s7_cdr(clause);
    if (!s7_is_pair(rest) || !s7_is_pair(s7_cdr(rest)) || !s7_is_pair(s7_cddr(rest)) ||
        !s7_is_pair(s7_cdddr(rest))) {
        return false;
    }

    GraphEdge edge{};
    if (!readString(sc, s7_car(rest), edge.from) || edge.from.empty() ||
        !readString(sc, s7_cadr(rest), edge.to) || edge.to.empty()) {
        return false;
    }

    if (s7_is_number(s7_caddr(rest))) {
        edge.cost = static_cast<float>(s7_number_to_real(sc, s7_caddr(rest)));
    }
    s7_pointer bidirVal = s7_car(s7_cdddr(rest));
    if (s7_is_boolean(bidirVal)) {
        edge.bidir = s7_boolean(sc, bidirVal);
    }

    graph.edges.push_back(std::move(edge));
    return true;
}

s7_pointer g_graph(s7_scheme* sc, s7_pointer args) {
    if (g_context == nullptr || g_context->doc == nullptr) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "graph-error"),
            s7_list(sc, 1, s7_make_string(sc, "graph called outside graph load")));
    }

    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "graph", 1, args, "id");
    }

    NamedGraph graph{};
    if (!readString(sc, s7_car(args), graph.id) || graph.id.empty()) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "graph-error"),
            s7_list(sc, 1, s7_make_string(sc, "graph requires id")));
    }

    if (!g_context->usedGraphIds.insert(graph.id).second) {
        TraceLog(LOG_WARNING, "GRAPH: duplicate graph id '%s'", graph.id.c_str());
        return s7_f(sc);
    }

    for (s7_pointer cursor = s7_cdr(args); s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer clause = s7_car(cursor);
        if (!appendNodeFromClause(sc, clause, graph) && !appendEdgeFromClause(sc, clause, graph)) {
            TraceLog(LOG_WARNING, "GRAPH: ignoring clause in graph '%s'", graph.id.c_str());
        }
    }

    g_context->doc->graphs.push_back(std::move(graph));
    return s7_t(sc);
}

s7_pointer makeNumberClause(s7_scheme* sc, const char* tag, const char* name, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_number(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, name, 1, args, "number");
    }
    return makeTaggedList(sc, tag, s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer makeSymbolClause(s7_scheme* sc, const char* tag, const char* name, s7_pointer args) {
    if (!s7_is_pair(args) || (!s7_is_symbol(s7_car(args)) && !s7_is_string(s7_car(args)))) {
        return s7_wrong_type_arg_error(sc, name, 1, args, "symbol");
    }
    return makeTaggedList(sc, tag, s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer makeBoolClause(s7_scheme* sc, const char* tag, const char* name, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_boolean(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, name, 1, args, "boolean");
    }
    return makeTaggedList(sc, tag, s7_cons(sc, s7_car(args), s7_nil(sc)));
}

s7_pointer g_look(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args))) {
        return s7_wrong_type_arg_error(sc, "look", 0, args, "yaw pitch");
    }
    return makeTaggedList(sc, "look", s7_list(sc, 2, s7_car(args), s7_cadr(args)));
}

s7_pointer g_look_at(s7_scheme* sc, s7_pointer args) {
    if (!s7_is_pair(args) || !s7_is_pair(s7_cdr(args)) || !s7_is_pair(s7_cddr(args))) {
        return s7_wrong_type_arg_error(sc, "look-at", 0, args, "x y z");
    }
    return makeTaggedList(sc, "look-at", s7_list(sc, 3, s7_car(args), s7_cadr(args), s7_caddr(args)));
}

s7_pointer g_fov(s7_scheme* sc, s7_pointer args) {
    return makeNumberClause(sc, "fov", "fov", args);
}

s7_pointer g_duration(s7_scheme* sc, s7_pointer args) {
    return makeNumberClause(sc, "duration", "duration", args);
}

s7_pointer g_ease(s7_scheme* sc, s7_pointer args) {
    return makeSymbolClause(sc, "ease", "ease", args);
}

s7_pointer g_interp(s7_scheme* sc, s7_pointer args) {
    return makeSymbolClause(sc, "interp", "interp", args);
}

s7_pointer g_orient(s7_scheme* sc, s7_pointer args) {
    return makeSymbolClause(sc, "orient", "orient", args);
}

s7_pointer g_closed(s7_scheme* sc, s7_pointer args) {
    return makeBoolClause(sc, "closed", "closed", args);
}

s7_pointer g_constant_speed(s7_scheme* sc, s7_pointer args) {
    return makeBoolClause(sc, "constant-speed", "constant-speed", args);
}

bool readNumber(s7_scheme* sc, s7_pointer value, float& out) {
    if (!s7_is_number(value)) {
        return false;
    }
    out = static_cast<float>(s7_number_to_real(sc, value));
    return true;
}

bool readBool(s7_scheme* sc, s7_pointer value, bool& out) {
    if (s7_is_boolean(value)) {
        out = s7_boolean(sc, value);
        return true;
    }
    if (s7_is_integer(value)) {
        out = s7_integer(value) != 0;
        return true;
    }
    return false;
}

s7_pointer g_knot(s7_scheme* sc, s7_pointer args) {
    PathKnot knot{};
    bool haveAt = false;
    for (s7_pointer cursor = args; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer clause = s7_car(cursor);
        if (!s7_is_pair(clause) || !s7_is_symbol(s7_car(clause))) {
            continue;
        }
        const char* tag = s7_symbol_name(s7_car(clause));
        s7_pointer rest = s7_cdr(clause);
        if (!s7_is_pair(rest)) {
            continue;
        }
        if (std::strcmp(tag, "at") == 0 && s7_is_pair(s7_cdr(rest)) && s7_is_pair(s7_cddr(rest))) {
            haveAt = readVec3(sc, s7_car(rest), s7_cadr(rest), s7_caddr(rest), knot.at);
        } else if (std::strcmp(tag, "look") == 0 && s7_is_pair(s7_cdr(rest))) {
            readNumber(sc, s7_car(rest), knot.yaw);
            readNumber(sc, s7_cadr(rest), knot.pitch);
        } else if (std::strcmp(tag, "fov") == 0) {
            readNumber(sc, s7_car(rest), knot.fov);
        } else if (std::strcmp(tag, "duration") == 0) {
            readNumber(sc, s7_car(rest), knot.duration);
        } else if (std::strcmp(tag, "ease") == 0) {
            std::string name;
            if (readString(sc, s7_car(rest), name)) {
                knot.haveEase = parsePathEase(name.c_str(), knot.ease);
                if (!knot.haveEase) {
                    TraceLog(LOG_WARNING, "GRAPH: unknown ease '%s'", name.c_str());
                }
            }
        }
    }
    if (!haveAt) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "graph-error"),
            s7_list(sc, 1, s7_make_string(sc, "knot requires at")));
    }
    return makeTaggedList(
        sc,
        "knot",
        s7_list(
            sc,
            8,
            s7_make_real(sc, static_cast<double>(knot.at.x)),
            s7_make_real(sc, static_cast<double>(knot.at.y)),
            s7_make_real(sc, static_cast<double>(knot.at.z)),
            s7_make_real(sc, static_cast<double>(knot.yaw)),
            s7_make_real(sc, static_cast<double>(knot.pitch)),
            s7_make_real(sc, static_cast<double>(knot.fov)),
            s7_make_real(sc, static_cast<double>(knot.duration)),
            knot.haveEase ? s7_make_symbol(sc, pathEaseName(knot.ease)) : s7_f(sc)));
}

bool appendKnotFromClause(s7_scheme* sc, s7_pointer clause, CameraPath& path) {
    if (!s7_is_pair(clause) || !s7_is_symbol(s7_car(clause))) {
        return false;
    }
    if (std::strcmp(s7_symbol_name(s7_car(clause)), "knot") != 0) {
        return false;
    }
    s7_pointer rest = s7_cdr(clause);
    float values[7];
    for (float& value : values) {
        if (!s7_is_pair(rest) || !readNumber(sc, s7_car(rest), value)) {
            return false;
        }
        rest = s7_cdr(rest);
    }
    PathKnot knot{};
    knot.at = {values[0], values[1], values[2]};
    knot.yaw = values[3];
    knot.pitch = values[4];
    knot.fov = values[5];
    knot.duration = values[6];
    if (s7_is_pair(rest)) {
        std::string name;
        if (readString(sc, s7_car(rest), name)) {
            knot.haveEase = parsePathEase(name.c_str(), knot.ease);
        }
    }
    path.knots.push_back(knot);
    return true;
}

bool applyPathOptionClause(s7_scheme* sc, s7_pointer clause, CameraPath& path) {
    if (!s7_is_pair(clause) || !s7_is_symbol(s7_car(clause))) {
        return false;
    }
    const char* tag = s7_symbol_name(s7_car(clause));
    s7_pointer rest = s7_cdr(clause);
    if (!s7_is_pair(rest)) {
        return false;
    }
    std::string name;
    if (std::strcmp(tag, "interp") == 0) {
        if (!readString(sc, s7_car(rest), name) || !parsePathInterp(name.c_str(), path.interp)) {
            TraceLog(LOG_WARNING, "GRAPH: unknown interp in path '%s'", path.id.c_str());
        }
        return true;
    }
    if (std::strcmp(tag, "orient") == 0) {
        if (!readString(sc, s7_car(rest), name) || !parsePathOrient(name.c_str(), path.orient)) {
            TraceLog(LOG_WARNING, "GRAPH: unknown orient in path '%s'", path.id.c_str());
        }
        return true;
    }
    if (std::strcmp(tag, "ease") == 0) {
        if (!readString(sc, s7_car(rest), name) || !parsePathEase(name.c_str(), path.ease)) {
            TraceLog(LOG_WARNING, "GRAPH: unknown ease in path '%s'", path.id.c_str());
        }
        return true;
    }
    if (std::strcmp(tag, "look-at") == 0 && s7_is_pair(s7_cdr(rest)) && s7_is_pair(s7_cddr(rest))) {
        readVec3(sc, s7_car(rest), s7_cadr(rest), s7_caddr(rest), path.lookAt);
        return true;
    }
    if (std::strcmp(tag, "closed") == 0) {
        return readBool(sc, s7_car(rest), path.closed);
    }
    if (std::strcmp(tag, "constant-speed") == 0) {
        return readBool(sc, s7_car(rest), path.constantSpeed);
    }
    if (std::strcmp(tag, "duration") == 0) {
        return readNumber(sc, s7_car(rest), path.duration);
    }
    if (std::strcmp(tag, "fov") == 0) {
        return readNumber(sc, s7_car(rest), path.fov);
    }
    return false;
}

s7_pointer g_path(s7_scheme* sc, s7_pointer args) {
    if (g_context == nullptr || g_context->doc == nullptr) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "graph-error"),
            s7_list(sc, 1, s7_make_string(sc, "path called outside graph load")));
    }
    if (!s7_is_pair(args)) {
        return s7_wrong_type_arg_error(sc, "path", 1, args, "id");
    }

    CameraPath path{};
    if (!readString(sc, s7_car(args), path.id) || path.id.empty()) {
        return s7_error(
            sc,
            s7_make_symbol(sc, "graph-error"),
            s7_list(sc, 1, s7_make_string(sc, "path requires id")));
    }
    if (!g_context->usedPathIds.insert(path.id).second) {
        TraceLog(LOG_WARNING, "GRAPH: duplicate path id '%s'", path.id.c_str());
        return s7_f(sc);
    }

    for (s7_pointer cursor = s7_cdr(args); s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer clause = s7_car(cursor);
        if (!appendKnotFromClause(sc, clause, path) && !applyPathOptionClause(sc, clause, path)) {
            TraceLog(LOG_WARNING, "GRAPH: ignoring clause in path '%s'", path.id.c_str());
        }
    }

    g_context->doc->paths.push_back(std::move(path));
    return s7_t(sc);
}

void bindGraphApi(s7_scheme* sc) {
    s7_define_function(sc, "at", g_at, 3, 0, false, "(at x y z)");
    s7_define_function(sc, "tags", g_tags, 0, 0, true, "(tags values...)");
    s7_define_function(sc, "cost", g_cost, 1, 0, false, "(cost value)");
    s7_define_function(sc, "bidir", g_bidir, 1, 0, false, "(bidir boolean)");
    s7_define_function(sc, "node", g_node, 1, 0, true, "(node id clauses...)");
    s7_define_function(sc, "edge", g_edge, 2, 0, true, "(edge from to clauses...)");
    s7_define_function(sc, "graph", g_graph, 1, 0, true, "(graph id clauses...)");
    s7_define_function(sc, "look", g_look, 2, 0, false, "(look yaw pitch)");
    s7_define_function(sc, "look-at", g_look_at, 3, 0, false, "(look-at x y z)");
    s7_define_function(sc, "fov", g_fov, 1, 0, false, "(fov degrees)");
    s7_define_function(sc, "duration", g_duration, 1, 0, false, "(duration seconds)");
    s7_define_function(sc, "ease", g_ease, 1, 0, false, "(ease linear|smooth|in|out)");
    s7_define_function(sc, "interp", g_interp, 1, 0, false, "(interp linear|catmull-rom)");
    s7_define_function(sc, "orient", g_orient, 1, 0, false, "(orient knots|tangent|look-at)");
    s7_define_function(sc, "closed", g_closed, 1, 0, false, "(closed boolean)");
    s7_define_function(
        sc, "constant-speed", g_constant_speed, 1, 0, false, "(constant-speed boolean)");
    s7_define_function(sc, "knot", g_knot, 0, 0, true, "(knot clauses...)");
    s7_define_function(sc, "path", g_path, 1, 0, true, "(path id clauses...)");
}

} // namespace

std::optional<GraphDocument> loadMapGraphs(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName) {
    if (scheme == nullptr) {
        return std::nullopt;
    }

    const std::string virtualPath = std::string(mapName) + "/graphs";
    if (!assets.hasMapGraphs(virtualPath)) {
        return GraphDocument{};
    }

    ScriptScopeGuard scopeGuard(ScriptScope::MapAuthor);
    const s7_pointer env = s7_sublet(scheme, s7_rootlet(scheme), s7_nil(scheme));
    const s7_pointer previousEnv = s7_set_curlet(scheme, env);

    GraphDocument doc{};
    GraphLoadContext context{};
    context.doc = &doc;
    g_context = &context;
    bindGraphApi(scheme);

    const bool loaded = assets.loadMapGraphs(scheme, virtualPath, env);
    g_context = nullptr;
    s7_set_curlet(scheme, previousEnv);

    if (!loaded) {
        TraceLog(
            LOG_WARNING,
            "GRAPH: failed to evaluate graphs.s7 for map '%.*s'",
            static_cast<int>(mapName.size()),
            mapName.data());
        return std::nullopt;
    }

    return doc;
}

}
