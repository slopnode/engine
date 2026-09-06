#include "layout.hpp"

namespace slopaudio {

UiLayout computeUiLayout(
    float menuHeight,
    float tabBarHeight,
    float statusHeight,
    float transportHeight,
    float leftWidth,
    float rightWidth) {
    UiLayout layout;
    layout.menuHeight = menuHeight;
    layout.tabBarHeight = tabBarHeight;
    layout.statusHeight = statusHeight;
    layout.transportHeight = transportHeight;
    layout.leftWidth = leftWidth;
    layout.rightWidth = rightWidth;

    const float screenW = static_cast<float>(GetScreenWidth());
    const float screenH = static_cast<float>(GetScreenHeight());
    const float top = menuHeight + tabBarHeight;

    float contentW = screenW - leftWidth - rightWidth;
    float contentH = screenH - top - statusHeight - transportHeight;
    if (contentW < 64.0f) {
        contentW = 64.0f;
    }
    if (contentH < 64.0f) {
        contentH = 64.0f;
    }

    layout.tabBar = {0.0f, menuHeight, screenW, tabBarHeight};
    layout.content = {leftWidth, top, contentW, contentH};
    layout.leftPanel = {0.0f, top, leftWidth, contentH + transportHeight};
    layout.rightPanel = {leftWidth + contentW, top, rightWidth, contentH + transportHeight};
    layout.transportPanel = {leftWidth, top + contentH, contentW, transportHeight};
    layout.fullPanel = {0.0f, top, leftWidth + contentW + rightWidth, contentH + transportHeight};
    return layout;
}

bool pointInRect(Vector2 point, Rectangle rect) {
    return CheckCollisionPointRec(point, rect);
}

}
