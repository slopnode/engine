#include "sexpr.hpp"

#include <s7.h>

#include <cstdio>

namespace slopthing {

NodePtr makeNil() {
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::Nil;
    return n;
}

NodePtr makeSymbol(std::string name) {
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::Symbol;
    n->str = std::move(name);
    return n;
}

NodePtr makeString(std::string value) {
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::String;
    n->str = std::move(value);
    return n;
}

NodePtr makeInt(long long value) {
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::Int;
    n->intVal = value;
    return n;
}

NodePtr makeFloat(double value) {
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::Float;
    n->floatVal = value;
    return n;
}

NodePtr makeBool(bool value) {
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::Bool;
    n->boolVal = value;
    return n;
}

NodePtr makeCons(NodePtr car, NodePtr cdr) {
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::Pair;
    n->car = std::move(car);
    n->cdr = std::move(cdr);
    return n;
}

NodePtr makeList(const std::vector<NodePtr>& items) {
    NodePtr tail = makeNil();
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        tail = makeCons(*it, tail);
    }
    return tail;
}

NodePtr makeStringList(const std::vector<std::string>& items) {
    std::vector<NodePtr> nodes;
    nodes.reserve(items.size());
    for (const std::string& s : items) {
        nodes.push_back(makeString(s));
    }
    return makeList(nodes);
}

bool isNil(const NodePtr& n) {
    return !n || n->kind == NodeKind::Nil;
}

bool isPair(const NodePtr& n) {
    return n && n->kind == NodeKind::Pair;
}

std::vector<NodePtr> listItems(const NodePtr& n) {
    std::vector<NodePtr> items;
    NodePtr cur = n;
    while (isPair(cur)) {
        items.push_back(cur->car);
        cur = cur->cdr;
    }
    return items;
}

NodePtr deepClone(const NodePtr& n) {
    if (!n) {
        return nullptr;
    }
    auto copy = std::make_shared<Node>(*n);
    if (n->car) {
        copy->car = deepClone(n->car);
    }
    if (n->cdr) {
        copy->cdr = deepClone(n->cdr);
    }
    return copy;
}

NodePtr fromS7(s7_scheme* sc, s7_pointer value) {
    if (value == nullptr || s7_is_null(sc, value)) {
        return makeNil();
    }
    if (s7_is_pair(value)) {
        return makeCons(fromS7(sc, s7_car(value)), fromS7(sc, s7_cdr(value)));
    }
    if (s7_is_boolean(value)) {
        return makeBool(s7_boolean(sc, value));
    }
    if (s7_is_string(value)) {
        return makeString(s7_string(value));
    }
    if (s7_is_integer(value)) {
        return makeInt(s7_integer(value));
    }
    if (s7_is_number(value)) {
        return makeFloat(s7_number_to_real(sc, value));
    }
    if (s7_is_symbol(value)) {
        return makeSymbol(s7_symbol_name(value));
    }
    // Fallback: anything else (unreadback objects) becomes an opaque symbol.
    return makeSymbol("#<unsupported>");
}

NodePtr readDatum(s7_scheme* sc, const std::string& text) {
    s7_pointer port = s7_open_input_string(sc, text.c_str());
    s7_pointer datum = s7_read(sc, port);
    return fromS7(sc, datum);
}

namespace {

std::string quoteString(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    out += "\"";
    return out;
}

std::string formatFloat(double value) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", value);
    // Keep a decimal point so re-reading the file preserves this as a float,
    // not an integer (e.g. "1" -> 1.0, not 1).
    std::string s = buf;
    if (s.find_first_of(".eE") == std::string::npos) {
        s += ".0";
    }
    return s;
}

}

std::string writeInline(const NodePtr& n) {
    if (!n || n->kind == NodeKind::Nil) {
        return "()";
    }
    switch (n->kind) {
        case NodeKind::Bool:
            return n->boolVal ? "#t" : "#f";
        case NodeKind::Int:
            return std::to_string(n->intVal);
        case NodeKind::Float:
            return formatFloat(n->floatVal);
        case NodeKind::String:
            return quoteString(n->str);
        case NodeKind::Symbol:
            return n->str;
        case NodeKind::Pair: {
            std::string out = "(";
            NodePtr cur = n;
            bool first = true;
            while (isPair(cur)) {
                if (!first) {
                    out += " ";
                }
                out += writeInline(cur->car);
                first = false;
                cur = cur->cdr;
            }
            if (!isNil(cur)) {
                out += " . ";
                out += writeInline(cur);
            }
            out += ")";
            return out;
        }
        case NodeKind::Nil:
            return "()";
    }
    return "()";
}

// --- alist helpers -------------------------------------------------------

NodePtr alistFindPair(const NodePtr& alist, const std::string& key) {
    NodePtr cur = alist;
    while (isPair(cur)) {
        const NodePtr& entry = cur->car;
        if (isPair(entry) && entry->car &&
            (entry->car->kind == NodeKind::Symbol || entry->car->kind == NodeKind::String) &&
            entry->car->str == key) {
            return entry;
        }
        cur = cur->cdr;
    }
    return nullptr;
}

