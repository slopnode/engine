#include "shader_doc.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
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

bool isDocLine(const std::string& line, std::string& text) {
    const std::string t = trim(line);
    if (t.rfind("///", 0) != 0) {
        return false;
    }
    text = t.substr(3);
    if (!text.empty() && text.front() == ' ') {
        text.erase(0, 1);
    }
    return true;
}

std::string joinDoc(const std::vector<std::string>& lines) {
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        if (i + 1 < lines.size()) {
            out.push_back('\n');
        }
    }
    return out;
}

void applyTags(const std::string& docText, ShaderDoc& doc, std::string& description) {
    std::istringstream in(docText);
    std::string line;
    std::vector<std::string> kept;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.rfind("@used-by ", 0) == 0) {
            doc.usedBy = trim(t.substr(9));
        } else if (t.rfind("@see ", 0) == 0) {
            std::istringstream names(t.substr(5));
            std::string name;
            while (names >> name) {
                doc.see.push_back(name);
            }
        } else {
            kept.push_back(line);
        }
    }
    while (!kept.empty() && trim(kept.back()).empty()) {
        kept.pop_back();
    }
    description = joinDoc(kept);
}

const std::regex& declRegex() {
    static const std::regex re(
        R"(^\s*(?:layout\s*\([^)]*\)\s*)?((?:flat\s+|smooth\s+|noperspective\s+)?(?:in|out|uniform))\s+([A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(\[[^\]]*\])?\s*;)");
    return re;
}

const std::regex& bufferRegex() {
    static const std::regex re(
        R"(^\s*layout\s*\(([^)]*)\)\s*((?:readonly|writeonly|coherent|restrict|volatile)\s+)*buffer\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{)");
    return re;
}

const std::regex& localSizeRegex() {
    static const std::regex re(R"(^\s*layout\s*\(\s*(local_size_[^)]*)\)\s*in\s*;)");
    return re;
}

std::string bindingOf(const std::string& layoutArgs) {
    const std::regex re(R"(binding\s*=\s*(\d+))");
    std::smatch m;
    if (std::regex_search(layoutArgs, m, re)) {
        return m[1];
    }
    return {};
}

void parseShader(ShaderDoc& doc) {
    std::istringstream in(doc.source);
    std::string line;
    std::vector<std::string> pending;
    bool pendingBlankAfter = false;
    bool haveFileDoc = false;
    int braceDepth = 0;
    int parenDepth = 0;

    auto flushPendingAsFileDoc = [&]() {
        if (!pending.empty() && !haveFileDoc) {
            applyTags(joinDoc(pending), doc, doc.description);
            haveFileDoc = true;
        }
        pending.clear();
        pendingBlankAfter = false;
    };

    while (std::getline(in, line)) {
        std::string docText;
        if (braceDepth == 0 && parenDepth == 0 && isDocLine(line, docText)) {
            if (pendingBlankAfter) {
                flushPendingAsFileDoc();
            }
            pending.push_back(docText);
            continue;
        }
        const std::string t = trim(line);
        if (t.empty()) {
            if (!pending.empty()) {
                pendingBlankAfter = true;
            }
            continue;
        }
        if (braceDepth == 0 && parenDepth == 0) {
            std::smatch m;
            if (t.rfind("#version", 0) == 0) {
                doc.version = trim(t.substr(8));
                flushPendingAsFileDoc();
            } else if (t.rfind("#include", 0) == 0) {
                const std::size_t a = t.find('"');
                const std::size_t b = t.rfind('"');
                if (a != std::string::npos && b > a) {
                    doc.includes.push_back(t.substr(a + 1, b - a - 1));
                }
                flushPendingAsFileDoc();
            } else if (std::regex_search(line, m, localSizeRegex())) {
                doc.localSize = m[1];
                flushPendingAsFileDoc();
            } else if (std::regex_search(line, m, bufferRegex())) {
                ShaderBuffer buffer;
                buffer.binding = bindingOf(m[1]);
                buffer.access = trim(m[2]);
                buffer.name = m[3];
                if (!pendingBlankAfter) {
                    buffer.doc = joinDoc(pending);
                    pending.clear();
                } else {
                    flushPendingAsFileDoc();
                }
                doc.buffers.push_back(std::move(buffer));
            } else if (std::regex_search(line, m, declRegex())) {
                ShaderDecl decl;
                std::string qualifier = m[1];
                decl.type = m[2];
                decl.name = m[3];
                if (m[4].matched) {
                    decl.name += std::string(m[4]);
                }
                if (!pendingBlankAfter) {
                    decl.doc = joinDoc(pending);
                    pending.clear();
                } else {
                    flushPendingAsFileDoc();
                }
                qualifier = trim(qualifier);
                const std::size_t space = qualifier.rfind(' ');
                const std::string kind = space == std::string::npos ? qualifier : qualifier.substr(space + 1);
                decl.qualifier = space == std::string::npos ? std::string() : trim(qualifier.substr(0, space));
                if (kind == "uniform") {
                    doc.uniforms.push_back(std::move(decl));
                } else if (kind == "in") {
                    doc.inputs.push_back(std::move(decl));
                } else {
                    doc.outputs.push_back(std::move(decl));
                }
            } else {
                flushPendingAsFileDoc();
            }
        }
        bool inLineComment = false;
        for (std::size_t i = 0; i < line.size() && !inLineComment; ++i) {
            const char c = line[i];
            if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
                inLineComment = true;
            } else if (c == '{') {
                ++braceDepth;
            } else if (c == '}') {
                braceDepth = std::max(0, braceDepth - 1);
            } else if (c == '(') {
                ++parenDepth;
            } else if (c == ')') {
                parenDepth = std::max(0, parenDepth - 1);
            }
        }
    }
    flushPendingAsFileDoc();
}

}

std::string shaderStageFromPath(const std::string& path) {
    auto endsWith = [&](const char* suffix) {
        const std::string s(suffix);
        return path.size() >= s.size() && path.compare(path.size() - s.size(), s.size(), s) == 0;
    };
    if (endsWith("_vert")) {
        return "vertex";
    }
    if (endsWith("_frag")) {
        return "fragment";
    }
    if (endsWith("_comp")) {
        return "compute";
    }
    return "include";
}

std::vector<ShaderDoc> scanShaders(const fs::path& packageRoot) {
    std::vector<ShaderDoc> out;
    const fs::path root = packageRoot / "shaders";
    if (!fs::is_directory(root)) {
        return out;
    }
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".glsl") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& file : files) {
        ShaderDoc doc;
        doc.file = file;
        fs::path rel = fs::relative(file, root);
        rel.replace_extension();
        doc.path = rel.generic_string();
        doc.stage = shaderStageFromPath(doc.path);
        std::ifstream in(file, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        doc.source = buffer.str();
        parseShader(doc);
        out.push_back(std::move(doc));
    }
    return out;
}

}
