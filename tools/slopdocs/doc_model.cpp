#include "doc_model.hpp"

#include <s7.h>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace slopdocs {

const std::string* DocNode::attr(const std::string& name) const {
    for (const auto& [key, value] : attrs) {
        if (key == name) {
            return &value;
        }
    }
    return nullptr;
}

namespace {

std::string s7ToText(s7_scheme* sc, s7_pointer v) {
    if (s7_is_string(v)) {
        return s7_string(v);
    }
    if (s7_is_symbol(v)) {
        return s7_symbol_name(v);
    }
    if (s7_is_boolean(v)) {
        return s7_boolean(sc, v) ? "#t" : "#f";
    }
    char* raw = s7_object_to_c_string(sc, v);
    std::string text = raw != nullptr ? raw : "";
    std::free(raw);
    return text;
}

bool isNodeList(s7_pointer v) {
    return s7_is_pair(v) && s7_is_symbol(s7_car(v));
}

bool readAttrs(s7_scheme* sc, s7_pointer attrList, DocNode& out, std::string& error) {
    for (s7_pointer it = attrList; s7_is_pair(it); it = s7_cdr(it)) {
        const s7_pointer entry = s7_car(it);
        if (!s7_is_pair(entry) || !s7_is_symbol(s7_car(entry)) || !s7_is_pair(s7_cdr(entry))) {
            error = "attribute entries are (name value)";
            return false;
        }
        out.attrs.emplace_back(s7_symbol_name(s7_car(entry)), s7ToText(sc, s7_car(s7_cdr(entry))));
    }
    return true;
}

bool convert(s7_scheme* sc, s7_pointer v, DocNode& out, std::string& error, const std::string& path) {
    if (s7_is_string(v)) {
        out.isText = true;
        out.text = s7_string(v);
        return true;
    }
    if (s7_is_number(v)) {
        out.isText = true;
        out.text = s7ToText(sc, v);
        return true;
    }
    if (!isNodeList(v)) {
        error = path + ": expected a node or string, got " + s7ToText(sc, v);
        return false;
    }
    out.tag = s7_symbol_name(s7_car(v));
    const std::string here = path.empty() ? out.tag : path + " > " + out.tag;
    s7_pointer rest = s7_cdr(v);
    if (s7_is_pair(rest) && s7_is_pair(s7_car(rest)) && s7_is_symbol(s7_car(s7_car(rest))) &&
        std::string(s7_symbol_name(s7_car(s7_car(rest)))) == "@") {
        if (!readAttrs(sc, s7_cdr(s7_car(rest)), out, error)) {
            error = here + ": " + error;
            return false;
        }
        rest = s7_cdr(rest);
    }
    for (; s7_is_pair(rest); rest = s7_cdr(rest)) {
        const s7_pointer child = s7_car(rest);
        if (s7_is_boolean(child) && !s7_boolean(sc, child)) {
            continue;
        }
        if (s7_is_null(sc, child)) {
            continue;
        }
        DocNode node;
        if (!convert(sc, child, node, error, here)) {
            return false;
        }
        out.children.push_back(std::move(node));
    }
    if (!s7_is_null(sc, rest)) {
        error = here + ": improper list";
        return false;
    }
    return true;
}

enum class Kind { Block, Inline, Both, Special };

enum class ChildRule { None, Inline, Blocks, InlineOrBlocks, Text, ListItems, TableRows, Cells, DlEntries, Raw };

struct Spec {
    Kind kind;
    ChildRule rule;
};

const std::unordered_map<std::string, Spec>& specs() {
    static const std::unordered_map<std::string, Spec> table = {
        {"page", {Kind::Special, ChildRule::Blocks}},
        {"section", {Kind::Block, ChildRule::Blocks}},
        {"p", {Kind::Block, ChildRule::Inline}},
        {"code", {Kind::Both, ChildRule::Text}},
        {"ul", {Kind::Block, ChildRule::ListItems}},
        {"ol", {Kind::Block, ChildRule::ListItems}},
        {"item", {Kind::Special, ChildRule::InlineOrBlocks}},
        {"table", {Kind::Block, ChildRule::TableRows}},
        {"header", {Kind::Special, ChildRule::Cells}},
        {"row", {Kind::Special, ChildRule::Cells}},
        {"cell", {Kind::Special, ChildRule::Inline}},
        {"dl", {Kind::Block, ChildRule::DlEntries}},
        {"term", {Kind::Special, ChildRule::Inline}},
        {"note", {Kind::Block, ChildRule::InlineOrBlocks}},
        {"tip", {Kind::Block, ChildRule::InlineOrBlocks}},
        {"warning", {Kind::Block, ChildRule::InlineOrBlocks}},
        {"quote-block", {Kind::Block, ChildRule::InlineOrBlocks}},
        {"image", {Kind::Block, ChildRule::Inline}},
        {"hr", {Kind::Block, ChildRule::None}},
        {"html", {Kind::Both, ChildRule::Raw}},
        {"em", {Kind::Inline, ChildRule::Inline}},
        {"strong", {Kind::Inline, ChildRule::Inline}},
        {"kbd", {Kind::Inline, ChildRule::Inline}},
        {"link", {Kind::Inline, ChildRule::Inline}},
        {"page-ref", {Kind::Inline, ChildRule::Inline}},
        {"ref", {Kind::Inline, ChildRule::None}},
        {"br", {Kind::Inline, ChildRule::None}},
    };
    return table;
}

std::string describe(const DocNode& node) {
    if (node.isText) {
        std::string snippet = collapseWhitespace(node.text);
        if (snippet.size() > 24) {
            snippet = snippet.substr(0, 24) + "...";
        }
        return "\"" + snippet + "\"";
    }
    const std::string* title = node.attr("title");
    if (title != nullptr) {
        return node.tag + " \"" + *title + "\"";
    }
    return node.tag;
}

bool isInlineNode(const DocNode& node) {
    if (node.isText) {
        return true;
    }
    const auto it = specs().find(node.tag);
    if (it == specs().end()) {
        return false;
    }
    return it->second.kind == Kind::Inline || it->second.kind == Kind::Both;
}

bool isBlockNode(const DocNode& node) {
    if (node.isText) {
        return false;
    }
    const auto it = specs().find(node.tag);
    if (it == specs().end()) {
        return false;
    }
    return it->second.kind == Kind::Block || it->second.kind == Kind::Both;
}

bool validate(DocNode& node, const std::string& path, std::string& error);

bool validateChildren(DocNode& node, const std::string& here, std::string& error) {
    for (DocNode& child : node.children) {
        if (!validate(child, here, error)) {
            return false;
        }
    }
    return true;
}

DocNode wrapInParagraph(std::vector<DocNode> children) {
    DocNode p;
    p.tag = "p";
    p.children = std::move(children);
    return p;
}

bool applyInlineOrBlocks(DocNode& node, const std::string& here, std::string& error) {
    if (node.children.empty()) {
        return true;
    }
    bool hasInlineOnly = false;
    bool hasBlockOnly = false;
    for (const DocNode& c : node.children) {
        const bool in = isInlineNode(c);
        const bool bl = isBlockNode(c);
        if (in && !bl) {
            hasInlineOnly = true;
        }
        if (bl && !in) {
            hasBlockOnly = true;
        }
        if (!in && !bl) {
            error = here + ": " + describe(c) + " is not allowed here";
            return false;
        }
    }
    if (hasInlineOnly && hasBlockOnly) {
        error = here + ": mixes inline content with blocks; wrap the text in (p ...)";
        return false;
    }
    if (hasInlineOnly) {
        node.children = {wrapInParagraph(std::move(node.children))};
    }
    return true;
}

bool validate(DocNode& node, const std::string& path, std::string& error) {
    if (node.isText) {
        return true;
    }
    const auto it = specs().find(node.tag);
    if (it == specs().end()) {
        error = path + ": unknown node " + node.tag;
        return false;
    }
    const std::string here = path.empty() ? describe(node) : path + " > " + describe(node);
    const Spec& spec = it->second;
    switch (spec.rule) {
    case ChildRule::None:
        if (!node.children.empty()) {
            error = here + ": takes no content";
            return false;
        }
        return true;
    case ChildRule::Text:
    case ChildRule::Raw:
        for (const DocNode& child : node.children) {
            if (!child.isText) {
                error = here + ": only text is allowed inside " + node.tag;
                return false;
            }
        }
        return true;
    case ChildRule::Inline:
        for (const DocNode& child : node.children) {
            if (!isInlineNode(child)) {
                error = here + ": block node " + describe(child) + " is not allowed inside " + node.tag;
                return false;
            }
        }
        return validateChildren(node, here, error);
    case ChildRule::Blocks:
        for (const DocNode& child : node.children) {
            if (!isBlockNode(child)) {
                error = here + ": " + describe(child) + " is not a block node; wrap text in (p ...)";
                return false;
            }
        }
        return validateChildren(node, here, error);
    case ChildRule::InlineOrBlocks:
        if (!applyInlineOrBlocks(node, here, error)) {
            return false;
        }
        return validateChildren(node, here, error);
    case ChildRule::ListItems: {
        std::vector<DocNode> items;
        for (DocNode& child : node.children) {
            if (!child.isText && child.tag == "item") {
                items.push_back(std::move(child));
                continue;
            }
            if (!isInlineNode(child)) {
                error = here + ": list entries are inline content or (item ...)";
                return false;
            }
            DocNode item;
            item.tag = "item";
            item.children.push_back(std::move(child));
            items.push_back(std::move(item));
        }
        node.children = std::move(items);
        return validateChildren(node, here, error);
    }
    case ChildRule::TableRows:
        for (const DocNode& child : node.children) {
            if (child.isText || (child.tag != "header" && child.tag != "row")) {
                error = here + ": table content is (header ...) and (row ...)";
                return false;
            }
        }
        return validateChildren(node, here, error);
    case ChildRule::Cells: {
        std::vector<DocNode> cells;
        for (DocNode& child : node.children) {
            if (!child.isText && child.tag == "cell") {
                cells.push_back(std::move(child));
                continue;
            }
            if (!isInlineNode(child)) {
                error = here + ": cells are inline content or (cell ...)";
                return false;
            }
            DocNode cell;
            cell.tag = "cell";
            cell.children.push_back(std::move(child));
            cells.push_back(std::move(cell));
        }
        node.children = std::move(cells);
        return validateChildren(node, here, error);
    }
    case ChildRule::DlEntries:
        for (const DocNode& child : node.children) {
            if (child.isText || child.tag != "term") {
                error = here + ": dl entries are (term \"name\" description...)";
                return false;
            }
        }
        return validateChildren(node, here, error);
    }
    return true;
}

}

