#pragma once

#include "assets/asset_store.hpp"
#include "editor.hpp"

#include <raylib.h>

namespace slopsprite {

struct FpPreview {
    void draw(
        Editor& editor,
        slopengine::AssetStore& assets,
        RenderTexture2D& target,
        Rectangle contentRect,
        bool allowInput);
};

}
