#pragma once

#include "editor.hpp"

#include <raylib.h>

namespace slopmap {

struct PlaceTool {
    void update(
        Editor& editor,
        slopengine::AssetStore& assets,
        const Camera3D& camera,
        bool uiWantsMouse,
        bool uiWantsKeyboard);
};

}
