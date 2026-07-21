#pragma once

#include "editor.hpp"

#include <raylib.h>

namespace slopsprite {

struct AlignPreview {
    void draw(Editor& editor, RenderTexture2D& target, Rectangle contentRect, bool allowInput);
};

}
