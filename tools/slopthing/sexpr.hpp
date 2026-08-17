#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

struct s7_scheme;
struct s7_cell;
using s7_pointer = s7_cell*;

namespace slopthing {

enum class NodeKind {
    Nil,
    Pair,
    Symbol,
    String,
    Int,
    Float,
    Bool,
};

struct Node;
using NodePtr = std::shared_ptr<Node>;

struct Node {
    NodeKind kind = NodeKind::Nil;
    std::string str;
    long long intVal = 0;
    double floatVal = 0.0;
    bool boolVal = false;
    NodePtr car;
    NodePtr cdr;
};

NodePtr makeNil();
NodePtr makeSymbol(std::string name);
NodePtr makeString(std::string value);
NodePtr makeInt(long long value);
NodePtr makeFloat(double value);
NodePtr makeBool(bool value);
NodePtr makeCons(NodePtr car, NodePtr cdr);
NodePtr makeList(const std::vector<NodePtr>& items);
NodePtr makeStringList(const std::vector<std::string>& items);

bool isNil(const NodePtr& n);
bool isPair(const NodePtr& n);

/** Walks a (possibly improper) list, collecting elements until a non-pair cdr is hit. */
std::vector<NodePtr> listItems(const NodePtr& n);

NodePtr deepClone(const NodePtr& n);

/** Converts an already-evaluated s7 data value into a detached Node tree. */
NodePtr fromS7(s7_scheme* sc, s7_pointer value);

/** Parses one datum (pure data, not evaluated) from text, e.g. a raw alist body. */
NodePtr readDatum(s7_scheme* sc, const std::string& text);

/** Renders a node as a single-line s-expression. */
std::string writeInline(const NodePtr& n);

// --- alist helpers -------------------------------------------------------
// An alist is a proper list of pair nodes shaped either `(key . value)` or
// `(key value...)`; both forms appear in things.s7.

/** Unwraps a single-item list `(x)` down to `x`; leaves dotted/multi-item values untouched. */
NodePtr scalarValue(const NodePtr& rest);

/** Finds the pair node `(key . rest)` for @p key, or nullptr. */
NodePtr alistFindPair(const NodePtr& alist, const std::string& key);

/** Returns the raw "rest" (unwrapped one level if it's a single-item list) for @p key. */
NodePtr alistFindValue(const NodePtr& alist, const std::string& key);

bool alistHasKey(const NodePtr& alist, const std::string& key);

/** Sets a scalar field, preserving the existing dotted/list-form shape if the key exists. */
void alistSetScalar(NodePtr& alist, const std::string& key, NodePtr value);

/** Sets a field whose value is itself a list of items (e.g. tags, a nested block). */
void alistSetRest(NodePtr& alist, const std::string& key, NodePtr rest);

void alistRemoveKey(NodePtr& alist, const std::string& key);

std::optional<std::string> getStr(const NodePtr& alist, const std::string& key);
std::optional<double> getFloat(const NodePtr& alist, const std::string& key);
std::optional<long long> getInt(const NodePtr& alist, const std::string& key);
std::optional<bool> getBool(const NodePtr& alist, const std::string& key);
std::vector<std::string> getStrList(const NodePtr& alist, const std::string& key);

void setStr(NodePtr& alist, const std::string& key, const std::string& value);
void setSymbol(NodePtr& alist, const std::string& key, const std::string& value);
void setFloat(NodePtr& alist, const std::string& key, double value);
void setInt(NodePtr& alist, const std::string& key, long long value);
void setBool(NodePtr& alist, const std::string& key, bool value);
void setStrList(NodePtr& alist, const std::string& key, const std::vector<std::string>& values);

}
