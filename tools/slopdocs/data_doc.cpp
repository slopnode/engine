#include "data_doc.hpp"

#include "core/engine_package.hpp"
#include "core/sexpr.hpp"
#include "script/scheme_harden.hpp"

#include <s7.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
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

DataValue fromS7(s7_scheme* sc, s7_pointer v, int depth) {
    DataValue out;
    if (depth > 64) {
        out.kind = DataValue::Kind::Other;
        out.text = "...";
        return out;
    }
    if (s7_is_null(sc, v)) {
        out.kind = DataValue::Kind::Nil;
        return out;
    }
    if (s7_is_string(v)) {
        out.kind = DataValue::Kind::String;
        out.text = s7_string(v);
        return out;
    }
    if (s7_is_symbol(v)) {
        out.kind = DataValue::Kind::Symbol;
        out.text = s7_symbol_name(v);
        return out;
    }
    if (s7_is_boolean(v)) {
        out.kind = DataValue::Kind::Bool;
        out.text = s7_boolean(sc, v) ? "#t" : "#f";
        return out;
    }
    if (s7_is_number(v)) {
        out.kind = DataValue::Kind::Number;
        char* raw = s7_object_to_c_string(sc, v);
        out.text = raw != nullptr ? raw : "";
        std::free(raw);
        return out;
    }
    if (s7_is_pair(v)) {
        out.kind = DataValue::Kind::List;
        s7_pointer it = v;
        while (s7_is_pair(it)) {
            out.items.push_back(fromS7(sc, s7_car(it), depth + 1));
            it = s7_cdr(it);
        }
        if (!s7_is_null(sc, it)) {
            out.items.push_back(fromS7(sc, it, depth + 1));
            out.dotted = true;
        }
        return out;
    }
    out.kind = DataValue::Kind::Other;
    char* raw = s7_object_to_c_string(sc, v);
    out.text = raw != nullptr ? raw : "";
    std::free(raw);
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

void parseDataFile(DataDoc& doc, const std::string& source) {
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
        if (!form.isList() || form.list.size() < 2 || !form.list.front().isAtom("define")) {
            continue;
        }
        if (form.list[1].kind != slopengine::SexprKind::Atom) {
            continue;
        }
        DataDef def;
        def.name = form.list[1].text;
        def.line = form.line;
        def.doc = commentBlockAbove(lines, form.line);
        doc.defs.push_back(std::move(def));
    }
}

void loadHelper(s7_scheme* sc, const fs::path& file) {
    if (fs::is_regular_file(file)) {
        s7_load(sc, file.string().c_str());
    }
}

}

std::vector<DataDoc> scanData(const fs::path& packageRoot, bool verbose) {
    std::vector<DataDoc> out;
    const fs::path root = packageRoot / "data";
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
        DataDoc doc;
        doc.file = file;
        fs::path rel = fs::relative(file, root);
        rel.replace_extension();
        doc.path = rel.generic_string();
        std::ifstream in(file, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        parseDataFile(doc, buffer.str());
        out.push_back(std::move(doc));
    }

    s7_scheme* sc = s7_init();
    slopengine::hardenSchemeRuntime(sc);
    fs::path engine = packageRoot;
    if (!fs::is_regular_file(packageRoot / "scripts" / "lists.s7")) {
        const auto resolved = slopengine::resolveEnginePackage();
        if (resolved) {
            engine = *resolved;
        }
    }
    loadHelper(sc, engine / "scripts" / "lists.s7");
    loadHelper(sc, engine / "scripts" / "math.s7");
    for (DataDoc& doc : out) {
        if (!doc.error.empty()) {
            continue;
        }
        slopengine::clearScriptingErrors();
        s7_load(sc, doc.file.string().c_str());
        if (slopengine::scriptingErrorsOccurred()) {
            std::cerr << "slopdocs: warning: " << doc.file.generic_string() << " did not evaluate; its values are not shown\n";
            continue;
        }
        doc.evaluated = true;
        if (verbose) {
            std::cout << "slopdocs: evaluated " << doc.file.generic_string() << "\n";
        }
        for (DataDef& def : doc.defs) {
            if (!s7_is_defined(sc, def.name.c_str())) {
                continue;
            }
            def.value = fromS7(sc, s7_symbol_value(sc, s7_make_symbol(sc, def.name.c_str())), 0);
            def.evaluated = true;
        }
    }
    slopengine::clearScriptingErrors();
    s7_quit(sc);
    return out;
}

std::string writeDataValue(const DataValue& v) {
    switch (v.kind) {
    case DataValue::Kind::Nil:
        return "()";
    case DataValue::Kind::String:
        return "\"" + v.text + "\"";
    case DataValue::Kind::List: {
        std::string out = "(";
        for (std::size_t i = 0; i < v.items.size(); ++i) {
            if (i > 0) {
                out += v.dotted && i + 1 == v.items.size() ? " . " : " ";
            }
            out += writeDataValue(v.items[i]);
        }
        return out + ")";
    }
    default:
        return v.text;
    }
}

bool isAlist(const DataValue& v) {
    if (v.kind != DataValue::Kind::List || v.dotted || v.items.empty()) {
        return false;
    }
    for (const DataValue& entry : v.items) {
        if (entry.kind != DataValue::Kind::List || entry.items.empty()) {
            return false;
        }
        const DataValue& key = entry.items.front();
        if (key.kind != DataValue::Kind::Symbol && key.kind != DataValue::Kind::String) {
            return false;
        }
    }
    return true;
}

bool isCatalog(const DataValue& v) {
    if (v.kind != DataValue::Kind::List || v.dotted || v.items.empty()) {
        return false;
    }
    for (const DataValue& entry : v.items) {
        if (entry.kind != DataValue::Kind::List || entry.items.size() < 2) {
            return false;
        }
        const DataValue& id = entry.items.front();
        if (id.kind != DataValue::Kind::String && id.kind != DataValue::Kind::Symbol) {
            return false;
        }
        if (entry.dotted) {
            if (entry.items.size() != 2 || !isAlist(entry.items[1])) {
                return false;
            }
        } else {
            DataValue rest;
            rest.kind = DataValue::Kind::List;
            rest.items.assign(entry.items.begin() + 1, entry.items.end());
            if (!isAlist(rest)) {
                return false;
            }
        }
    }
    return true;
}

bool isAlistList(const DataValue& v) {
    if (v.kind != DataValue::Kind::List || v.dotted || v.items.empty()) {
        return false;
    }
    for (const DataValue& entry : v.items) {
        if (!isAlist(entry)) {
            return false;
        }
    }
    return true;
}

}
