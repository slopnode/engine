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
    PrefabBrowserResult draw(Editor& editor, float posX, float posY, float width, float height);
};

}
