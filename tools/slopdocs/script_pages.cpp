#include "script_pages.hpp"

#include "doc_text.hpp"

#include <algorithm>
#include <cctype>

namespace slopdocs {

std::string scriptAnchor(const std::string& name) {
    std::string out = "def-";
    for (char c : name) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc)) {
            out.push_back(static_cast<char>(std::tolower(uc)));
        } else if (c == '!') {
            out += "-bang";
        } else if (c == '?') {
            out += "-p";
        } else if (c == '*') {
            out += "-star";
        } else {
            out.push_back('-');
        }
    }
    return out;
}

bool buildScriptPages(Site& site, std::string& error) {
    for (const ScriptDoc& script : site.scripts) {
        if (!script.error.empty()) {
            error = script.error;
            return false;
        }
        const std::string id = "scripts/" + script.path;
        for (const ScriptDef& def : script.defs) {
            site.symbols.emplace(def.name, id + ".html#" + scriptAnchor(def.name));
        }
    }
    for (const ScriptDoc& script : site.scripts) {
        const std::string id = "scripts/" + script.path;
        if (site.findPage(id) != nullptr) {
            error = "page \"" + id + "\" collides with the generated script page";
            return false;
        }
        DocNode root = withAttr(withAttr(elem("page"), "id", id), "title", script.path);
        for (DocNode& block : docTextToBlocks(site, script.summary)) {
            root.children.push_back(std::move(block));
        }

        DocNode facts = withAttr(elem("table"), "class", "sd-script-facts");
        facts.children.push_back(row({cell({textNode("File")}), cell({codeInline("scripts/" + script.path + ".s7")})}));
        if (!script.requiredScripts.empty()) {
            std::vector<DocNode> items;
            for (const std::string& req : script.requiredScripts) {
                if (!items.empty()) {
                    items.push_back(elem("br"));
                }
                if (site.findPage("scripts/" + req) != nullptr || std::any_of(site.scripts.begin(), site.scripts.end(), [&](const ScriptDoc& s) { return s.path == req; })) {
                    items.push_back(pageRef("scripts/" + req, req));
                } else {
                    items.push_back(codeInline(req));
                }
            }
            facts.children.push_back(row({cell({textNode("Requires")}), cell(std::move(items))}));
        }
        if (!script.requiresData.empty()) {
            std::vector<DocNode> items;
            for (const std::string& req : script.requiresData) {
                if (!items.empty()) {
                    items.push_back(elem("br"));
                }
                if (std::any_of(site.data.begin(), site.data.end(), [&](const DataDoc& d) { return d.path == req; })) {
                    items.push_back(pageRef("data/" + req, req));
                } else {
                    items.push_back(codeInline("data/" + req + ".s7"));
                }
            }
            facts.children.push_back(row({cell({textNode("Loads data")}), cell(std::move(items))}));
        }
        root.children.push_back(std::move(facts));

        auto describe = [&](const ScriptDef& def, std::string& error) {
            std::vector<DocNode> parts;
            bool first = true;
            auto br = [&]() {
                if (!first) {
                    parts.push_back(elem("br"));
                }
                first = false;
            };
            if (site.engineNames.count(def.name) > 0 && !site.config.engineDocs.empty()) {
                br();
                parts.push_back(textNode("Engine hook "));
                parts.push_back(refNode(def.name));
                parts.push_back(textNode("."));
            }
            for (DocNode& block : docTextToBlocks(site, def.doc)) {
                br();
                for (DocNode& n : block.children) {
                    parts.push_back(std::move(n));
                }
            }
            for (const ScriptParam& p : def.params) {
                br();
                parts.push_back(withAttr(codeInline(p.name), "class", "sd-param"));
                if (!p.type.empty()) {
                    parts.push_back(textNode(" "));
                    parts.push_back(withAttr(codeInline(p.type), "class", "sd-type"));
                }
                if (!p.doc.empty()) {
                    parts.push_back(textNode(" "));
                    for (DocNode& n : inlineFromText(site, p.doc)) {
                        parts.push_back(std::move(n));
                    }
                }
            }
            if (!def.returnType.empty() || !def.returnDoc.empty()) {
                br();
                parts.push_back(textNode("Returns "));
                if (!def.returnType.empty()) {
                    parts.push_back(withAttr(codeInline(def.returnType), "class", "sd-type"));
                    if (!def.returnDoc.empty()) {
                        parts.push_back(textNode(" "));
                    }
                }
                for (DocNode& n : inlineFromText(site, def.returnDoc)) {
                    parts.push_back(std::move(n));
                }
            }
            if (!def.see.empty()) {
                br();
                parts.push_back(textNode("See "));
                for (std::size_t i = 0; i < def.see.size(); ++i) {
                    if (i > 0) {
                        parts.push_back(textNode(", "));
                    }
                    if (!symbolKnown(site, def.see[i])) {
                        error = script.file.generic_string() + ": " + def.name + " @see " + def.see[i] + " does not resolve";
                        return parts;
                    }
                    parts.push_back(refNode(def.see[i]));
                }
            }
            return parts;
        };
        auto declaration = [&](const ScriptDef& def) {
            std::string sig = "(" + def.name;
            for (const SigParam& p : def.sigParams) {
                sig += " ";
                if (p.optional) {
                    sig += "[" + p.name + "]";
                } else if (p.rest) {
                    sig += p.name + " ...";
                } else {
                    sig += p.name;
                }
            }
            sig += ")";
            if (!def.returnType.empty()) {
                sig += " \xE2\x86\x92 " + def.returnType;
            }
            const bool typed = !def.params.empty();
            if (typed) {
                for (const SigParam& p : def.sigParams) {
                    std::string type = "any?";
                    for (const ScriptParam& doc : def.params) {
                        if (doc.name == p.name && !doc.type.empty()) {
                            type = doc.type;
                        }
                    }
                    sig += "\n  " + p.name + " : " + type;
                    if (!p.defaultValue.empty()) {
                        sig += " = " + p.defaultValue;
                    }
                }
            }
            return codeBlock("scheme", sig);
        };
        auto defBlocks = [&](const char* title, const char* kind, std::string& error) {
            std::vector<DocNode> sections;
            for (const ScriptDef& def : script.defs) {
                if (def.kind != kind) {
                    continue;
                }
                DocNode section = withAttr(withAttr(elem("section"), "title", def.name), "id", scriptAnchor(def.name));
                section = withAttr(section, "class", "sd-def");
                section.children.push_back(declaration(def));
                if (site.engineNames.count(def.name) > 0 && !site.config.engineDocs.empty()) {
                    section.children.push_back(elem("p", {textNode("Implements the engine hook "), refNode(def.name), textNode(".")}));
                }
                for (DocNode& block : docTextToBlocks(site, def.doc)) {
                    section.children.push_back(std::move(block));
                }
                bool anyParamDoc = false;
                for (const ScriptParam& p : def.params) {
                    anyParamDoc = anyParamDoc || !p.doc.empty();
                }
                if (anyParamDoc || !def.returnDoc.empty()) {
                    DocNode tree = withAttr(elem("ul"), "class", "sd-tree sd-def-params");
                    for (const ScriptParam& p : def.params) {
                        if (p.doc.empty()) {
                            continue;
                        }
                        std::vector<DocNode> line = {withAttr(codeInline(p.name), "class", "sd-key")};
                        line.push_back(withAttr(elem("span", inlineFromText(site, p.doc)), "class", "sd-val"));
                        tree.children.push_back(withAttr(elem("item", {elem("p", std::move(line))}), "class", "sd-leaf"));
                    }
                    if (!def.returnDoc.empty()) {
                        std::vector<DocNode> line = {withAttr(codeInline("returns"), "class", "sd-key")};
                        line.push_back(withAttr(elem("span", inlineFromText(site, def.returnDoc)), "class", "sd-val"));
                        tree.children.push_back(withAttr(elem("item", {elem("p", std::move(line))}), "class", "sd-leaf"));
                    }
                    section.children.push_back(std::move(tree));
                }
                if (!def.see.empty()) {
                    std::vector<DocNode> parts = {textNode("See ")};
                    for (std::size_t i = 0; i < def.see.size(); ++i) {
                        if (i > 0) {
                            parts.push_back(textNode(", "));
                        }
                        if (!symbolKnown(site, def.see[i])) {
                            error = script.file.generic_string() + ": " + def.name + " @see " + def.see[i] + " does not resolve";
                            return DocNode();
                        }
                        parts.push_back(refNode(def.see[i]));
                    }
                    parts.push_back(textNode("."));
                    section.children.push_back(elem("p", std::move(parts)));
                }
                sections.push_back(std::move(section));
            }
            if (sections.empty()) {
                return DocNode();
            }
            return withAttr(withAttr(elem("section", std::move(sections)), "title", title), "class", "sd-defs");
        };
        {
            DocNode table = withAttr(elem("table"), "class", "sd-defs sd-defs-variables");
            table.children.push_back(headerRow({"Name", "Description"}));
            bool any = false;
            for (const ScriptDef& def : script.defs) {
                if (def.kind != "variable" && def.kind != "constant") {
                    continue;
                }
                any = true;
                std::vector<DocNode> desc = describe(def, error);
                if (!error.empty()) {
                    return false;
                }
                table.children.push_back(withAttr(row({cell({codeInline(def.name)}), cell(std::move(desc))}), "id", scriptAnchor(def.name)));
            }
            if (any) {
                root.children.push_back(withAttr(elem("section", {std::move(table)}), "title", "Variables"));
            }
        }
        for (const auto& [title, kind] : std::vector<std::pair<const char*, const char*>>{{"Functions", "function"}, {"Macros", "macro"}}) {
            DocNode section = defBlocks(title, kind, error);
            if (!error.empty()) {
                return false;
            }
            if (!section.tag.empty()) {
                root.children.push_back(std::move(section));
            }
        }

        if (!validateDocPage(root, error)) {
            error = script.file.generic_string() + ": " + error;
            return false;
        }
        Page page;
        page.id = id;
        page.title = script.path;
        const std::size_t slash = script.path.rfind('/');
        page.navTitle = slash == std::string::npos ? script.path : script.path.substr(slash + 1);
        page.root = std::move(root);
        page.source = script.file;
        page.generated = true;
        site.pages.push_back(std::move(page));
    }
    return true;
}

}