NodePtr scalarValue(const NodePtr& rest) {
    if (isPair(rest) && isNil(rest->cdr)) {
        return rest->car;
    }
    return rest;
}

NodePtr alistFindValue(const NodePtr& alist, const std::string& key) {
    NodePtr pair = alistFindPair(alist, key);
    if (!pair) {
        return nullptr;
    }
    return scalarValue(pair->cdr);
}

bool alistHasKey(const NodePtr& alist, const std::string& key) {
    return alistFindPair(alist, key) != nullptr;
}

void alistSetScalar(NodePtr& alist, const std::string& key, NodePtr value) {
    NodePtr existing = alistFindPair(alist, key);
    const bool listForm = existing && isPair(existing->cdr) && isNil(existing->cdr->cdr);
    NodePtr rest = listForm ? makeCons(value, makeNil()) : value;
    alistSetRest(alist, key, rest);
}

void alistSetRest(NodePtr& alist, const std::string& key, NodePtr rest) {
    NodePtr newPair = makeCons(makeSymbol(key), rest);
    if (isNil(alist)) {
        alist = makeCons(newPair, makeNil());
        return;
    }
    // Rebuild the list, replacing the matching entry in place, or appending.
    std::vector<NodePtr> items = listItems(alist);
    bool replaced = false;
    for (NodePtr& entry : items) {
        if (isPair(entry) && entry->car &&
            (entry->car->kind == NodeKind::Symbol || entry->car->kind == NodeKind::String) &&
            entry->car->str == key) {
            entry = newPair;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        items.push_back(newPair);
    }
    alist = makeList(items);
}

void alistRemoveKey(NodePtr& alist, const std::string& key) {
    if (isNil(alist)) {
        return;
    }
    std::vector<NodePtr> items = listItems(alist);
    std::vector<NodePtr> kept;
    kept.reserve(items.size());
    for (NodePtr& entry : items) {
        if (isPair(entry) && entry->car &&
            (entry->car->kind == NodeKind::Symbol || entry->car->kind == NodeKind::String) &&
            entry->car->str == key) {
            continue;
        }
        kept.push_back(entry);
    }
    alist = makeList(kept);
}

std::optional<std::string> getStr(const NodePtr& alist, const std::string& key) {
    NodePtr value = alistFindValue(alist, key);
    if (!value) {
        return std::nullopt;
    }
    if (value->kind == NodeKind::String || value->kind == NodeKind::Symbol) {
        return value->str;
    }
    return std::nullopt;
}

std::optional<double> getFloat(const NodePtr& alist, const std::string& key) {
    NodePtr value = alistFindValue(alist, key);
    if (!value) {
        return std::nullopt;
    }
    if (value->kind == NodeKind::Float) {
        return value->floatVal;
    }
    if (value->kind == NodeKind::Int) {
        return static_cast<double>(value->intVal);
    }
    return std::nullopt;
}

std::optional<long long> getInt(const NodePtr& alist, const std::string& key) {
    NodePtr value = alistFindValue(alist, key);
    if (!value) {
        return std::nullopt;
    }
    if (value->kind == NodeKind::Int) {
        return value->intVal;
    }
    if (value->kind == NodeKind::Float) {
        return static_cast<long long>(value->floatVal);
    }
    return std::nullopt;
}

std::optional<bool> getBool(const NodePtr& alist, const std::string& key) {
    NodePtr value = alistFindValue(alist, key);
    if (!value || value->kind != NodeKind::Bool) {
        return std::nullopt;
    }
    return value->boolVal;
}

std::vector<std::string> getStrList(const NodePtr& alist, const std::string& key) {
    std::vector<std::string> out;
    NodePtr pair = alistFindPair(alist, key);
    if (!pair) {
        return out;
    }
    for (const NodePtr& item : listItems(pair->cdr)) {
        if (item && item->kind == NodeKind::String) {
            out.push_back(item->str);
        }
    }
    return out;
}

void setStr(NodePtr& alist, const std::string& key, const std::string& value) {
    alistSetScalar(alist, key, makeString(value));
}

void setSymbol(NodePtr& alist, const std::string& key, const std::string& value) {
    alistSetScalar(alist, key, makeSymbol(value));
}

void setFloat(NodePtr& alist, const std::string& key, double value) {
    alistSetScalar(alist, key, makeFloat(value));
}

void setInt(NodePtr& alist, const std::string& key, long long value) {
    alistSetScalar(alist, key, makeInt(value));
}

void setBool(NodePtr& alist, const std::string& key, bool value) {
    alistSetScalar(alist, key, makeBool(value));
}

void setStrList(NodePtr& alist, const std::string& key, const std::vector<std::string>& values) {
    alistSetRest(alist, key, makeStringList(values));
}

}
