#pragma once

#include <raylib.h>

namespace slopmat {

struct UiLayout {
    float menuHeight = 0.0f;
    float statusHeight = 0.0f;
    float leftWidth = 320.0f;
    float rightWidth = 420.0f;
    Rectangle content{0.0f, 0.0f, 1.0f, 1.0f};
    Rectangle leftPanel{0.0f, 0.0f, 1.0f, 1.0f};
    Rectangle rightPanel{0.0f, 0.0f, 1.0f, 1.0f};
};

UiLayout computeUiLayout(float menuHeight, float statusHeight, float leftWidth = 320.0f, float rightWidth = 420.0f);

}
