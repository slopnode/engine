#pragma once

#include "editor.hpp"

namespace slopmap {

struct BrushPanelResult {
    bool changed = false;
};

struct BrushPanel {
    BrushPanelResult drawSection(Editor& editor, float bodyHeight);
};

}
