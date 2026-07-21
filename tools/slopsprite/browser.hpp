#pragma once

#include "assets/asset_store.hpp"
#include "editor.hpp"

#include <string>
#include <vector>

namespace slopsprite {

struct SpriteBrowserEntry {
    std::string virtualPath;
    std::string packageId;
};

struct SpriteBrowser {
    std::vector<SpriteBrowserEntry> entries;
    std::string filter;

    void rescan(const slopengine::AssetStore& assets);
    void draw(Editor& editor, slopengine::AssetStore& assets);
};

}
