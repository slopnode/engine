#pragma once

#include "assets/asset_store.hpp"

#include <string>
#include <vector>

namespace slopsprite {

struct SoundBrowser {
    std::vector<std::string> sounds;
    std::string filter;
    bool open = false;

    void rescan(const slopengine::AssetStore& assets);
    bool drawModal(std::string& outPath);
};

}
