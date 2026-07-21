#include "align_preview.hpp"

#include "render/sprite_billboard.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace slopsprite {

namespace {

slopengine::SpriteRotation* selectedRotation(EditorDocument& doc) {
    if (doc.selectedFrameIndex < 0 ||
        doc.selectedFrameIndex >= static_cast<int>(doc.asset.frames.size())) {
        return nullptr;
    }
    slopengine::SpriteFrame& frame =
        doc.asset.frames[static_cast<std::size_t>(doc.selectedFrameIndex)];
    if (doc.selectedRot < 0 || doc.selectedRot >= slopengine::kSpriteRotationCount) {
        return nullptr;
    }
    if (!frame.rotations[doc.selectedRot].has_value()) {
        return nullptr;
    }
    return &*frame.rotations[doc.selectedRot];
}

void ensureOffsetDefaults(slopengine::SpriteRotation& rot) {
    if (rot.hasOffset) {
        return;
    }
    const int w = rot.pixelWidth > 0 ? rot.pixelWidth : 1;
    const int h = rot.pixelHeight > 0 ? rot.pixelHeight : 1;
    rot.offsetX = w / 2;
    rot.offsetY = h;
    rot.hasOffset = true;
}

void drawFrameSample(
    const slopengine::ViewSpriteFrame& frame,
    float destX,
    float destY,
    float zoom,
    Color tint) {
    const float destW = static_cast<float>(frame.pixelWidth) * zoom;
    const float destH = static_cast<float>(frame.pixelHeight) * zoom;
    DrawTexturePro(
        *frame.texture,
        frame.source,
        {destX, destY, destW, destH},
        {0.0f, 0.0f},
        0.0f,
        tint);
}

} // namespace

