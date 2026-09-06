#include "layout.hpp"

namespace slopaudio {

UiLayout computeUiLayout(
    float menuHeight,
    float statusHeight,
    float transportHeight,
    float leftWidth,
    float rightWidth) {
    UiLayout layout;
    layout.menuHeight = menuHeight;
    layout.statusHeight = statusHeight;
    layout.transportHeight = transportHeight;
    layout.leftWidth = leftWidth;
    layout.rightWidth = rightWidth;

    const float screenW = static_cast<float>(GetScreenWidth());
    const float screenH = static_cast<float>(GetScreenHeight());

    float contentW = screenW - leftWidth - rightWidth;
    float contentH = screenH - menuHeight - statusHeight - transportHeight;
    if (contentW < 64.0f) {
        contentW = 64.0f;
    }
    if (contentH < 64.0f) {
        contentH = 64.0f;
    }

    layout.content = {leftWidth, menuHeight, contentW, contentH};
    layout.leftPanel = {0.0f, menuHeight, leftWidth, contentH + transportHeight};
    layout.rightPanel = {leftWidth + contentW, menuHeight, rightWidth, contentH + transportHeight};
    layout.transportPanel = {leftWidth, menuHeight + contentH, contentW, transportHeight};
    return layout;
}

bool pointInRect(Vector2 point, Rectangle rect) {
    return CheckCollisionPointRec(point, rect);
}

}
