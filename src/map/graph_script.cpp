#include "map/graph_script.hpp"

#include "map/camera_path.hpp"
#include "script/s7_read.hpp"
#include "script/script_scope.hpp"

#include <raylib.h>
#include <s7.h>

#include <cstring>
#include <initializer_list>
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

s7_pointer graphError(s7_scheme* sc, const char* message) {
    return s7_error(
        sc,
        s7_make_symbol(sc, "graph-error"),
        s7_list(sc, 1, s7_make_string(sc, message)));
}

bool hasThree(s7_pointer list) {
    return s7_is_pair(list) && s7_is_pair(s7_cdr(list)) && s7_is_pair(s7_cddr(list));
}

bool readRealRun(ArgReader& in, std::initializer_list<float*> outs) {
    for (float* out : outs) {
        if (!in.nextIsNumber()) {
            return false;
        }
        *out = in.optRealf(0.0f);
    }
    return true;
}

void appendTags(s7_pointer values, std::vector<std::string>& tags) {
    for (s7_pointer cursor = values; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        std::string tag;
        if (readName(s7_car(cursor), tag) && !tag.empty()) {
            tags.push_back(std::move(tag));
        }
    }
}

constexpr ClauseSpec kGraphClauses[] = {
    {"at", 3, 0, false, "(at x y z)"},
    {"tags", 0, 0, true, "(tags values...)"},
    {"cost", 1, 0, false, "(cost value)"},
    {"bidir", 1, 0, false, "(bidir boolean)"},
    {"look", 2, 0, false, "(look yaw pitch)"},
    {"look-at", 3, 0, false, "(look-at x y z)"},
    {"fov", 1, 0, false, "(fov degrees)", ClauseArg::Number},
    {"duration", 1, 0, false, "(duration seconds)", ClauseArg::Number},
    {"ease", 1, 0, false, "(ease linear|smooth|in|out)", ClauseArg::Name},
    {"interp", 1, 0, false, "(interp linear|catmull-rom)", ClauseArg::Name},
    {"orient", 1, 0, false, "(orient knots|tangent|look-at)", ClauseArg::Name},
    {"closed", 1, 0, false, "(closed boolean)", ClauseArg::Boolean},
    {"constant-speed", 1, 0, false, "(constant-speed boolean)", ClauseArg::Boolean},
};

void applyNodeClauses(s7_scheme* sc, s7_pointer clauses, GraphNode& node) {
    for (s7_pointer cursor = clauses; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer clause = s7_car(cursor);
        if (!s7_is_pair(clause) || !s7_is_symbol(s7_car(clause))) {
            continue;
        }
        const char* tag = s7_symbol_name(s7_car(clause));
        s7_pointer rest = s7_cdr(clause);
        if (std::strcmp(tag, "at") == 0 && hasThree(rest)) {
            node.haveAt = readVec3List(sc, rest, node.at);
        } else if (std::strcmp(tag, "tags") == 0) {
            appendTags(rest, node.tags);
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
        ArgReader in(sc, tag, s7_cdr(clause));
        if (std::strcmp(tag, "cost") == 0) {
            edge.cost = in.optRealf(edge.cost);
        } else if (std::strcmp(tag, "bidir") == 0) {
            edge.bidir = in.optFlag(edge.bidir);
        }
    }
}

s7_pointer g_node(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "node", args);
    GraphNode node{};
    if (!readName(in.any("id"), node.id) || node.id.empty()) {
        return graphError(sc, "node requires id");
    }

    applyNodeClauses(sc, in.rest(), node);
    if (!node.haveAt) {
        return graphError(sc, "node requires at");
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
            makeStringList(sc, node.tags)));
}

s7_pointer g_edge(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "edge", args);
    GraphEdge edge{};
    const bool haveFrom = readName(in.any("from to"), edge.from);
    const bool haveTo = readName(in.any("from to"), edge.to);
    if (!haveFrom || edge.from.empty() || !haveTo || edge.to.empty()) {
        return graphError(sc, "edge requires from and to");
    }

    applyEdgeClauses(sc, in.rest(), edge);

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

bool isClauseTagged(s7_pointer clause, const char* tag) {
    return s7_is_pair(clause) && s7_is_symbol(s7_car(clause)) &&
           std::strcmp(s7_symbol_name(s7_car(clause)), tag) == 0;
}

bool appendNodeFromClause(s7_scheme* sc, s7_pointer clause, NamedGraph& graph) {
    if (!isClauseTagged(clause, "node")) {
        return false;
    }

    ArgReader in(sc, "node", s7_cdr(clause));
    GraphNode node{};
    const char* id = in.optName();
    if (id == nullptr || *id == '\0') {
        return false;
    }
    node.id = id;
    if (!readRealRun(in, {&node.at.x, &node.at.y, &node.at.z})) {
        return false;
    }
    node.haveAt = true;
    appendTags(in.optAny(s7_nil(sc)), node.tags);

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
    if (!isClauseTagged(clause, "edge")) {
        return false;
    }

    ArgReader in(sc, "edge", s7_cdr(clause));
    GraphEdge edge{};
    const char* from = in.optName();
    const char* to = in.optName();
    s7_pointer costValue = in.optAny();
    s7_pointer bidirValue = in.optAny();
    if (from == nullptr || *from == '\0' || to == nullptr || *to == '\0' ||
        costValue == nullptr || bidirValue == nullptr) {
        return false;
    }
    edge.from = from;
    edge.to = to;
    readReal(sc, costValue, edge.cost);
    if (s7_is_boolean(bidirValue)) {
        edge.bidir = s7_boolean(sc, bidirValue);
    }

    graph.edges.push_back(std::move(edge));
    return true;
}

