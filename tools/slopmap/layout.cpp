#include "layout.hpp"

#include <algorithm>

namespace slopmap {

namespace {

constexpr float kViewportGap = 1.0f;

} // namespace

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

ContentViewports splitContentViewports(Rectangle content, ViewportLayout layout, int activeViewport) {
    ContentViewports result{};
    if (layout == ViewportLayout::Single) {
        result.count = 1;
        const int index = std::clamp(activeViewport, 0, kViewportCount - 1);
        result.rects[static_cast<std::size_t>(index)] = content;
        return result;
    }

    result.count = kViewportCount;
    const float halfW = (content.width - kViewportGap) * 0.5f;
    const float halfH = (content.height - kViewportGap) * 0.5f;
    const float rightX = content.x + halfW + kViewportGap;
    const float bottomY = content.y + halfH + kViewportGap;
    result.rects[0] = {content.x, content.y, halfW, halfH};
    result.rects[1] = {rightX, content.y, halfW, halfH};
    result.rects[2] = {content.x, bottomY, halfW, halfH};
    result.rects[3] = {rightX, bottomY, halfW, halfH};
    return result;
}

int hitTestContentViewport(Vector2 point, const ContentViewports& viewports) {
    if (viewports.count == 1) {
        for (int i = 0; i < kViewportCount; ++i) {
            const Rectangle& rect = viewports.rects[static_cast<std::size_t>(i)];
            if (rect.width > 0.0f && rect.height > 0.0f && pointInRect(point, rect)) {
                return i;
            }
        }
        return -1;
    }
    for (int i = 0; i < viewports.count; ++i) {
        if (pointInRect(point, viewports.rects[static_cast<std::size_t>(i)])) {
            return i;
        }
    }
    return -1;
}

void drawViewportChrome(const ContentViewports& viewports, int activeViewport) {
    if (viewports.count <= 1) {
        return;
    }
    for (int i = 0; i < viewports.count; ++i) {
        const Rectangle& rect = viewports.rects[static_cast<std::size_t>(i)];
        const bool active = i == activeViewport;
        const Color color = active ? Color{220, 180, 70, 255} : Color{18, 20, 24, 255};
        const float thickness = active ? 2.0f : 1.0f;
        DrawRectangleLinesEx(rect, thickness, color);
    }
}

}
