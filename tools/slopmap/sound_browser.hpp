#pragma once

#include "assets/asset_store.hpp"

#include <string>
#include <vector>

namespace slopmap {

enum class SoundBrowserMode {
    Combined,
    DefinedOnly,
};

struct SoundBrowser {
    std::vector<std::string> definedSounds;
    std::vector<std::string> rawClips;
    std::string filter;
    bool open = false;
    SoundBrowserMode mode = SoundBrowserMode::Combined;

    void rescan(const slopengine::AssetStore& assets);
    bool drawModal(slopengine::AssetStore& assets, std::string& outPath);
};

}
