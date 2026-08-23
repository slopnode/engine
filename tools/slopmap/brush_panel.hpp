#pragma once

#include "editor.hpp"
#include "sound_browser.hpp"

#include "assets/asset_store.hpp"

namespace slopmap {

struct BrushPanelResult {
    bool changed = false;
};

struct BrushPanel {
    BrushPanelResult drawSection(
        Editor& editor,
        slopengine::AssetStore& assets,
        SoundBrowser& soundBrowser,
        float bodyHeight);
};

/** On use / on touch for selected face(s); for Surface tab. */
bool drawFaceHandlerSection(Editor& editor);

}
