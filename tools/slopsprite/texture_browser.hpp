#pragma once

#include "assets/asset_store.hpp"

#include <string>
#include <vector>

namespace slopsprite {

/** Which field a pick-texture button opened the shared browser for. */
enum class TextureBrowserTarget {
    Texture,
    HitMask,
    BrightMask,
};

struct TextureBrowser {
    std::vector<std::string> textures;
    std::string filter;
    bool open = false;
    TextureBrowserTarget target = TextureBrowserTarget::Texture;

    void rescan(const slopengine::AssetStore& assets);
    /** Returns true and writes @p outPath when the user picks a texture. */
    bool drawModal(slopengine::AssetStore& assets, std::string& outPath);
};

}
