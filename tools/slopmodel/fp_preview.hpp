#pragma once

#include "assets/asset_store.hpp"
#include "editor.hpp"
#include "model_draw.hpp"

#include <raylib.h>

namespace slopmodel {

/** The view-model pass as the engine draws it: eye camera at the origin, mirrored, 75 degree fov. */
struct FpPreview {
    float fovy = 75.0f;
    float eyeHeight = 1.7f;

    void draw(
        Editor& editor,
        const ShadedDraw& shaded,
        RenderTexture2D& target,
        Rectangle contentRect,
        bool allowInput);
};

}
