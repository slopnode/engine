#include "layout.hpp"

namespace slopaudio {

UiLayout computeUiLayout(
    float menuHeight,
    float tabBarHeight,
    float statusHeight,
    float leftWidth,
    float rightWidth) {
    UiLayout layout;
    layout.menuHeight = menuHeight;
    layout.tabBarHeight = tabBarHeight;
    layout.statusHeight = statusHeight;
    layout.leftWidth = leftWidth;
    layout.rightWidth = rightWidth;

    const float screenW = static_cast<float>(GetScreenWidth());
    const float screenH = static_cast<float>(GetScreenHeight());
    const float top = menuHeight + tabBarHeight;

    float contentW = screenW - leftWidth - rightWidth;
    float contentH = screenH - top - statusHeight;
    if (contentW < 64.0f) {
        contentW = 64.0f;
    }
    if (contentH < 64.0f) {
        contentH = 64.0f;
    }

    layout.tabBar = {0.0f, menuHeight, screenW, tabBarHeight};
    layout.content = {leftWidth, top, contentW, contentH};
    layout.leftPanel = {0.0f, top, leftWidth, contentH};
    layout.rightPanel = {leftWidth + contentW, top, rightWidth, contentH};
    layout.fullPanel = {0.0f, top, leftWidth + contentW + rightWidth, contentH};
    return layout;
}

bool pointInRect(Vector2 point, Rectangle rect) {
    return CheckCollisionPointRec(point, rect);
}

}
