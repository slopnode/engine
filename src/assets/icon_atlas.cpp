#include "assets/icon_atlas.hpp"

#include <cctype>
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

std::optional<std::string> readQuoted(std::string_view& rest) {
    rest = trim(rest);
    if (rest.empty() || rest.front() != '"') {
        return std::nullopt;
    }
    const std::size_t end = rest.find('"', 1);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    std::string value{rest.substr(1, end - 1)};
    rest = trim(rest.substr(end + 1));
    return value;
}

bool readInt(std::string_view& rest, int& out) {
    rest = trim(rest);
    if (rest.empty()) {
        return false;
    }
    std::size_t index = 0;
    if (rest[index] == '+' || rest[index] == '-') {
        ++index;
    }
    if (index >= rest.size() || !std::isdigit(static_cast<unsigned char>(rest[index]))) {
        return false;
    }
    while (index < rest.size() && std::isdigit(static_cast<unsigned char>(rest[index]))) {
        ++index;
    }
    const std::string token{rest.substr(0, index)};
    out = std::stoi(token);
    rest = trim(rest.substr(index));
    return true;
}

} // namespace

bool parseIconMap(std::string_view source, IconAtlas& atlas) {
    atlas.rects.clear();
    atlas.atlasPath.clear();
    atlas.width = 0;
    atlas.height = 0;

    std::size_t lineStart = 0;
    while (lineStart <= source.size()) {
        const std::size_t lineEnd = source.find('\n', lineStart);
        std::string_view line = trim(source.substr(
            lineStart,
            lineEnd == std::string_view::npos ? std::string_view::npos : lineEnd - lineStart));

        if (lineEnd == std::string_view::npos && line.empty()) {
            break;
        }

        if (line.rfind("(atlas ", 0) == 0) {
            std::string_view rest = line.substr(std::string_view("(atlas ").size());
            auto path = readQuoted(rest);
            if (!path) {
                return false;
            }
            atlas.atlasPath = std::move(*path);
        } else if (line.rfind("(size ", 0) == 0) {
            std::string_view rest = line.substr(std::string_view("(size ").size());
            int w = 0;
            int h = 0;
            if (!readInt(rest, w) || !readInt(rest, h)) {
                return false;
            }
            atlas.width = w;
            atlas.height = h;
        } else if (line.rfind("(icon ", 0) == 0) {
            std::string_view rest = line.substr(std::string_view("(icon ").size());
            auto id = readQuoted(rest);
            if (!id) {
                return false;
            }
            int x = 0;
            int y = 0;
            int w = 0;
            int h = 0;
            if (!readInt(rest, x) || !readInt(rest, y) || !readInt(rest, w) || !readInt(rest, h)) {
                return false;
            }
            atlas.rects.emplace(
                std::move(*id),
                Rectangle{
                    static_cast<float>(x),
                    static_cast<float>(y),
                    static_cast<float>(w),
                    static_cast<float>(h),
                });
        }

        if (lineEnd == std::string_view::npos) {
            break;
        }
        lineStart = lineEnd + 1;
    }

    return !atlas.atlasPath.empty() && atlas.width > 0 && atlas.height > 0 && !atlas.rects.empty();
}

void unloadIconAtlas(IconAtlas& atlas) {
    if (atlas.texture.id != 0) {
        UnloadTexture(atlas.texture);
    }
    atlas = {};
}

std::optional<Rectangle> findIconRect(const IconAtlas& atlas, std::string_view id) {
    const auto it = atlas.rects.find(std::string{id});
    if (it == atlas.rects.end()) {
        return std::nullopt;
    }
    return it->second;
}

}
