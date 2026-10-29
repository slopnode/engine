#include "highlight.hpp"

#include <cctype>
#include <unordered_set>

namespace slopdocs {

std::string escapeHtml(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

namespace {

const std::unordered_set<std::string_view>& specialForms() {
    static const std::unordered_set<std::string_view> forms = {
        "define", "define*", "define-macro", "define-constant", "lambda", "lambda*", "let", "let*",
        "letrec", "letrec*", "if", "cond", "case", "when", "unless", "set!", "begin", "do",
        "quote", "quasiquote", "unquote", "and", "or", "else", "require", "require-data",
        "define-screen", "page", "section",
    };
    return forms;
}

bool isDelimiter(char c) {
    return std::isspace(static_cast<unsigned char>(c)) || c == '(' || c == ')' || c == '[' ||
           c == ']' || c == '"' || c == ';' || c == '\'' || c == '`' || c == ',';
}

bool looksNumeric(std::string_view token) {
    if (token.empty()) {
        return false;
    }
    std::size_t i = 0;
    if (token[0] == '-' || token[0] == '+') {
        i = 1;
    }
    if (i >= token.size()) {
        return false;
    }
    bool digit = false;
    for (; i < token.size(); ++i) {
        const char c = token[i];
        if (std::isdigit(static_cast<unsigned char>(c))) {
            digit = true;
            continue;
        }
        if (c == '.' || c == 'e' || c == 'E' || c == '/') {
            continue;
        }
        return false;
    }
    return digit;
}

void span(std::string& out, const char* cls, std::string_view text) {
    out += "<span class=\"sd-tok-";
    out += cls;
    out += "\">";
    out += escapeHtml(text);
    out += "</span>";
}

}

namespace {

const std::unordered_set<std::string_view>& glslKeywords() {
    static const std::unordered_set<std::string_view> words = {
        "in", "out", "inout", "uniform", "layout", "const", "void", "if", "else", "for", "while",
        "do", "return", "discard", "break", "continue", "struct", "buffer", "readonly", "writeonly",
        "coherent", "restrict", "volatile", "flat", "smooth", "noperspective", "switch", "case",
        "default", "true", "false", "shared", "precision", "highp", "mediump", "lowp",
    };
    return words;
}

const std::unordered_set<std::string_view>& glslTypes() {
    static const std::unordered_set<std::string_view> words = {
        "float", "int", "uint", "bool", "double", "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4",
        "uvec2", "uvec3", "uvec4", "bvec2", "bvec3", "bvec4", "mat2", "mat3", "mat4", "mat2x2",
        "mat3x3", "mat4x4", "sampler2D", "sampler3D", "samplerCube", "sampler2DArray",
        "sampler2DShadow", "image2D", "atomic_uint",
    };
    return words;
}

bool isIdentStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

}

std::string highlightGlsl(std::string_view src) {
    std::string out;
    out.reserve(src.size() * 2);
    std::size_t i = 0;
    while (i < src.size()) {
        const char c = src[i];
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
            std::size_t end = src.find('\n', i);
            if (end == std::string_view::npos) {
                end = src.size();
            }
            const bool doc = i + 2 < src.size() && src[i + 2] == '/';
            span(out, doc ? "doc" : "comment", src.substr(i, end - i));
            i = end;
            continue;
        }
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
            std::size_t end = src.find("*/", i + 2);
            end = end == std::string_view::npos ? src.size() : end + 2;
            span(out, "comment", src.substr(i, end - i));
            i = end;
            continue;
        }
        if (c == '#') {
            std::size_t lineStart = src.rfind('\n', i);
            lineStart = lineStart == std::string_view::npos ? 0 : lineStart + 1;
            if (src.substr(lineStart, i - lineStart).find_first_not_of(" \t") == std::string_view::npos) {
                std::size_t end = src.find('\n', i);
                if (end == std::string_view::npos) {
                    end = src.size();
                }
                span(out, "preprocessor", src.substr(i, end - i));
                i = end;
                continue;
            }
        }
        if (c == '"') {
            std::size_t j = src.find('"', i + 1);
            j = j == std::string_view::npos ? src.size() : j + 1;
            span(out, "string", src.substr(i, j - i));
            i = j;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && i + 1 < src.size() && std::isdigit(static_cast<unsigned char>(src[i + 1])))) {
            std::size_t j = i;
            while (j < src.size() && (std::isalnum(static_cast<unsigned char>(src[j])) || src[j] == '.')) {
                ++j;
            }
            span(out, "number", src.substr(i, j - i));
            i = j;
            continue;
        }
        if (isIdentStart(c)) {
            std::size_t j = i;
            while (j < src.size() && isIdentChar(src[j])) {
                ++j;
            }
            const std::string_view word = src.substr(i, j - i);
            std::size_t k = j;
            while (k < src.size() && (src[k] == ' ' || src[k] == '\t')) {
                ++k;
            }
            if (glslKeywords().count(word) > 0) {
                span(out, "special", word);
            } else if (glslTypes().count(word) > 0) {
                span(out, "type", word);
            } else if (k < src.size() && src[k] == '(') {
                span(out, "call", word);
            } else {
                span(out, "symbol", word);
            }
            i = j;
            continue;
        }
        if (c == '{' || c == '}' || c == '(' || c == ')' || c == '[' || c == ']' || c == ';' || c == ',') {
            span(out, "paren", src.substr(i, 1));
            ++i;
            continue;
        }
        out += escapeHtml(src.substr(i, 1));
        ++i;
    }
    return out;
}

