#pragma once

#include "editor.hpp"

#include "assets/asset_store.hpp"

namespace slopmap {

struct ThingPanelResult {
    bool changed = false;
};

struct ThingPanel {
    ThingPanelResult drawSection(
        Editor& editor, slopengine::AssetStore& assets, float bodyHeight);
};

}
