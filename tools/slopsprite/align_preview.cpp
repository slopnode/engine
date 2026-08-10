#include "align_preview.hpp"

#include "render/sprite_billboard.hpp"

#include <algorithm>
#include <string>

namespace slopsprite {

namespace {

void drawAlignedFrame(
    const slopengine::ViewSpriteFrame& frame,
    float pivotScreenX,
    float pivotScreenY,
    float zoom,
    Color tint) {
    const float destW = static_cast<float>(frame.pixelWidth) * zoom * frame.scaleX;
    const float destH = static_cast<float>(frame.pixelHeight) * zoom * frame.scaleY;
    const float originX =
        frame.pixelWidth > 0
            ? (static_cast<float>(frame.offsetX) / static_cast<float>(frame.pixelWidth)) * destW
            : destW * 0.5f;
    const float originY =
        frame.pixelHeight > 0
            ? (static_cast<float>(frame.offsetY) / static_cast<float>(frame.pixelHeight)) * destH
            : destH;
    const float destX = pivotScreenX + frame.translateX * zoom;
    const float destY = pivotScreenY + frame.translateY * zoom;
    DrawTexturePro(
        *frame.texture,
        frame.source,
        {destX, destY, destW, destH},
        {originX, originY},
        frame.rotationDeg,
        tint);
}

} // namespace

void AlignPreview::draw(
    Editor& editor,
    RenderTexture2D& target,
    Rectangle contentRect,
    bool allowInput) {
    BeginTextureMode(target);
    ClearBackground(previewClearColor(editor.doc, PreviewMode::Align));

    const float screenW = static_cast<float>(target.texture.width);
    const float screenH = static_cast<float>(target.texture.height);
    const float zoom = std::clamp(editor.doc.alignZoom, 0.25f, 16.0f);

    if (!editor.doc.open || editor.doc.atlasDirty) {
        EndTextureMode();
        return;
    }

    if (editor.doc.selectedFrameIndex < 0 ||
        editor.doc.selectedFrameIndex >= static_cast<int>(editor.doc.asset.frames.size()) ||
        editor.doc.selectedRot < 0 ||
        editor.doc.selectedRot >= slopengine::kSpriteRotationCount ||
        !editor.doc.asset.frames[static_cast<std::size_t>(editor.doc.selectedFrameIndex)]
             .rotations[editor.doc.selectedRot]
             .has_value()) {
        EndTextureMode();
        return;
    }

    const std::string& frameId =
        editor.doc.asset.frames[static_cast<std::size_t>(editor.doc.selectedFrameIndex)].id;
    const auto primary = slopengine::resolveViewSpriteFrame(
        editor.doc.asset, editor.doc.atlas, frameId, editor.doc.selectedRot);
    if (!primary || primary->texture == nullptr) {
        EndTextureMode();
        return;
    }

    const float pivotScreenX = screenW * 0.5f + editor.doc.alignPanX;
    const float pivotScreenY = screenH * 0.55f + editor.doc.alignPanY;

    DrawLine(
        0, static_cast<int>(pivotScreenY), static_cast<int>(screenW), static_cast<int>(pivotScreenY),
        Color{60, 70, 90, 180});
    DrawLine(
        static_cast<int>(pivotScreenX),
        0,
        static_cast<int>(pivotScreenX),
        static_cast<int>(screenH),
        Color{60, 70, 90, 180});

    if (editor.doc.onionEnabled) {
        const slopengine::SpriteAsset& onionAsset = editor.onionAsset();
        const slopengine::SpriteAtlas& onionAtlas = editor.onionAtlas();
        if (editor.doc.onionFrameIndex >= 0 &&
            editor.doc.onionFrameIndex < static_cast<int>(onionAsset.frames.size())) {
            const std::string& onionFrameId =
                onionAsset.frames[static_cast<std::size_t>(editor.doc.onionFrameIndex)].id;
            const auto onion = slopengine::resolveViewSpriteFrame(
                onionAsset, onionAtlas, onionFrameId, editor.doc.onionRot);
            if (onion && onion->texture != nullptr) {
                drawAlignedFrame(*onion, pivotScreenX, pivotScreenY, zoom, Color{80, 180, 255, 110});
            }
        }
    }

    drawAlignedFrame(*primary, pivotScreenX, pivotScreenY, zoom, WHITE);

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

    EndTextureMode();

    if (!allowInput) {
        return;
    }

    const Vector2 mouse = GetMousePosition();
    if (!CheckCollisionPointRec(mouse, contentRect)) {
        return;
    }

    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        editor.doc.alignZoom = std::clamp(editor.doc.alignZoom * (1.0f + wheel * 0.1f), 0.25f, 16.0f);
    }

    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        const Vector2 delta = GetMouseDelta();
        editor.doc.alignPanX += delta.x;
        editor.doc.alignPanY += delta.y;
    }
}

}
