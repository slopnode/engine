#include "shader_pages.hpp"

#include <sstream>

namespace slopdocs {

namespace {

DocNode text(const std::string& s) {
    DocNode n;
    n.isText = true;
    n.text = s;
    return n;
}

DocNode elem(const std::string& tag, std::vector<DocNode> children = {}) {
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
    return elem("code", {text(s)});
}

DocNode refNode(const std::string& name) {
    return withAttr(elem("ref"), "name", name);
}

DocNode pageRef(const std::string& id, const std::string& label) {
    DocNode n = withAttr(elem("page-ref"), "page", id);
    if (!label.empty()) {
        n.children.push_back(text(label));
    }
    return n;
}

DocNode cell(std::vector<DocNode> children) {
    return elem("cell", std::move(children));
}

bool symbolKnown(const Site& site, const std::string& name) {
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
                out.push_back(text(plain));
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
        out.push_back(text(plain));
    }
    return out;
}

DocNode declTable(const Site& site, const std::vector<ShaderDecl>& decls, const char* cls) {
    DocNode table = withAttr(elem("table"), "class", cls);
    table.children.push_back(elem("header", {cell({text("Name")}), cell({text("Type")}), cell({text("Description")})}));
    for (const ShaderDecl& d : decls) {
        std::string type = d.type;
        if (!d.qualifier.empty()) {
            type = d.qualifier + " " + type;
        }
        table.children.push_back(elem("row", {
            cell({codeInline(d.name)}),
            cell({codeInline(type)}),
            cell(inlineFromText(site, d.doc)),
        }));
    }
    return table;
}

std::string companionPath(const ShaderDoc& doc) {
    const std::string& p = doc.path;
    auto swap = [&](const char* from, const char* to) {
        const std::string f(from);
        if (p.size() >= f.size() && p.compare(p.size() - f.size(), f.size(), f) == 0) {
            return p.substr(0, p.size() - f.size()) + to;
        }
        return std::string();
    };
    if (doc.stage == "vertex") {
        return swap("_vert", "_frag");
    }
    if (doc.stage == "fragment") {
        return swap("_frag", "_vert");
    }
    return {};
}

}

namespace {

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
        para += line;
    }
    flush();
    return blocks;
}

}

bool buildShaderPages(Site& site, std::string& error) {
    for (const ShaderDoc& doc : site.shaders) {
        const std::string id = "shaders/" + doc.path;
        if (site.findPage(id) != nullptr) {
            error = "page \"" + id + "\" collides with the generated shader page";
            return false;
        }
        for (const std::string& name : doc.see) {
            if (!symbolKnown(site, name)) {
                error = doc.file.generic_string() + ": @see " + name + " does not resolve";
                return false;
            }
        }
        DocNode root = withAttr(withAttr(elem("page"), "id", id), "title", doc.path);
        for (DocNode& block : docTextToBlocks(site, doc.description)) {
            root.children.push_back(std::move(block));
        }
        if (doc.description.empty()) {
            root.children.push_back(elem("p", {text("This shader has no doc comment yet.")}));
        }

        DocNode facts = withAttr(elem("table"), "class", "sd-shader-facts");
        auto fact = [&](const char* label, std::vector<DocNode> value) {
            facts.children.push_back(elem("row", {cell({text(label)}), cell(std::move(value))}));
        };
        fact("Stage", {text(doc.stage)});
        if (!doc.version.empty()) {
            fact("GLSL", {text(doc.version)});
        }
        if (!doc.usedBy.empty()) {
            fact("Used by", inlineFromText(site, doc.usedBy));
        }
        const std::string companion = companionPath(doc);
        if (!companion.empty() && site.findShader(companion) != nullptr) {
            fact("Pairs with", {pageRef("shaders/" + companion, companion)});
        }
        if (!doc.includes.empty()) {
            std::vector<DocNode> items;
            for (const std::string& inc : doc.includes) {
                if (!items.empty()) {
                    items.push_back(text(", "));
                }
                items.push_back(codeInline(inc));
            }
            fact("Includes", std::move(items));
        }
        if (!doc.localSize.empty()) {
            fact("Work group", {codeInline(doc.localSize)});
        }
        fact("File", {codeInline("shaders/" + doc.path + ".glsl")});
        root.children.push_back(std::move(facts));

        if (!doc.uniforms.empty()) {
            root.children.push_back(withAttr(elem("section", {declTable(site, doc.uniforms, "sd-shader-uniforms")}), "title", "Uniforms"));
        }
        if (!doc.buffers.empty()) {
            DocNode table = withAttr(elem("table"), "class", "sd-shader-buffers");
            table.children.push_back(elem("header", {cell({text("Binding")}), cell({text("Buffer")}), cell({text("Access")}), cell({text("Description")})}));
            for (const ShaderBuffer& b : doc.buffers) {
                table.children.push_back(elem("row", {
                    cell({text(b.binding)}),
                    cell({codeInline(b.name)}),
                    cell({text(b.access.empty() ? "read-write" : b.access)}),
                    cell(inlineFromText(site, b.doc)),
                }));
            }
            root.children.push_back(withAttr(elem("section", {std::move(table)}), "title", "Buffers"));
        }
        if (!doc.inputs.empty()) {
            root.children.push_back(withAttr(elem("section", {declTable(site, doc.inputs, "sd-shader-inputs")}), "title", "Inputs"));
        }
        if (!doc.outputs.empty()) {
            root.children.push_back(withAttr(elem("section", {declTable(site, doc.outputs, "sd-shader-outputs")}), "title", "Outputs"));
        }
        if (!doc.see.empty()) {
            std::vector<DocNode> items;
            for (const std::string& name : doc.see) {
                items.push_back(elem("item", {refNode(name)}));
            }
            root.children.push_back(withAttr(elem("section", {elem("ul", std::move(items))}), "title", "See also"));
        }
        if (!validateDocPage(root, error)) {
            error = doc.file.generic_string() + ": " + error;
            return false;
        }
        Page page;
        page.id = id;
        page.title = doc.path;
        const std::size_t slash = doc.path.rfind('/');
        page.navTitle = slash == std::string::npos ? doc.path : doc.path.substr(slash + 1);
        page.root = std::move(root);
        page.source = doc.file;
        page.generated = true;
        site.pages.push_back(std::move(page));
    }
    return true;
}

}
