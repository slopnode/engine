#include "world_preview.hpp"

#include "rlImGui.h"

#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>

namespace slopmat {

namespace {

constexpr float kFloorSize = 6.0f;
constexpr float kWallHeight = 3.0f;

void texturedQuad(Texture2D texture, Vector3 a, Vector3 b, Vector3 c, Vector3 d, float uRepeat, float vRepeat, Color tint) {
    rlSetTexture(texture.id);
    rlBegin(RL_QUADS);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);
    rlTexCoord2f(0.0f, vRepeat);
    rlVertex3f(a.x, a.y, a.z);
    rlTexCoord2f(uRepeat, vRepeat);
    rlVertex3f(b.x, b.y, b.z);
    rlTexCoord2f(uRepeat, 0.0f);
    rlVertex3f(c.x, c.y, c.z);
    rlTexCoord2f(0.0f, 0.0f);
    rlVertex3f(d.x, d.y, d.z);
    rlEnd();
    rlSetTexture(0);
}

} // namespace

void WorldPreview::release() {
    if (target.id != 0) {
        UnloadRenderTexture(target);
        target = {};
    }
}

void WorldPreview::draw(Texture2D texture, float pixelsPerMeter, ImVec2 size) {
    const int w = std::max(16, static_cast<int>(size.x));
    const int h = std::max(16, static_cast<int>(size.y));
    if (target.id == 0 || target.texture.width != w || target.texture.height != h) {
        release();
        target = LoadRenderTexture(w, h);
    }

    Camera3D camera{};
    camera.target = {0.0f, 1.0f, -1.0f};
    camera.position = {
        camera.target.x + distance * std::cos(pitch) * std::sin(yaw),
        camera.target.y + distance * std::sin(pitch),
        camera.target.z + distance * std::cos(pitch) * std::cos(yaw),
    };
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.fovy = 50.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    const float half = kFloorSize * 0.5f;
    const float texW = texture.id != 0 ? static_cast<float>(texture.width) : 64.0f;
    const float texH = texture.id != 0 ? static_cast<float>(texture.height) : 64.0f;
    const float ppm = std::max(1.0f, pixelsPerMeter);
    const float floorU = kFloorSize * ppm / texW;
    const float floorV = kFloorSize * ppm / texH;
    const float wallU = kFloorSize * ppm / texW;
    const float wallV = kWallHeight * ppm / texH;

    BeginTextureMode(target);
    ClearBackground(Color{30, 32, 38, 255});
    BeginMode3D(camera);
    if (texture.id != 0) {
        texturedQuad(texture, {-half, 0.0f, half}, {half, 0.0f, half}, {half, 0.0f, -half}, {-half, 0.0f, -half}, floorU, floorV, Color{200, 200, 200, 255});
        texturedQuad(texture, {-half, 0.0f, -half}, {half, 0.0f, -half}, {half, kWallHeight, -half}, {-half, kWallHeight, -half}, wallU, wallV, WHITE);
    }
    if (showGrid) {
        const Color grid{255, 255, 255, 50};
        for (int i = 0; i <= static_cast<int>(kFloorSize); ++i) {
            const float t = -half + static_cast<float>(i);
            DrawLine3D({t, 0.002f, -half}, {t, 0.002f, half}, grid);
            DrawLine3D({-half, 0.002f, t}, {half, 0.002f, t}, grid);
            DrawLine3D({t, 0.0f, -half + 0.002f}, {t, kWallHeight, -half + 0.002f}, grid);
        }
        for (int i = 0; i <= static_cast<int>(kWallHeight); ++i) {
            const float y = static_cast<float>(i);
            DrawLine3D({-half, y, -half + 0.002f}, {half, y, -half + 0.002f}, grid);
        }
    }
    EndMode3D();
    EndTextureMode();

    rlImGuiImageRenderTextureFit(&target, true);
    if (ImGui::IsItemHovered()) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            yaw -= delta.x * 0.01f;
            pitch = std::clamp(pitch + delta.y * 0.01f, 0.05f, 1.4f);
        }
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            distance = std::clamp(distance * (wheel > 0.0f ? 0.9f : 1.1f), 2.0f, 20.0f);
        }
    }
}

}
