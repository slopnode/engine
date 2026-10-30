#include "world_preview.hpp"

#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>

namespace slopmodel {

void WorldPreview::draw(
    Editor& editor,
    const ShadedDraw& shaded,
    RenderTexture2D& target,
    Rectangle contentRect,
    bool allowInput) {
    if (editor.requestWorldCameraFrame) {
        framePending = true;
        editor.requestWorldCameraFrame = false;
    }
    if (autoOrbit) {
        constexpr float kPi = 3.14159265358979323846f;
        camera.yaw += autoOrbitSpeedDeg * DEG2RAD * GetFrameTime();
        while (camera.yaw > kPi) {
            camera.yaw -= 2.0f * kPi;
        }
        while (camera.yaw < -kPi) {
            camera.yaw += 2.0f * kPi;
        }
    }
    camera.update(allowInput);

    if (editor.doc.open && framePending) {
        const BoundingBox bounds = GetModelBoundingBox(editor.doc.model);
        const Vector3 center = Vector3Scale(
            Vector3Scale(Vector3Add(bounds.min, bounds.max), 0.5f), editor.doc.worldScale);
        const float radius = std::max(
            0.1f,
            Vector3Length(Vector3Subtract(bounds.max, bounds.min)) * 0.5f * editor.doc.worldScale);
        camera.frameBounds(center, radius);
        framePending = false;
    }

    BeginTextureMode(target);
    ClearBackground(Color{30, 32, 38, 255});

    const Camera3D rayCam = camera.toRaylib();
    SceneDraw scene{};
    scene.editor = &editor;
    scene.camera = &rayCam;
    scene.screenSize = {
        static_cast<float>(target.texture.width), static_cast<float>(target.texture.height)};

    BeginMode3D(rayCam);
    DrawGrid(20, 0.5f);
    DrawLine3D({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, RED);
    DrawLine3D({0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, GREEN);
    DrawLine3D({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, BLUE);
    const Matrix modelMatrix =
        MatrixScale(editor.doc.worldScale, editor.doc.worldScale, editor.doc.worldScale);
    drawEditorScene(editor, modelMatrix, &shaded, scene);
    EndMode3D();

    drawSceneLabels(scene);
    EndTextureMode();

    if (allowInput && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !scene.markers.empty()) {
        const Vector2 mouse = GetMousePosition();
        const Vector2 local{mouse.x - contentRect.x, mouse.y - contentRect.y};
        int best = -1;
        float bestDistance = 14.0f;
        for (int i = 0; i < static_cast<int>(scene.markers.size()); ++i) {
            const float distance = Vector2Distance(local, scene.markers[static_cast<std::size_t>(i)].screen);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = i;
            }
        }
        if (best >= 0) {
            editor.doc.selectedAttachment = best;
        }
    }
}

}
