#include "fp_preview.hpp"

#include "render/sprite_billboard.hpp"

#include <algorithm>

namespace slopsprite {

namespace {

struct ViewCanvasFit {
    float canvasW = 320.0f;
    float canvasH = 200.0f;
    float scale = 1.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
};

ViewCanvasFit makeViewCanvasFit(int width, int height, float screenW, float screenH) {
    ViewCanvasFit fit{};
    fit.canvasW = static_cast<float>(std::max(width, 1));
    fit.canvasH = static_cast<float>(std::max(height, 1));
    fit.scale = std::min(screenW / fit.canvasW, screenH / fit.canvasH);
    fit.offsetX = (screenW - fit.canvasW * fit.scale) * 0.5f;
    fit.offsetY = (screenH - fit.canvasH * fit.scale) * 0.5f;
    return fit;
}

} // namespace

void FpPreview::draw(Editor& editor, RenderTexture2D& target, Rectangle contentRect, bool allowInput) {
    (void)contentRect;
    (void)allowInput;

    BeginTextureMode(target);
    ClearBackground(Color{18, 18, 22, 255});

    const float screenW = static_cast<float>(target.texture.width);
    const float screenH = static_cast<float>(target.texture.height);
    const ViewCanvasFit fit = makeViewCanvasFit(editor.viewCanvasW, editor.viewCanvasH, screenW, screenH);

    const Rectangle canvasRect{
        fit.offsetX,
        fit.offsetY,
        fit.canvasW * fit.scale,
        fit.canvasH * fit.scale,
    };
    DrawRectangleRec(canvasRect, Color{40, 44, 52, 255});
    DrawRectangleLinesEx(canvasRect, 1.0f, Color{90, 100, 120, 255});

    const float midX = fit.offsetX + fit.canvasW * 0.5f * fit.scale;
    const float midY = fit.offsetY + fit.canvasH * 0.5f * fit.scale;
    DrawLine(
        static_cast<int>(fit.offsetX),
        static_cast<int>(midY),
        static_cast<int>(fit.offsetX + canvasRect.width),
        static_cast<int>(midY),
        Color{70, 80, 100, 160});
    DrawLine(
        static_cast<int>(midX),
        static_cast<int>(fit.offsetY),
        static_cast<int>(midX),
        static_cast<int>(fit.offsetY + canvasRect.height),
        Color{70, 80, 100, 160});

    if (editor.doc.open && !editor.doc.atlasDirty) {
        const auto frame = slopengine::resolveViewSpriteFrame(
            editor.doc.asset, editor.doc.atlas, editor.doc.currentFrame, 0);
        if (frame && frame->texture != nullptr) {
            const slopengine::ViewSprite& view = editor.doc.viewSprite;

            auto originFromFrame = [&](const slopengine::ViewSpriteFrame& resolved) {
                if (resolved.hasOffset && resolved.pixelWidth > 0 && resolved.pixelHeight > 0) {
                    return Vector2{
                        static_cast<float>(resolved.offsetX) /
                            static_cast<float>(resolved.pixelWidth),
                        static_cast<float>(resolved.offsetY) /
                            static_cast<float>(resolved.pixelHeight),
                    };
                }
                return Vector2{view.originX, view.originY};
            };

            float originX = originFromFrame(*frame).x;
            float originY = originFromFrame(*frame).y;
            float frameRotationDeg = frame->rotationDeg;
            float frameScaleX = frame->scaleX;
            float frameScaleY = frame->scaleY;
            float translateX = frame->translateX;
            float translateY = frame->translateY;

            if (!editor.doc.animNextFrame.empty() &&
                (editor.doc.animTweenRotation || editor.doc.animTweenScale ||
                 editor.doc.animTweenTranslate)) {
                const auto nextFrame = slopengine::resolveViewSpriteFrame(
                    editor.doc.asset, editor.doc.atlas, editor.doc.animNextFrame, 0);
                if (nextFrame) {
                    const float blend = editor.doc.animTransformBlend;
                    if (editor.doc.animTweenRotation) {
                        frameRotationDeg = frame->rotationDeg +
                            (nextFrame->rotationDeg - frame->rotationDeg) * blend;
                    }
                    if (editor.doc.animTweenScale) {
                        frameScaleX =
                            frame->scaleX + (nextFrame->scaleX - frame->scaleX) * blend;
                        frameScaleY =
                            frame->scaleY + (nextFrame->scaleY - frame->scaleY) * blend;
                    }
                    if (editor.doc.animTweenTranslate) {
                        translateX = frame->translateX +
                            (nextFrame->translateX - frame->translateX) * blend;
                        translateY = frame->translateY +
                            (nextFrame->translateY - frame->translateY) * blend;
                    }
                }
            }

            const float destW =
                static_cast<float>(frame->pixelWidth) * fit.scale * view.scaleX * frameScaleX;
            const float destH =
                static_cast<float>(frame->pixelHeight) * fit.scale * view.scaleY * frameScaleY;
            const float screenX = fit.offsetX + (view.canvasX + translateX) * fit.scale;
            const float screenY = fit.offsetY + (view.canvasY + translateY) * fit.scale;
            const Rectangle dest{screenX, screenY, destW, destH};
            DrawTexturePro(
                *frame->texture,
                frame->source,
                dest,
                Vector2{destW * originX, destH * originY},
                view.rotationDeg + frameRotationDeg,
                WHITE);
            DrawCircleV({screenX, screenY}, 4.0f, Color{255, 180, 60, 255});
        }
    }

    DrawText("FP", 8, 8, 16, Color{200, 200, 200, 220});
    EndTextureMode();
}

}
