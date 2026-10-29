#pragma once

#include "editor.hpp"

#include "assets/asset_store.hpp"

namespace slopmap {

struct PathPanelResult {
    bool changed = false;
};

struct PathPanel {
    PathPanelResult drawSection(Editor& editor, slopengine::AssetStore& assets, float bodyHeight);
    PathPanelResult drawKnotSection(Editor& editor, float bodyHeight);
};

void updatePathPreview(Editor& editor);

}
