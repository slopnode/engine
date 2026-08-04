#pragma once

#include "assets/asset_store.hpp"
#include "editor.hpp"
#include "editor_settings.hpp"
#include "material_thumb_atlas.hpp"

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
    bool skyMaterialsOnly = false;
    MaterialThumbAtlas thumbs;
    bool thumbsDirty = true;

    void rescan(const slopengine::AssetStore& assets);
    MaterialBrowserResult drawSection(
        Editor& editor,
        slopengine::AssetStore& assets,
        EditorSettings& settings,
        float bodyHeight);
};

std::string selectionMaterialLabel(const EditorDocument& doc);
bool applyMaterialToSelection(Editor& editor, const std::string& materialPath);

}
