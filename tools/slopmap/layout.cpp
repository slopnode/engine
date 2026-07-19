#include "layout.hpp"

#include <algorithm>

namespace slopmap {

UiLayout computeUiLayout(float menuHeight, float statusHeight, float leftWidth, float rightWidth) {
    UiLayout layout;
    layout.menuHeight = menuHeight;
    layout.statusHeight = statusHeight;
    layout.leftWidth = leftWidth;
    layout.rightWidth = rightWidth;

    const float screenW = static_cast<float>(GetScreenWidth());
    const float screenH = static_cast<float>(GetScreenHeight());

    float contentW = screenW - leftWidth - rightWidth;
    float contentH = screenH - menuHeight - statusHeight;
    if (contentW < 64.0f) {
        contentW = 64.0f;
    }
    if (contentH < 64.0f) {
        contentH = 64.0f;
    }

    layout.content = {leftWidth, menuHeight, contentW, contentH};
    layout.leftPanel = {0.0f, menuHeight, leftWidth, contentH};
    layout.rightPanel = {leftWidth + contentW, menuHeight, rightWidth, contentH};
    return layout;
}

bool pointInRect(Vector2 point, Rectangle rect) {
    return CheckCollisionPointRec(point, rect);
}

bool ensureContentTarget(RenderTexture2D& target, Rectangle content) {
    const int width = std::max(1, static_cast<int>(content.width));
    const int height = std::max(1, static_cast<int>(content.height));
    if (target.id != 0 && target.texture.width == width && target.texture.height == height) {
        return false;
    }
    if (target.id != 0) {
        UnloadRenderTexture(target);
        target = {};
    }
    target = LoadRenderTexture(width, height);
    return true;
}

void drawContentTarget(const RenderTexture2D& target, Rectangle content) {
    if (target.id == 0) {
        return;
    }
    const Rectangle source{
        0.0f,
        0.0f,
        static_cast<float>(target.texture.width),
        -static_cast<float>(target.texture.height),
    };
    DrawTexturePro(target.texture, source, content, {0.0f, 0.0f}, 0.0f, WHITE);
}

}