void AlignPreview::draw(
    Editor& editor,
    RenderTexture2D& target,
    Rectangle contentRect,
    bool allowInput) {
    BeginTextureMode(target);
    ClearBackground(Color{22, 24, 28, 255});

    const float screenW = static_cast<float>(target.texture.width);
    const float screenH = static_cast<float>(target.texture.height);
    const float zoom = std::clamp(editor.doc.alignZoom, 0.25f, 16.0f);

    if (!editor.doc.open || editor.doc.atlasDirty) {
        DrawText("Open a sprite to align", 8, 8, 16, Color{200, 200, 200, 220});
        EndTextureMode();
        draggingOrigin = false;
        return;
    }

    if (editor.doc.selectedFrameIndex < 0 ||
        editor.doc.selectedFrameIndex >= static_cast<int>(editor.doc.asset.frames.size()) ||
        editor.doc.selectedRot < 0 ||
        editor.doc.selectedRot >= slopengine::kSpriteRotationCount ||
        !editor.doc.asset.frames[static_cast<std::size_t>(editor.doc.selectedFrameIndex)]
             .rotations[editor.doc.selectedRot]
             .has_value()) {
        DrawText("Selected rot has no texture", 8, 8, 16, Color{255, 160, 120, 220});
        EndTextureMode();
        draggingOrigin = false;
        return;
    }

    const auto primary = slopengine::resolveViewSpriteFrame(
        editor.doc.asset, editor.doc.atlas, editor.doc.currentFrame, editor.doc.selectedRot);
    if (!primary || primary->texture == nullptr) {
        DrawText("Selected rot has no texture", 8, 8, 16, Color{255, 160, 120, 220});
        EndTextureMode();
        draggingOrigin = false;
        return;
    }

    const float primaryW = static_cast<float>(primary->pixelWidth) * zoom;
    const float primaryH = static_cast<float>(primary->pixelHeight) * zoom;
    const float originPxX = static_cast<float>(primary->offsetX) * zoom;
    const float originPxY = static_cast<float>(primary->offsetY) * zoom;

    const float pivotScreenX = screenW * 0.5f;
    const float pivotScreenY = screenH * 0.55f;
    const float destX = pivotScreenX - originPxX;
    const float destY = pivotScreenY - originPxY;

    DrawLine(
        0, static_cast<int>(pivotScreenY), static_cast<int>(screenW), static_cast<int>(pivotScreenY),
        Color{60, 70, 90, 180});
    DrawLine(
        static_cast<int>(pivotScreenX),
        0,
        static_cast<int>(pivotScreenX),
        static_cast<int>(screenH),
        Color{60, 70, 90, 180});

    if (editor.doc.onionEnabled &&
        editor.doc.onionFrameIndex >= 0 &&
        editor.doc.onionFrameIndex < static_cast<int>(editor.doc.asset.frames.size())) {
        const std::string& onionFrameId =
            editor.doc.asset.frames[static_cast<std::size_t>(editor.doc.onionFrameIndex)].id;
        const auto onion = slopengine::resolveViewSpriteFrame(
            editor.doc.asset, editor.doc.atlas, onionFrameId, editor.doc.onionRot);
        if (onion && onion->texture != nullptr) {
            const float onionOx = static_cast<float>(onion->offsetX) * zoom;
            const float onionOy = static_cast<float>(onion->offsetY) * zoom;
            drawFrameSample(
                *onion,
                pivotScreenX - onionOx,
                pivotScreenY - onionOy,
                zoom,
                Color{80, 180, 255, 110});
        }
    }

    drawFrameSample(*primary, destX, destY, zoom, WHITE);
    DrawRectangleLinesEx({destX, destY, primaryW, primaryH}, 1.0f, Color{120, 140, 180, 200});

    DrawCircleV({pivotScreenX, pivotScreenY}, 5.0f, Color{255, 180, 60, 255});
    DrawLine(
        static_cast<int>(pivotScreenX) - 12,
        static_cast<int>(pivotScreenY),
        static_cast<int>(pivotScreenX) + 12,
        static_cast<int>(pivotScreenY),
        Color{255, 180, 60, 255});
    DrawLine(
        static_cast<int>(pivotScreenX),
        static_cast<int>(pivotScreenY) - 12,
        static_cast<int>(pivotScreenX),
        static_cast<int>(pivotScreenY) + 12,
        Color{255, 180, 60, 255});

    const std::string label = "Align  drag origin  wheel zoom  rot " +
        std::to_string(editor.doc.selectedRot) + "  off " +
        std::to_string(primary->offsetX) + "," + std::to_string(primary->offsetY);
    DrawText(label.c_str(), 8, 8, 16, Color{200, 200, 200, 220});
    EndTextureMode();

    if (!allowInput) {
        draggingOrigin = false;
        return;
    }

    const Vector2 mouse = GetMousePosition();
    if (!CheckCollisionPointRec(mouse, contentRect)) {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            draggingOrigin = false;
        }
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f && CheckCollisionPointRec(mouse, contentRect)) {
            editor.doc.alignZoom = std::clamp(editor.doc.alignZoom * (1.0f + wheel * 0.1f), 0.25f, 16.0f);
        }
        return;
    }

    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        editor.doc.alignZoom = std::clamp(editor.doc.alignZoom * (1.0f + wheel * 0.1f), 0.25f, 16.0f);
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        draggingOrigin = true;
    }
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        draggingOrigin = false;
    }
    if (!draggingOrigin) {
        return;
    }

    slopengine::SpriteRotation* rot = selectedRotation(editor.doc);
    if (rot == nullptr) {
        return;
    }
    ensureOffsetDefaults(*rot);

    // Drag the sprite under a fixed crosshair (SLADE-style), not "pick pixel under cursor".
    const Vector2 delta = GetMouseDelta();
    rot->offsetX -= static_cast<int>(std::lround(delta.x / zoom));
    rot->offsetY -= static_cast<int>(std::lround(delta.y / zoom));
    rot->hasOffset = true;
    editor.markDirty();
}

}
