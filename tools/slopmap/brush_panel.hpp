#pragma once

#include "editor.hpp"

namespace slopmap {

struct BrushPanelResult {
    bool changed = false;
};

struct BrushPanel {
    BrushPanelResult drawSection(Editor& editor, float bodyHeight);
};

/** On use / on touch for selected face(s); for Surface tab. */
bool drawFaceHandlerSection(Editor& editor);

}
