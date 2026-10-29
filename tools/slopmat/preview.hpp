#pragma once

#include "editor.hpp"
#include "world_preview.hpp"

#include "assets/asset_store.hpp"

namespace slopmat {

struct PreviewView {
    float zoom = 4.0f;
    bool tile = false;
    bool world = false;
    bool playing = true;
    float playTime = 0.0f;
    float texelSize = 64.0f;
    int slot = 0;
    WorldPreview worldPreview;
};

void drawPreviewPanel(PreviewView& view, Editor& editor, slopengine::AssetStore& assets);

}
