#pragma once

#include "doc_model.hpp"

#include <string>
#include <vector>

namespace slopdocs {

struct Site;

DocNode textNode(const std::string& s);
DocNode elem(const std::string& tag, std::vector<DocNode> children = {});
DocNode withAttr(DocNode n, const std::string& key, const std::string& value);
DocNode codeInline(const std::string& s);
DocNode codeBlock(const std::string& lang, const std::string& s);
DocNode refNode(const std::string& name);
DocNode pageRef(const std::string& id, const std::string& label);
DocNode cell(std::vector<DocNode> children);
DocNode row(std::vector<DocNode> cells);
DocNode headerRow(std::vector<std::string> labels);

bool symbolKnown(const Site& site, const std::string& name);

std::vector<DocNode> inlineFromText(const Site& site, const std::string& line);
std::vector<DocNode> docTextToBlocks(const Site& site, const std::string& text);

struct DocTag {
    std::string tag;
    std::string text;
};

struct ParsedDoc {
    std::string description;
    std::vector<DocTag> tags;
};

ParsedDoc parseDocTags(const std::string& text);

}
