#include "fp_preview.hpp"

#include <raymath.h>
#include <rlgl.h>

#include <cmath>

namespace slopmodel {

void FpPreview::draw(
    Editor& editor,
    const ShadedDraw& shaded,
    RenderTexture2D& target,
    Rectangle contentRect,
    bool allowInput) {
    (void)contentRect;
    (void)allowInput;

    BeginTextureMode(target);
    ClearBackground(Color{30, 32, 38, 255});

    const float width = static_cast<float>(target.texture.width);
    const float height = static_cast<float>(target.texture.height);

    Camera3D worldCam{};
    worldCam.position = {0.0f, eyeHeight, 0.0f};
    worldCam.target = {0.0f, eyeHeight, 1.0f};
    worldCam.up = {0.0f, 1.0f, 0.0f};
    worldCam.fovy = fovy;
    worldCam.projection = CAMERA_PERSPECTIVE;

    BeginMode3D(worldCam);
    rlDisableDepthMask();
    const Color grid{255, 255, 255, 40};
    for (int i = -6; i <= 6; ++i) {
        const float t = static_cast<float>(i);
        DrawLine3D({t, 0.0f, -2.0f}, {t, 0.0f, 10.0f}, grid);
        DrawLine3D({-6.0f, 0.0f, t + 4.0f}, {6.0f, 0.0f, t + 4.0f}, grid);
    }
    for (int i = -6; i <= 6; ++i) {
        const float t = static_cast<float>(i);
        DrawLine3D({t, 0.0f, 10.0f}, {t, 4.0f, 10.0f}, grid);
        if (i >= 0 && i <= 4) {
            DrawLine3D({-6.0f, t, 10.0f}, {6.0f, t, 10.0f}, grid);
        }
    }
    rlEnableDepthMask();
    EndMode3D();

    Camera3D eyeCam{};
    eyeCam.position = {0.0f, 0.0f, 0.0f};
    eyeCam.target = {0.0f, 0.0f, 1.0f};
    eyeCam.up = {0.0f, 1.0f, 0.0f};
    eyeCam.fovy = fovy;
    eyeCam.projection = CAMERA_PERSPECTIVE;

    SceneDraw scene{};
    scene.editor = &editor;
    scene.camera = nullptr;
    scene.screenSize = {width, height};

    BeginMode3D(eyeCam);
    rlScalef(-1.0f, 1.0f, 1.0f);
    rlDisableBackfaceCulling();
    const Matrix placement = viewPlacementMatrix(editor.doc.geo.view);
    drawEditorScene(editor, placement, &shaded, scene);
    rlEnableBackfaceCulling();
    EndMode3D();

    const int cx = static_cast<int>(width * 0.5f);
    const int cy = static_cast<int>(height * 0.5f);
    DrawLine(cx - 8, cy, cx + 8, cy, Color{200, 200, 200, 160});
    DrawLine(cx, cy - 8, cx, cy + 8, Color{200, 200, 200, 160});
    EndTextureMode();
}

}
