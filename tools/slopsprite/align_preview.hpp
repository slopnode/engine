#pragma once

#include "editor.hpp"

#include <raylib.h>

namespace slopsprite {

struct AlignPreview {
    bool draggingOrigin = false;

    void draw(Editor& editor, RenderTexture2D& target, Rectangle contentRect, bool allowInput);
};

}
