#pragma once

#include "editor.hpp"

namespace slopmap {

struct ThingPanelResult {
    bool changed = false;
};

struct ThingPanel {
    ThingPanelResult drawSection(Editor& editor, float bodyHeight);
};

}
