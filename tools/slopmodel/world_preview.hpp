#pragma once

#include "assets/asset_store.hpp"
#include "camera.hpp"
#include "editor.hpp"
#include "model_draw.hpp"

#include <raylib.h>

namespace slopmodel {

struct WorldPreview {
    OrbitCamera camera{};
    bool framePending = false;
    bool autoOrbit = false;
    float autoOrbitSpeedDeg = 30.0f;

    void draw(
        Editor& editor,
        const ShadedDraw& shaded,
        RenderTexture2D& target,
        Rectangle contentRect,
        bool allowInput);
};

}