bool docNodeFromS7(s7_scheme* sc, s7_pointer value, DocNode& out, std::string& error) {
    return convert(sc, value, out, error, "");
}

bool validateDocPage(DocNode& page, std::string& error) {
    if (page.isText || page.tag != "page") {
        error = "not a page";
        return false;
    }
    return validate(page, "", error);
}

std::string collapseWhitespace(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool pendingSpace = false;
    for (char c : text) {
        if (c == '\n' || c == '\r' || c == '\t') {
            pendingSpace = true;
            continue;
        }
        if (pendingSpace) {
            if (c == ' ') {
                continue;
            }
            if (!out.empty() && out.back() != ' ') {
                out.push_back(' ');
            }
            pendingSpace = false;
        }
        out.push_back(c);
    }
    if (pendingSpace && !out.empty() && out.back() != ' ') {
        out.push_back(' ');
    }
    return out;
}

std::string dedentCode(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    while (!lines.empty() && lines.front().find_first_not_of(" \t") == std::string::npos) {
        lines.erase(lines.begin());
    }
    while (!lines.empty() && lines.back().find_first_not_of(" \t") == std::string::npos) {
        lines.pop_back();
    }
    std::size_t indent = std::numeric_limits<std::size_t>::max();
    for (const std::string& l : lines) {
        const std::size_t first = l.find_first_not_of(" \t");
        if (first != std::string::npos) {
            indent = std::min(indent, first);
        }
    }
    if (indent == std::numeric_limits<std::size_t>::max()) {
        indent = 0;
    }
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& l = lines[i];
        out += l.size() >= indent ? l.substr(indent) : std::string();
        if (i + 1 < lines.size()) {
            out.push_back('\n');
        }
    }
    return out;
}

}