s7_pointer g_graph(s7_scheme* sc, s7_pointer args) {
    if (g_context == nullptr || g_context->doc == nullptr) {
        return graphError(sc, "graph called outside graph load");
    }

    ArgReader in(sc, "graph", args);
    NamedGraph graph{};
    if (!readName(in.any("id"), graph.id) || graph.id.empty()) {
        return graphError(sc, "graph requires id");
    }

    if (!g_context->usedGraphIds.insert(graph.id).second) {
        TraceLog(LOG_WARNING, "GRAPH: duplicate graph id '%s'", graph.id.c_str());
        return s7_f(sc);
    }

    for (s7_pointer cursor = in.rest(); s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer clause = s7_car(cursor);
        if (!appendNodeFromClause(sc, clause, graph) && !appendEdgeFromClause(sc, clause, graph)) {
            TraceLog(LOG_WARNING, "GRAPH: ignoring clause in graph '%s'", graph.id.c_str());
        }
    }

    g_context->doc->graphs.push_back(std::move(graph));
    return s7_t(sc);
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
        if (std::strcmp(tag, "at") == 0 && hasThree(rest)) {
            haveAt = readVec3List(sc, rest, knot.at);
        } else if (std::strcmp(tag, "look") == 0 && s7_is_pair(s7_cdr(rest))) {
            readReal(sc, s7_car(rest), knot.yaw);
            readReal(sc, s7_cadr(rest), knot.pitch);
        } else if (std::strcmp(tag, "fov") == 0) {
            readReal(sc, s7_car(rest), knot.fov);
        } else if (std::strcmp(tag, "duration") == 0) {
            readReal(sc, s7_car(rest), knot.duration);
        } else if (std::strcmp(tag, "ease") == 0) {
            std::string name;
            if (readName(s7_car(rest), name)) {
                knot.haveEase = parsePathEase(name.c_str(), knot.ease);
                if (!knot.haveEase) {
                    TraceLog(LOG_WARNING, "GRAPH: unknown ease '%s'", name.c_str());
                }
            }
        }
    }
    if (!haveAt) {
        return graphError(sc, "knot requires at");
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
    if (!isClauseTagged(clause, "knot")) {
        return false;
    }
    ArgReader in(sc, "knot", s7_cdr(clause));
    PathKnot knot{};
    if (!readRealRun(
            in,
            {&knot.at.x,
             &knot.at.y,
             &knot.at.z,
             &knot.yaw,
             &knot.pitch,
             &knot.fov,
             &knot.duration})) {
        return false;
    }
    const char* ease = in.optName();
    if (ease != nullptr) {
        knot.haveEase = parsePathEase(ease, knot.ease);
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
    s7_pointer value = s7_car(rest);
    std::string name;
    if (std::strcmp(tag, "interp") == 0) {
        if (!readName(value, name) || !parsePathInterp(name.c_str(), path.interp)) {
            TraceLog(LOG_WARNING, "GRAPH: unknown interp in path '%s'", path.id.c_str());
        }
        return true;
    }
    if (std::strcmp(tag, "orient") == 0) {
        if (!readName(value, name) || !parsePathOrient(name.c_str(), path.orient)) {
            TraceLog(LOG_WARNING, "GRAPH: unknown orient in path '%s'", path.id.c_str());
        }
        return true;
    }
    if (std::strcmp(tag, "ease") == 0) {
        if (!readName(value, name) || !parsePathEase(name.c_str(), path.ease)) {
            TraceLog(LOG_WARNING, "GRAPH: unknown ease in path '%s'", path.id.c_str());
        }
        return true;
    }
    if (std::strcmp(tag, "look-at") == 0 && hasThree(rest)) {
        readVec3List(sc, rest, path.lookAt);
        return true;
    }
    if (std::strcmp(tag, "closed") == 0) {
        return readFlag(sc, value, path.closed);
    }
    if (std::strcmp(tag, "constant-speed") == 0) {
        return readFlag(sc, value, path.constantSpeed);
    }
    if (std::strcmp(tag, "duration") == 0) {
        return readReal(sc, value, path.duration);
    }
    if (std::strcmp(tag, "fov") == 0) {
        return readReal(sc, value, path.fov);
    }
    return false;
}

s7_pointer g_path(s7_scheme* sc, s7_pointer args) {
    if (g_context == nullptr || g_context->doc == nullptr) {
        return graphError(sc, "path called outside graph load");
    }

    ArgReader in(sc, "path", args);
    CameraPath path{};
    if (!readName(in.any("id"), path.id) || path.id.empty()) {
        return graphError(sc, "path requires id");
    }
    if (!g_context->usedPathIds.insert(path.id).second) {
        TraceLog(LOG_WARNING, "GRAPH: duplicate path id '%s'", path.id.c_str());
        return s7_f(sc);
    }

    for (s7_pointer cursor = in.rest(); s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer clause = s7_car(cursor);
        if (!appendKnotFromClause(sc, clause, path) && !applyPathOptionClause(sc, clause, path)) {
            TraceLog(LOG_WARNING, "GRAPH: ignoring clause in path '%s'", path.id.c_str());
        }
    }

    g_context->doc->paths.push_back(std::move(path));
    return s7_t(sc);
}

constexpr SchemeBinding kGraphBindings[] = {
    {"node", g_node, 1, 0, true, "(node id clauses...)"},
    {"edge", g_edge, 2, 0, true, "(edge from to clauses...)"},
    {"graph", g_graph, 1, 0, true, "(graph id clauses...)"},
    {"knot", g_knot, 0, 0, true, "(knot clauses...)"},
    {"path", g_path, 1, 0, true, "(path id clauses...)"},
};

void bindGraphApi(s7_scheme* sc) {
    registerClauseBuilders(sc, kGraphClauses);
    registerBindings(sc, kGraphBindings);
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
