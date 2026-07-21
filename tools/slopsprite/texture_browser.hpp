#pragma once

#include "assets/asset_store.hpp"

#include <string>
#include <vector>

namespace slopsprite {

struct TextureBrowser {
    std::vector<std::string> textures;
    std::string filter;
    bool open = false;

    void rescan(const slopengine::AssetStore& assets);
    /** Returns true and writes @p outPath when the user picks a texture. */
    bool drawModal(slopengine::AssetStore& assets, std::string& outPath);
};

}
