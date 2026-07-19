#pragma once

#include "assets/asset_store.hpp"
#include "editor.hpp"

#include <string>
#include <vector>

namespace slopmap {

struct PrefabBrowserResult {
    bool selected = false;
    bool requestRescan = false;
    bool openRequested = false;
};

struct PrefabBrowser {
    std::vector<std::string> prefabs;
    char filter[128] = {};

    void rescan(const slopengine::AssetStore& assets);
    PrefabBrowserResult drawSection(
        Editor& editor,
        slopengine::AssetStore& assets,
        float bodyHeight);
};

}
