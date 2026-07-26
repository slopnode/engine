#pragma once

#include "camera.hpp"

#include <array>
#include <raylib.h>

namespace slopmap {

struct UiLayout {
    float menuHeight = 0.0f;
    float statusHeight = 0.0f;
    float leftWidth = 300.0f;
    float rightWidth = 320.0f;
    Rectangle content{0.0f, 0.0f, 1.0f, 1.0f};
    Rectangle leftPanel{0.0f, 0.0f, 1.0f, 1.0f};
    Rectangle rightPanel{0.0f, 0.0f, 1.0f, 1.0f};
};

struct ContentViewports {
    int count = 1;
    std::array<Rectangle, kViewportCount> rects{};
};

UiLayout computeUiLayout(float menuHeight, float statusHeight, float leftWidth = 300.0f, float rightWidth = 320.0f);

bool pointInRect(Vector2 point, Rectangle rect);
bool ensureContentTarget(RenderTexture2D& target, Rectangle content);
void drawContentTarget(const RenderTexture2D& target, Rectangle content);

ContentViewports splitContentViewports(Rectangle content, ViewportLayout layout, int activeViewport);
int hitTestContentViewport(Vector2 point, const ContentViewports& viewports);
void drawViewportChrome(const ContentViewports& viewports, int activeViewport);

}
