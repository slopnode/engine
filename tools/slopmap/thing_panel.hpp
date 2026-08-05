#pragma once

#include "editor.hpp"
#include "material_browser.hpp"

#include "assets/asset_store.hpp"

namespace slopmap {

struct ThingPanelResult {
    bool changed = false;
};

struct ThingPanel {
    ThingPanelResult drawSection(
        Editor& editor,
        slopengine::AssetStore& assets,
        MaterialBrowser& materialBrowser,
        float bodyHeight);
};

}
