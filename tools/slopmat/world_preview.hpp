#pragma once

#include <raylib.h>

#include "imgui.h"

namespace slopmat {

/** A floor and a wall drawn with the preview texture repeated at the material's texel density. */
struct WorldPreview {
    RenderTexture2D target{};
    float yaw = 0.7f;
    float pitch = 0.4f;
    float distance = 7.0f;
    bool showGrid = true;

    void draw(Texture2D texture, float pixelsPerMeter, ImVec2 size);
    void release();
};

}
