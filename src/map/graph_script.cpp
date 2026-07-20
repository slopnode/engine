#include "map/graph_script.hpp"

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

void bindGraphApi(s7_scheme* sc) {
    s7_define_function(sc, "at", g_at, 3, 0, false, "(at x y z)");
    s7_define_function(sc, "tags", g_tags, 0, 0, true, "(tags values...)");
    s7_define_function(sc, "cost", g_cost, 1, 0, false, "(cost value)");
    s7_define_function(sc, "bidir", g_bidir, 1, 0, false, "(bidir boolean)");
    s7_define_function(sc, "node", g_node, 1, 0, true, "(node id clauses...)");
    s7_define_function(sc, "edge", g_edge, 2, 0, true, "(edge from to clauses...)");
    s7_define_function(sc, "graph", g_graph, 1, 0, true, "(graph id clauses...)");
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

    GraphDocument doc{};
    GraphLoadContext context{};
    context.doc = &doc;
    g_context = &context;
    bindGraphApi(scheme);

    const bool loaded = assets.loadMapGraphs(scheme, virtualPath);
    g_context = nullptr;

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
