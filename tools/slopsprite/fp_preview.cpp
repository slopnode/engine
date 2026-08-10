#include "fp_preview.hpp"

#include "render/sprite_billboard.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace slopsprite {

namespace {

struct ViewCanvasFit {
    float canvasW = 320.0f;
    float canvasH = 200.0f;
    float scale = 1.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
};

struct HostPose {
    float pinX = 0.0f;
    float pinY = 0.0f;
    float rotationDeg = 0.0f;
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

HostPose resolveHostPose(
    const slopengine::SpriteAsset& asset,
    const slopengine::SpriteAtlas& atlas,
    const std::string& frameId,
    const slopengine::ViewSprite& view,
    bool tweenRotation,
    bool tweenScale,
    bool tweenTranslate,
    float transformBlend,
    const std::string& nextFrame) {
    HostPose pose{};
    const auto frame = slopengine::resolveViewSpriteFrame(asset, atlas, frameId, 0);
    if (!frame) {
        pose.pinX = view.anchorX + view.offsetX;
        pose.pinY = view.anchorY + view.offsetY;
        pose.rotationDeg = view.rotationDeg;
        return pose;
    }

    float rotationDeg = frame->rotationDeg + frame->animRotationDeg;
    float translateX = frame->translateX + frame->animTranslateX;
    float translateY = frame->translateY + frame->animTranslateY;

    if (!nextFrame.empty() && (tweenRotation || tweenScale || tweenTranslate)) {
        const auto next = slopengine::resolveViewSpriteFrame(asset, atlas, nextFrame, 0);
        if (next) {
            const float nextRotation = next->rotationDeg + next->animRotationDeg;
            const float nextTranslateX = next->translateX + next->animTranslateX;
            const float nextTranslateY = next->translateY + next->animTranslateY;
            if (tweenRotation) {
                rotationDeg = rotationDeg + (nextRotation - rotationDeg) * transformBlend;
            }
            if (tweenTranslate) {
                translateX = translateX + (nextTranslateX - translateX) * transformBlend;
                translateY = translateY + (nextTranslateY - translateY) * transformBlend;
            }
        }
    }

    pose.pinX = view.anchorX + view.offsetX + translateX;
    pose.pinY = view.anchorY + view.offsetY + translateY;
    pose.rotationDeg = view.rotationDeg + rotationDeg;
    return pose;
}

Vector2 attachCanvasPoint(const HostPose& pose, float attachX, float attachY) {
    const float theta = pose.rotationDeg * (static_cast<float>(DEG2RAD));
    const float cosT = std::cos(theta);
    const float sinT = std::sin(theta);
    return {
        pose.pinX + attachX * cosT - attachY * sinT,
        pose.pinY + attachX * sinT + attachY * cosT,
    };
}

void drawFpSpriteFrame(
    const slopengine::SpriteAsset& asset,
    const slopengine::SpriteAtlas& atlas,
    const std::string& frameId,
    const slopengine::ViewSprite& view,
    const ViewCanvasFit& fit,
    bool tweenRotation,
    bool tweenScale,
    bool tweenTranslate,
    float transformBlend,
    const std::string& nextFrame,
    bool drawOriginDot,
    Color tint) {
    const auto frame = slopengine::resolveViewSpriteFrame(asset, atlas, frameId, 0);
    if (!frame || frame->texture == nullptr) {
        return;
    }

    auto originFromFrame = [&](const slopengine::ViewSpriteFrame& resolved) {
        if (resolved.hasOffset && resolved.pixelWidth > 0 && resolved.pixelHeight > 0) {
            return Vector2{
                static_cast<float>(resolved.offsetX) / static_cast<float>(resolved.pixelWidth),
                static_cast<float>(resolved.offsetY) / static_cast<float>(resolved.pixelHeight),
            };
        }
        return Vector2{view.originX, view.originY};
    };

    float originX = originFromFrame(*frame).x;
    float originY = originFromFrame(*frame).y;
    float rotationDeg = frame->rotationDeg + frame->animRotationDeg;
    float scaleX = frame->scaleX * frame->animScaleX;
    float scaleY = frame->scaleY * frame->animScaleY;
    float translateX = frame->translateX + frame->animTranslateX;
    float translateY = frame->translateY + frame->animTranslateY;

    if (!nextFrame.empty() && (tweenRotation || tweenScale || tweenTranslate)) {
        const auto next = slopengine::resolveViewSpriteFrame(asset, atlas, nextFrame, 0);
        if (next) {
            const float nextRotation = next->rotationDeg + next->animRotationDeg;
            const float nextScaleX = next->scaleX * next->animScaleX;
            const float nextScaleY = next->scaleY * next->animScaleY;
            const float nextTranslateX = next->translateX + next->animTranslateX;
            const float nextTranslateY = next->translateY + next->animTranslateY;
            if (tweenRotation) {
                rotationDeg = rotationDeg + (nextRotation - rotationDeg) * transformBlend;
            }
            if (tweenScale) {
                scaleX = scaleX + (nextScaleX - scaleX) * transformBlend;
                scaleY = scaleY + (nextScaleY - scaleY) * transformBlend;
            }
            if (tweenTranslate) {
                translateX = translateX + (nextTranslateX - translateX) * transformBlend;
                translateY = translateY + (nextTranslateY - translateY) * transformBlend;
            }
        }
    }

    const float destW = static_cast<float>(frame->pixelWidth) * fit.scale * view.scaleX * scaleX;
    const float destH = static_cast<float>(frame->pixelHeight) * fit.scale * view.scaleY * scaleY;
    const float screenX = fit.offsetX + (view.anchorX + view.offsetX + translateX) * fit.scale;
    const float screenY = fit.offsetY + (view.anchorY + view.offsetY + translateY) * fit.scale;
    const Rectangle dest{screenX, screenY, destW, destH};
    DrawTexturePro(
        *frame->texture,
        frame->source,
        dest,
        Vector2{destW * originX, destH * originY},
        view.rotationDeg + rotationDeg,
        tint);
    if (drawOriginDot) {
        DrawCircleV({screenX, screenY}, 4.0f, Color{255, 180, 60, 255});
    }
}

void drawAttachMarker(Vector2 screen, const std::string& name, bool selected) {
    const Color color = selected ? Color{80, 220, 255, 255} : Color{40, 180, 220, 220};
    const float r = selected ? 6.0f : 5.0f;
    DrawCircleLines(static_cast<int>(screen.x), static_cast<int>(screen.y), r, color);
    DrawLine(
        static_cast<int>(screen.x - 8.0f),
        static_cast<int>(screen.y),
        static_cast<int>(screen.x + 8.0f),
        static_cast<int>(screen.y),
        color);
    DrawLine(
        static_cast<int>(screen.x),
        static_cast<int>(screen.y - 8.0f),
        static_cast<int>(screen.x),
        static_cast<int>(screen.y + 8.0f),
        color);
    DrawText(
        name.c_str(), static_cast<int>(screen.x) + 9, static_cast<int>(screen.y) - 9, 10, color);
}

slopengine::SpriteAttachPoint* selectedAttachPointMutable(Editor& editor) {
    if (editor.doc.selectedFrameIndex < 0 ||
        editor.doc.selectedFrameIndex >= static_cast<int>(editor.doc.asset.frames.size())) {
        return nullptr;
    }
    slopengine::SpriteFrame& frame =
        editor.doc.asset.frames[static_cast<std::size_t>(editor.doc.selectedFrameIndex)];
    if (editor.doc.selectedAttachPointIndex < 0 ||
        editor.doc.selectedAttachPointIndex >= static_cast<int>(frame.attachPoints.size())) {
        return nullptr;
    }
    return &frame.attachPoints[static_cast<std::size_t>(editor.doc.selectedAttachPointIndex)];
}

slopengine::SpriteAnimOverlay* selectedOverlayMutable(Editor& editor) {
    if (!editor.doc.hasAnim || editor.doc.selectedOverlayHoldIndex < 0 ||
        editor.doc.selectedOverlayIndex < 0) {
        return nullptr;
    }
    const auto clipIt = editor.doc.animBank.clipIndexByName.find(editor.doc.animClip);
    if (clipIt == editor.doc.animBank.clipIndexByName.end() ||
        clipIt->second >= editor.doc.animBank.clips.size()) {
        return nullptr;
    }
    slopengine::SpriteAnimClip& clip = editor.doc.animBank.clips[clipIt->second];
    if (editor.doc.selectedOverlayHoldIndex >= static_cast<int>(clip.frames.size())) {
        return nullptr;
    }
    slopengine::SpriteAnimFrame& hold =
        clip.frames[static_cast<std::size_t>(editor.doc.selectedOverlayHoldIndex)];
    if (editor.doc.selectedOverlayIndex >= static_cast<int>(hold.overlays.size())) {
        return nullptr;
    }
    return &hold.overlays[static_cast<std::size_t>(editor.doc.selectedOverlayIndex)];
}

} // namespace

void FpPreview::draw(
    Editor& editor,
    slopengine::AssetStore& assets,
    RenderTexture2D& target,
    Rectangle contentRect,
    bool allowInput) {
    BeginTextureMode(target);
    ClearBackground(previewClearColor(editor.doc, PreviewMode::FirstPerson));

    const float screenW = static_cast<float>(target.texture.width);
    const float screenH = static_cast<float>(target.texture.height);
    ViewCanvasFit fit = makeViewCanvasFit(editor.viewCanvasW, editor.viewCanvasH, screenW, screenH);
    const float fpZoom = std::clamp(editor.doc.fpZoom, 0.25f, 16.0f);
    fit.scale *= fpZoom;
    fit.offsetX = (screenW - fit.canvasW * fit.scale) * 0.5f + editor.doc.fpPanX;
    fit.offsetY = (screenH - fit.canvasH * fit.scale) * 0.5f + editor.doc.fpPanY;

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
        std::vector<PreviewOverlayDraw> overlays;
        collectPreviewOverlays(editor.doc, assets, overlays);

        const HostPose hostPose = resolveHostPose(
            editor.doc.asset,
            editor.doc.atlas,
            editor.doc.currentFrame,
            editor.doc.viewSprite,
            editor.doc.animTweenRotation,
            editor.doc.animTweenScale,
            editor.doc.animTweenTranslate,
            editor.doc.animTransformBlend,
            editor.doc.animNextFrame);

        if (allowInput && fit.scale > 0.0f) {
            const Vector2 mouse = GetMousePosition();
            const bool inContent = CheckCollisionPointRec(mouse, contentRect);
            if (inContent) {
                const float wheel = GetMouseWheelMove();
                if (wheel != 0.0f) {
                    editor.doc.fpZoom =
                        std::clamp(editor.doc.fpZoom * (1.0f + wheel * 0.1f), 0.25f, 16.0f);
                }
                if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
                    const Vector2 delta = GetMouseDelta();
                    editor.doc.fpPanX += delta.x;
                    editor.doc.fpPanY += delta.y;
                }
            }
            if (inContent && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                if (slopengine::SpriteAttachPoint* point = selectedAttachPointMutable(editor)) {
                    const Vector2 delta = GetMouseDelta();
                    const float theta = hostPose.rotationDeg * (static_cast<float>(DEG2RAD));
                    const float cosT = std::cos(theta);
                    const float sinT = std::sin(theta);
                    const float dx = delta.x / fit.scale;
                    const float dy = delta.y / fit.scale;
                    point->x += dx * cosT + dy * sinT;
                    point->y += -dx * sinT + dy * cosT;
                    editor.markDirty();
                } else if (slopengine::SpriteAnimOverlay* overlay = selectedOverlayMutable(editor)) {
                    const Vector2 delta = GetMouseDelta();
                    overlay->x += delta.x / fit.scale;
                    overlay->y += delta.y / fit.scale;
                    editor.doc.animDirty = true;
                    for (PreviewOverlayDraw& draw : overlays) {
                        if (draw.holdIndex == editor.doc.selectedOverlayHoldIndex &&
                            draw.overlayIndex == editor.doc.selectedOverlayIndex) {
                            draw.x = overlay->x;
                            draw.y = overlay->y;
                        }
                    }
                }
            }
        }

