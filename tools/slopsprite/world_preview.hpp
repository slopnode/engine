#pragma once

#include "assets/asset_store.hpp"
#include "camera.hpp"
#include "editor.hpp"

#include <raylib.h>

namespace slopsprite {

struct WorldPreview {
    OrbitCamera camera{};
    bool framePending = false;
    bool autoOrbit = false;
    float autoOrbitSpeedDeg = 45.0f;

    void draw(
        Editor& editor,
        slopengine::AssetStore& assets,
        RenderTexture2D& target,
        bool allowInput);
};

}
