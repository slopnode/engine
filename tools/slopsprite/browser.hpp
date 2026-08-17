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

struct SpritePicker {
    std::vector<std::string> sprites;
    std::string filter;
    bool open = false;

    void rescan(const slopengine::AssetStore& assets);
    bool drawModal(slopengine::AssetStore& assets, std::string& outPath);
};

}