        auto drawHost = [&]() {
            drawFpSpriteFrame(
                editor.doc.asset,
                editor.doc.atlas,
                editor.doc.currentFrame,
                editor.doc.viewSprite,
                fit,
                editor.doc.animTweenRotation,
                editor.doc.animTweenScale,
                editor.doc.animTweenTranslate,
                editor.doc.animTransformBlend,
                editor.doc.animNextFrame,
                true,
                WHITE);
        };

        auto drawOverlay = [&](const PreviewOverlayDraw& overlay) {
            const slopengine::SpriteAsset* asset = assets.getSpriteAsset(overlay.spritePath);
            const slopengine::SpriteAtlas* atlas = assets.getSpriteAtlas(overlay.spritePath);
            if (asset == nullptr || atlas == nullptr) {
                return;
            }
            slopengine::ViewSprite view = editor.doc.viewSprite;
            view.offsetX = editor.doc.viewSprite.offsetX + overlay.x;
            view.offsetY = editor.doc.viewSprite.offsetY + overlay.y;
            const bool selected = overlay.holdIndex == editor.doc.selectedOverlayHoldIndex &&
                                  overlay.overlayIndex == editor.doc.selectedOverlayIndex;
            drawFpSpriteFrame(
                *asset,
                *atlas,
                overlay.frameId,
                view,
                fit,
                overlay.tweenRotation,
                overlay.tweenScale,
                overlay.tweenTranslate,
                overlay.transformBlend,
                overlay.nextFrame,
                selected,
                selected ? Color{255, 255, 180, 255} : WHITE);
        };

        std::size_t oi = 0;
        while (oi < overlays.size() && overlays[oi].layer < 0) {
            drawOverlay(overlays[oi]);
            ++oi;
        }
        drawHost();
        while (oi < overlays.size()) {
            drawOverlay(overlays[oi]);
            ++oi;
        }

        if (editor.doc.selectedFrameIndex >= 0 &&
            editor.doc.selectedFrameIndex < static_cast<int>(editor.doc.asset.frames.size())) {
            const slopengine::SpriteFrame& frame = editor.doc.asset
                .frames[static_cast<std::size_t>(editor.doc.selectedFrameIndex)];
            for (int pi = 0; pi < static_cast<int>(frame.attachPoints.size()); ++pi) {
                const slopengine::SpriteAttachPoint& point =
                    frame.attachPoints[static_cast<std::size_t>(pi)];
                const Vector2 canvas = attachCanvasPoint(hostPose, point.x, point.y);
                const Vector2 screen{
                    fit.offsetX + canvas.x * fit.scale,
                    fit.offsetY + canvas.y * fit.scale,
                };
                drawAttachMarker(screen, point.name, editor.doc.selectedAttachPointIndex == pi);
            }
        }
    }

    EndTextureMode();
}

}
