#pragma once

#include "assets/asset_store.hpp"
#include "editor.hpp"

namespace slopmap {

struct TexturePanelResult {
    bool changed = false;
};

struct TexturePanel {
    bool lockAspect = true;

    TexturePanelResult drawSection(
        Editor& editor,
        slopengine::AssetStore& assets,
        float bodyHeight);
};

}
