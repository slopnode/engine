#include "script_doc.hpp"

#include "doc_text.hpp"

#include "core/sexpr.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace slopdocs {

namespace fs = std::filesystem;

namespace {

std::string trim(const std::string& s) {
    const std::size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) {
        return {};
    }
    const std::size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

std::string render(const slopengine::Sexpr& e) {
    switch (e.kind) {
    case slopengine::SexprKind::String:
        return "\"" + e.text + "\"";
    case slopengine::SexprKind::List: {
        std::string out = "(";
        for (std::size_t i = 0; i < e.list.size(); ++i) {
            if (i > 0) {
                out.push_back(' ');
            }
            out += render(e.list[i]);
        }
        return out + ")";
    }
    default:
        return e.text;
    }
}

std::vector<SigParam> parseLambdaList(const std::vector<slopengine::Sexpr>& items, std::size_t from) {
    std::vector<SigParam> out;
    bool restNext = false;
    for (std::size_t i = from; i < items.size(); ++i) {
        const slopengine::Sexpr& item = items[i];
        if (item.isAtom(".")) {
            restNext = true;
            continue;
        }
        SigParam p;
        if (item.isList() && !item.list.empty() && item.list.front().kind == slopengine::SexprKind::Atom) {
            p.name = item.list.front().text;
            p.optional = true;
            if (item.list.size() > 1) {
                p.defaultValue = render(item.list[1]);
            }
        } else if (item.kind == slopengine::SexprKind::Atom) {
            if (item.text.size() > 1 && item.text.front() == ':') {
                continue;
            }
            p.name = item.text;
        } else {
            continue;
        }
        p.rest = restNext;
        restNext = false;
        out.push_back(std::move(p));
    }
    return out;
}

std::string commentBlockAbove(const std::vector<std::string>& lines, int line) {
    std::vector<std::string> block;
    for (int i = line - 2; i >= 0; --i) {
        const std::string t = trim(lines[static_cast<std::size_t>(i)]);
        if (t.rfind(";;;", 0) != 0) {
            break;
        }
        std::string text = t.substr(3);
        if (!text.empty() && text.front() == ' ') {
            text.erase(0, 1);
        }
        block.insert(block.begin(), text);
    }
    std::string out;
    for (std::size_t i = 0; i < block.size(); ++i) {
        out += block[i];
        if (i + 1 < block.size()) {
            out.push_back('\n');
        }
    }
    return out;
}

bool takeType(const std::string& text, std::string& type, std::string& rest) {
    const std::string t = trim(text);
    if (t.empty()) {
        return false;
    }
    if (t.front() == '(') {
        int depth = 0;
        for (std::size_t i = 0; i < t.size(); ++i) {
            if (t[i] == '(') {
                ++depth;
            } else if (t[i] == ')') {
                --depth;
                if (depth == 0) {
                    type = t.substr(0, i + 1);
                    rest = trim(t.substr(i + 1));
                    return true;
                }
            }
        }
        return false;
    }
    const std::size_t space = t.find(' ');
    const std::string first = space == std::string::npos ? t : t.substr(0, space);
    if (first.back() == '?' || first.front() == '#') {
        type = first;
        rest = space == std::string::npos ? std::string() : trim(t.substr(space + 1));
        return true;
    }
    return false;
}

void applyTags(ScriptDef& def, const std::string& raw) {
    const ParsedDoc parsed = parseDocTags(raw);
    def.doc = parsed.description;
    for (const DocTag& tag : parsed.tags) {
        if (tag.tag == "param") {
            const std::string t = trim(tag.text);
            const std::size_t space = t.find(' ');
            ScriptParam param;
            param.name = space == std::string::npos ? t : t.substr(0, space);
            const std::string after = space == std::string::npos ? std::string() : t.substr(space + 1);
            std::string type;
            std::string rest;
            if (takeType(after, type, rest)) {
                param.type = type;
                param.doc = rest;
            } else {
                param.doc = trim(after);
            }
            def.params.push_back(std::move(param));
        } else if (tag.tag == "return" || tag.tag == "returns") {
            std::string type;
            std::string rest;
            if (takeType(tag.text, type, rest)) {
                def.returnType = type;
                def.returnDoc = rest;
            } else {
                def.returnDoc = trim(tag.text);
            }
        } else if (tag.tag == "see") {
            std::istringstream in(tag.text);
            std::string name;
            while (in >> name) {
                def.see.push_back(name);
            }
        }
    }
}

void parseScript(ScriptDoc& doc, const std::string& source) {
    std::vector<std::string> lines;
    {
        std::istringstream in(source);
        std::string line;
        while (std::getline(in, line)) {
            lines.push_back(line);
        }
    }
    const slopengine::SexprParseResult parsed = slopengine::parseSexprs(source);
    if (!parsed.ok) {
        doc.error = slopengine::formatSexprError(doc.file.generic_string(), parsed.error);
        return;
    }
    bool first = true;
    for (const slopengine::Sexpr& form : parsed.forms) {
        if (first) {
            first = false;
            if (form.isString()) {
                doc.summary = form.text;
                continue;
            }
            if (!lines.empty()) {
                std::vector<std::string> top;
                for (int i = 0; i < form.line - 1 && i < static_cast<int>(lines.size()); ++i) {
                    const std::string t = trim(lines[static_cast<std::size_t>(i)]);
                    if (t.rfind(";;;", 0) == 0) {
                        std::string text = t.substr(3);
                        if (!text.empty() && text.front() == ' ') {
                            text.erase(0, 1);
                        }
                        top.push_back(text);
                    } else if (!t.empty()) {
                        break;
                    }
                }
                for (std::size_t i = 0; i < top.size(); ++i) {
                    doc.summary += top[i];
                    if (i + 1 < top.size()) {
                        doc.summary.push_back('\n');
                    }
                }
            }
        }
        if (!form.isList() || form.list.empty() || form.list.front().kind != slopengine::SexprKind::Atom) {
            continue;
        }
        const std::string& head = form.list.front().text;
        if ((head == "require" || head == "require-data") && form.list.size() == 2 && form.list[1].isString()) {
            (head == "require" ? doc.requiredScripts : doc.requiresData).push_back(form.list[1].text);
            continue;
        }
        if (head != "define" && head != "define*" && head != "define-macro" && head != "define-constant") {
            continue;
        }
        if (form.list.size() < 2) {
            continue;
        }
        const slopengine::Sexpr& target = form.list[1];
        ScriptDef def;
        def.line = form.line;
        std::size_t bodyStart = 2;
        if (target.isList() && !target.list.empty() && target.list.front().kind == slopengine::SexprKind::Atom) {
            def.name = target.list.front().text;
            def.kind = head == "define-macro" ? "macro" : "function";
            def.signature = render(target);
            def.sigParams = parseLambdaList(target.list, 1);
        } else if (target.kind == slopengine::SexprKind::Atom) {
            def.name = target.text;
            def.kind = head == "define-constant" ? "constant" : "variable";
            def.signature = target.text;
            if (form.list.size() > 2 && form.list[2].isList() && !form.list[2].list.empty() &&
                form.list[2].list.front().isAtom("lambda") && form.list[2].list.size() >= 3) {
                def.kind = "function";
                slopengine::Sexpr sig = form.list[2].list[1];
                std::string args;
                if (sig.isList()) {
                    for (const slopengine::Sexpr& a : sig.list) {
                        args += " " + render(a);
                    }
                    def.sigParams = parseLambdaList(sig.list, 0);
                } else {
                    args = " . " + sig.text;
                    SigParam rest;
                    rest.name = sig.text;
                    rest.rest = true;
                    def.sigParams.push_back(rest);
                }
                def.signature = "(" + def.name + args + ")";
                const slopengine::Sexpr& lambda = form.list[2];
                if (lambda.list.size() > 3 && lambda.list[2].isString()) {
                    applyTags(def, lambda.list[2].text);
                }
                bodyStart = form.list.size();
            }
        } else {
            continue;
        }
        if (def.doc.empty() && form.list.size() > bodyStart + 1 && form.list[bodyStart].isString()) {
            applyTags(def, form.list[bodyStart].text);
        }
        if (def.doc.empty() && def.params.empty() && def.returnDoc.empty()) {
            const std::string above = commentBlockAbove(lines, def.line);
            if (!above.empty()) {
                applyTags(def, above);
            }
        }
        doc.defs.push_back(std::move(def));
    }
}

}

std::vector<ScriptDoc> scanScripts(const fs::path& packageRoot) {
    std::vector<ScriptDoc> out;
    const fs::path root = packageRoot / "scripts";
    if (!fs::is_directory(root)) {
        return out;
    }
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".s7") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& file : files) {
        ScriptDoc doc;
        doc.file = file;
        fs::path rel = fs::relative(file, root);
        rel.replace_extension();
        doc.path = rel.generic_string();
        std::ifstream in(file, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        parseScript(doc, buffer.str());
        out.push_back(std::move(doc));
    }
    return out;
}

}
