#include "map/map_meta.hpp"

#include <cctype>
#include <optional>
#include <string>

namespace slopengine {

namespace {

std::string_view trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

std::optional<std::string> readQuoted(std::string_view text, std::size_t& cursor) {
    while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor]))) {
        ++cursor;
    }
    if (cursor >= text.size() || text[cursor] != '"') {
        return std::nullopt;
    }
    ++cursor;
    const std::size_t start = cursor;
    while (cursor < text.size() && text[cursor] != '"') {
        ++cursor;
    }
    if (cursor >= text.size()) {
        return std::nullopt;
    }
    std::string value{text.substr(start, cursor - start)};
    ++cursor;
    return value;
}

std::optional<std::string> readQuotedField(std::string_view line, std::string_view prefix) {
    if (line.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }
    std::size_t cursor = prefix.size();
    return readQuoted(line, cursor);
}

bool readDepends(std::string_view line, std::vector<std::string>& out) {
    constexpr std::string_view kPrefix = "(depends";
    if (line.rfind(kPrefix, 0) != 0) {
        return false;
    }
    std::size_t cursor = kPrefix.size();
    while (cursor < line.size()) {
        while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor]))) {
            ++cursor;
        }
        if (cursor >= line.size() || line[cursor] == ')') {
            break;
        }
        auto value = readQuoted(line, cursor);
        if (!value) {
            return false;
        }
        out.push_back(*value);
    }
    return true;
}

} // namespace

bool parseMapMeta(std::string_view source, MapMeta& out) {
    out = {};
    std::size_t lineStart = 0;
    while (lineStart <= source.size()) {
        const std::size_t lineEnd = source.find('\n', lineStart);
        const std::string_view line = trim(source.substr(
            lineStart,
            lineEnd == std::string_view::npos ? std::string_view::npos : lineEnd - lineStart));

        if (lineEnd == std::string_view::npos && line.empty()) {
            break;
        }

        if (auto id = readQuotedField(line, "(id ")) {
            out.id = *id;
        } else if (auto name = readQuotedField(line, "(name ")) {
            out.name = *name;
        } else if (auto package = readQuotedField(line, "(package ")) {
            out.package = *package;
        } else if (line.rfind("(depends", 0) == 0) {
            if (!readDepends(line, out.depends)) {
                return false;
            }
        }

        if (lineEnd == std::string_view::npos) {
            break;
        }
        lineStart = lineEnd + 1;
    }

    return !out.id.empty() && !out.package.empty();
}

}