std::string highlightScheme(std::string_view src, const SymbolLinker& linker) {
    std::string out;
    out.reserve(src.size() * 2);
    bool headPosition = false;
    std::size_t i = 0;
    while (i < src.size()) {
        const char c = src[i];
        if (c == ';') {
            std::size_t end = src.find('\n', i);
            if (end == std::string_view::npos) {
                end = src.size();
            }
            span(out, "comment", src.substr(i, end - i));
            i = end;
            continue;
        }
        if (c == '"') {
            std::size_t j = i + 1;
            while (j < src.size()) {
                if (src[j] == '\\') {
                    j += 2;
                    continue;
                }
                if (src[j] == '"') {
                    ++j;
                    break;
                }
                ++j;
            }
            if (j > src.size()) {
                j = src.size();
            }
            span(out, "string", src.substr(i, j - i));
            i = j;
            headPosition = false;
            continue;
        }
        if (c == '(' || c == '[') {
            span(out, "paren", src.substr(i, 1));
            headPosition = true;
            ++i;
            continue;
        }
        if (c == ')' || c == ']') {
            span(out, "paren", src.substr(i, 1));
            headPosition = false;
            ++i;
            continue;
        }
        if (c == '\'' || c == '`' || c == ',') {
            std::size_t j = i + 1;
            if (c == ',' && j < src.size() && src[j] == '@') {
                ++j;
            }
            span(out, "quote", src.substr(i, j - i));
            i = j;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            out.push_back(c);
            ++i;
            continue;
        }
        if (c == '#' && i + 1 < src.size() && src[i + 1] == '\\') {
            std::size_t j = i + 2;
            if (j < src.size()) {
                ++j;
            }
            while (j < src.size() && !isDelimiter(src[j])) {
                ++j;
            }
            span(out, "char", src.substr(i, j - i));
            i = j;
            headPosition = false;
            continue;
        }
        std::size_t j = i;
        while (j < src.size() && !isDelimiter(src[j])) {
            ++j;
        }
        const std::string_view token = src.substr(i, j - i);
        i = j;
        if (token == "#t" || token == "#f" || token == "#true" || token == "#false") {
            span(out, "boolean", token);
        } else if (looksNumeric(token)) {
            span(out, "number", token);
        } else if (token.front() == ':' || token.back() == ':') {
            span(out, "keyword", token);
        } else if (specialForms().count(token) > 0) {
            span(out, "special", token);
        } else {
            const std::string href = linker ? linker(token) : std::string();
            if (!href.empty()) {
                out += "<a class=\"sd-tok-link\" href=\"";
                out += escapeHtml(href);
                out += "\">";
                span(out, headPosition ? "call" : "symbol", token);
                out += "</a>";
            } else {
                span(out, headPosition ? "call" : "symbol", token);
            }
        }
        headPosition = false;
    }
    return out;
}

}
