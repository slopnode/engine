#pragma once

#include <raylib.h>

namespace slopaudio {

struct UiLayout {
    float menuHeight = 0.0f;
    float tabBarHeight = 0.0f;
    float statusHeight = 0.0f;
    float transportHeight = 0.0f;
    float leftWidth = 320.0f;
    float rightWidth = 380.0f;
    Rectangle tabBar{0.0f, 0.0f, 1.0f, 1.0f};
    Rectangle content{0.0f, 0.0f, 1.0f, 1.0f};
    Rectangle leftPanel{0.0f, 0.0f, 1.0f, 1.0f};
    Rectangle rightPanel{0.0f, 0.0f, 1.0f, 1.0f};
    Rectangle transportPanel{0.0f, 0.0f, 1.0f, 1.0f};
    // Full remaining area below the top tab bar, spanning what leftPanel+content+rightPanel
    // together cover -- for single-panel tabs (e.g. Bus) that don't use the Sources layout.
    Rectangle fullPanel{0.0f, 0.0f, 1.0f, 1.0f};
};

UiLayout computeUiLayout(
    float menuHeight,
    float tabBarHeight,
    float statusHeight,
    float transportHeight,
    float leftWidth = 320.0f,
    float rightWidth = 380.0f);

bool pointInRect(Vector2 point, Rectangle rect);

}
