#pragma once

#include <raylib.h>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace slopengine {

struct IconAtlas {
    Texture2D texture{};
    int width = 0;
    int height = 0;
    std::string atlasPath;
    std::unordered_map<std::string, Rectangle> rects;
};

bool parseIconMap(std::string_view source, IconAtlas& atlas);

void unloadIconAtlas(IconAtlas& atlas);

std::optional<Rectangle> findIconRect(const IconAtlas& atlas, std::string_view id);

}
