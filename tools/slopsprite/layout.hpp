#pragma once

#include <raylib.h>

namespace slopsprite {

struct UiLayout {
    float menuHeight = 0.0f;
    float statusHeight = 0.0f;
    float animHeight = 0.0f;
    float leftWidth = 280.0f;
    float rightWidth = 320.0f;
    Rectangle content{0.0f, 0.0f, 1.0f, 1.0f};
    Rectangle leftPanel{0.0f, 0.0f, 1.0f, 1.0f};
    Rectangle rightPanel{0.0f, 0.0f, 1.0f, 1.0f};
    Rectangle animPanel{0.0f, 0.0f, 1.0f, 1.0f};
};

UiLayout computeUiLayout(
    float menuHeight,
    float statusHeight,
    float animHeight,
    float leftWidth = 280.0f,
    float rightWidth = 320.0f);

bool pointInRect(Vector2 point, Rectangle rect);
bool ensureContentTarget(RenderTexture2D& target, Rectangle content);
void drawContentTarget(const RenderTexture2D& target, Rectangle content);

}
