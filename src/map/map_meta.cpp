#include "map/map_meta.hpp"

#include <cctype>
#include <charconv>
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

bool readFloats(std::string_view text, std::size_t count, float* out) {
    std::string_view value = text;
    for (std::size_t index = 0; index < count; ++index) {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
            value.remove_prefix(1);
        }
        if (value.empty()) {
            return false;
        }
        float parsed = 0.0f;
        const auto* begin = value.data();
        const auto* end = value.data() + value.size();
        const auto result = std::from_chars(begin, end, parsed);
        if (result.ec != std::errc{} || result.ptr == begin) {
            return false;
        }
        out[index] = parsed;
        value.remove_prefix(static_cast<std::size_t>(result.ptr - begin));
    }
    return true;
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
        } else if (line.rfind("(ambient ", 0) == 0) {
            float rgb[3] = {0.02f, 0.02f, 0.025f};
            if (!readFloats(line.substr(std::string_view("(ambient ").size()), 3, rgb)) {
                return false;
            }
            out.ambient = {rgb[0], rgb[1], rgb[2]};
        }

        if (lineEnd == std::string_view::npos) {
            break;
        }
        lineStart = lineEnd + 1;
    }

    return !out.id.empty() && !out.package.empty();
}

}
