#pragma once

#include "camera.hpp"
#include "editor.hpp"

#include <raylib.h>

namespace slopsprite {

struct WorldPreview {
    OrbitCamera camera{};
    bool framePending = false;

    void draw(Editor& editor, RenderTexture2D& target, bool allowInput);
};

}
