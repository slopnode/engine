#include "doc_text.hpp"

#include "site.hpp"

#include <sstream>

namespace slopdocs {

namespace {

std::string trim(const std::string& s) {
    const std::size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) {
        return {};
    }
    const std::size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

}

DocNode textNode(const std::string& s) {
    DocNode n;
    n.isText = true;
    n.text = s;
    return n;
}

DocNode elem(const std::string& tag, std::vector<DocNode> children) {
    DocNode n;
    n.tag = tag;
    n.children = std::move(children);
    return n;
}

DocNode withAttr(DocNode n, const std::string& key, const std::string& value) {
    n.attrs.emplace_back(key, value);
    return n;
}

DocNode codeInline(const std::string& s) {
    return elem("code", {textNode(s)});
}

DocNode codeBlock(const std::string& lang, const std::string& s) {
    return withAttr(elem("code", {textNode(s)}), "lang", lang);
}

DocNode refNode(const std::string& name) {
    return withAttr(elem("ref"), "name", name);
}

DocNode pageRef(const std::string& id, const std::string& label) {
    DocNode n = withAttr(elem("page-ref"), "page", id);
    if (!label.empty()) {
        n.children.push_back(textNode(label));
    }
    return n;
}

DocNode cell(std::vector<DocNode> children) {
    return elem("cell", std::move(children));
}

DocNode row(std::vector<DocNode> cells) {
    return elem("row", std::move(cells));
}

DocNode headerRow(std::vector<std::string> labels) {
    DocNode h = elem("header");
    for (const std::string& label : labels) {
        h.children.push_back(cell({textNode(label)}));
    }
    return h;
}

bool symbolKnown(const Site& site, const std::string& name) {
    if (site.symbols.count(name) > 0) {
        return true;
    }
    if (site.engineNames.count(name) > 0 && !site.config.engineDocs.empty()) {
        return true;
    }
    return site.findShader(name) != nullptr;
}

std::vector<DocNode> inlineFromText(const Site& site, const std::string& line) {
    std::vector<DocNode> out;
    std::string plain;
    std::size_t i = 0;
    while (i < line.size()) {
        if (line[i] == '`') {
            const std::size_t end = line.find('`', i + 1);
            if (end == std::string::npos) {
                plain += line.substr(i);
                break;
            }
            if (!plain.empty()) {
                out.push_back(textNode(plain));
                plain.clear();
            }
            const std::string name = line.substr(i + 1, end - i - 1);
            out.push_back(symbolKnown(site, name) ? refNode(name) : codeInline(name));
            i = end + 1;
            continue;
        }
        plain.push_back(line[i]);
        ++i;
    }
    if (!plain.empty()) {
        out.push_back(textNode(plain));
    }
    return out;
}

std::vector<DocNode> docTextToBlocks(const Site& site, const std::string& textBlock) {
    std::vector<DocNode> blocks;
    std::istringstream in(textBlock);
    std::string line;
    std::string para;
    auto flush = [&]() {
        if (!para.empty()) {
            blocks.push_back(elem("p", inlineFromText(site, para)));
            para.clear();
        }
    };
    while (std::getline(in, line)) {
        if (line.find_first_not_of(" \t\r") == std::string::npos) {
            flush();
            continue;
        }
        if (!para.empty()) {
            para.push_back(' ');
        }
        para += trim(line);
    }
    flush();
    return blocks;
}

ParsedDoc parseDocTags(const std::string& text) {
    ParsedDoc out;
    std::istringstream in(text);
    std::string line;
    std::vector<std::string> kept;
    DocTag* open = nullptr;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (!t.empty() && t[0] == '@') {
            const std::size_t space = t.find(' ');
            DocTag tag;
            tag.tag = space == std::string::npos ? t.substr(1) : t.substr(1, space - 1);
            tag.text = space == std::string::npos ? std::string() : trim(t.substr(space + 1));
            out.tags.push_back(std::move(tag));
            open = &out.tags.back();
            continue;
        }
        if (open != nullptr && !t.empty()) {
            open->text += " " + t;
            continue;
        }
        open = nullptr;
        kept.push_back(line);
    }
    while (!kept.empty() && trim(kept.back()).empty()) {
        kept.pop_back();
    }
    for (std::size_t i = 0; i < kept.size(); ++i) {
        out.description += kept[i];
        if (i + 1 < kept.size()) {
            out.description.push_back('\n');
        }
    }
    return out;
}

}
