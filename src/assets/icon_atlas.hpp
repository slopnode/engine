#pragma once

#include <raylib.h>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace slopengine {

/** Packed UI icon atlas (.png + .iconmap). */
struct IconAtlas {
    Texture2D texture{};
    int width = 0;
    int height = 0;
    std::string atlasPath;
    std::unordered_map<std::string, Rectangle> rects; /**< Icon id → source rect. */
};

/** Parses .iconmap text into @p atlas (rects only; texture loaded separately). */
bool parseIconMap(std::string_view source, IconAtlas& atlas);

/** Unloads the atlas GPU texture. */
void unloadIconAtlas(IconAtlas& atlas);

/** Returns the source rect for icon @p id when present. */
std::optional<Rectangle> findIconRect(const IconAtlas& atlas, std::string_view id);

}
