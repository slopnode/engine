#pragma once

#include "assets/asset_store.hpp"

#include <string>
#include <vector>

namespace slopmap {

struct SoundBrowser {
    std::vector<std::string> sounds;
    std::string filter;
    bool open = false;

    void rescan(const slopengine::AssetStore& assets);
    bool drawModal(slopengine::AssetStore& assets, std::string& outPath);
};

}
