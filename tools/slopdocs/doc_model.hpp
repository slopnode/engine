#pragma once

#include <string>
#include <utility>
#include <vector>

struct s7_scheme;
struct s7_cell;
using s7_pointer = s7_cell*;

namespace slopdocs {

struct DocNode {
    bool isText = false;
    std::string tag;
    std::string text;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<DocNode> children;

    const std::string* attr(const std::string& name) const;
    bool hasAttr(const std::string& name) const { return attr(name) != nullptr; }
};

bool docNodeFromS7(s7_scheme* sc, s7_pointer value, DocNode& out, std::string& error);

bool validateDocPage(DocNode& page, std::string& error);

std::string collapseWhitespace(const std::string& text);
std::string dedentCode(const std::string& text);

}
