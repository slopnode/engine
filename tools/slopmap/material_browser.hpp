#pragma once

#include "assets/asset_store.hpp"
#include "editor.hpp"

#include <string>
#include <vector>

namespace slopmap {

struct MaterialBrowserResult {
    bool applied = false;
    bool requestRescan = false;
};

struct MaterialBrowser {
    std::vector<std::string> materials;
    char filter[128] = {};

    void rescan(const slopengine::AssetStore& assets);
    MaterialBrowserResult draw(Editor& editor, float posX, float posY, float width, float height);
};

std::string selectionMaterialLabel(const EditorDocument& doc);
bool applyMaterialToSelection(Editor& editor, const std::string& materialPath);

}
