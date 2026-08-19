#include "render/view_sprite_attachment.hpp"

#include "assets/asset_services.hpp"
#include "assets/asset_store.hpp"
#include "assets/sprite_loader.hpp"
#include "camera/components.hpp"
#include "render/components.hpp"
#include "render/hud.hpp"
#include "render/sprite_animator.hpp"
#include "render/sprite_billboard.hpp"
#include "script/first_person_script.hpp"

#include <algorithm>
#include <cmath>
#include <raymath.h>

namespace slopengine {

std::optional<Vector3> resolveViewSpriteAttachmentWorld(
    flecs::world& world,
    flecs::entity host,
    const std::string& attachName,
    float depth) {
    if (!host.is_valid() || !host.has<ViewSprite>() || !host.has<SpriteInstance>()) {
        return std::nullopt;
    }
    if (!world.has<AssetServices>() || world.get<AssetServices>().store == nullptr) {
        return std::nullopt;
    }
    AssetStore& assets = *world.get_mut<AssetServices>().store;
    const SpriteInstance& sprite = host.get<SpriteInstance>();
    const SpriteAsset* asset = assets.getSpriteAsset(sprite.sprite);
    if (asset == nullptr) {
        return std::nullopt;
    }
    const SpriteAttachPoint* attachPoint = findSpriteAttachPoint(*asset, sprite.frame, attachName);
    if (attachPoint == nullptr) {
        return std::nullopt;
    }

    const ViewSprite& viewSprite = host.get<ViewSprite>();
    const auto frame = resolveViewSpriteFrame(sprite, assets);
    if (!frame) {
        return std::nullopt;
    }

    float rotationDeg = frame->rotationDeg + frame->animRotationDeg;
    float scaleX = frame->scaleX * frame->animScaleX;
    float scaleY = frame->scaleY * frame->animScaleY;
    float translateX = frame->translateX + frame->animTranslateX;
    float translateY = frame->translateY + frame->animTranslateY;
    float attachX = attachPoint->x;
    float attachY = attachPoint->y;

    if (host.has<SpriteAnimator>()) {
        const SpriteAnimator& animator = host.get<SpriteAnimator>();
        if (animator.hasTween() && !animator.nextFrame.empty()) {
            SpriteInstance nextSprite = sprite;
            nextSprite.frame = animator.nextFrame;
            const auto nextFrame = resolveViewSpriteFrame(nextSprite, assets);
            if (nextFrame) {
                const float blend = animator.transformBlend;
                const float nextRotation =
                    nextFrame->rotationDeg + nextFrame->animRotationDeg;
                const float nextScaleX = nextFrame->scaleX * nextFrame->animScaleX;
                const float nextScaleY = nextFrame->scaleY * nextFrame->animScaleY;
                const float nextTranslateX =
                    nextFrame->translateX + nextFrame->animTranslateX;
                const float nextTranslateY =
                    nextFrame->translateY + nextFrame->animTranslateY;
                if (animator.tweenRotation) {
                    rotationDeg = rotationDeg + (nextRotation - rotationDeg) * blend;
                }
                if (animator.tweenScale) {
                    scaleX = scaleX + (nextScaleX - scaleX) * blend;
                    scaleY = scaleY + (nextScaleY - scaleY) * blend;
                }
                if (animator.tweenTranslate) {
                    translateX = translateX + (nextTranslateX - translateX) * blend;
                    translateY = translateY + (nextTranslateY - translateY) * blend;
                }
                const SpriteAttachPoint* nextAttachPoint =
                    findSpriteAttachPoint(*asset, animator.nextFrame, attachName);
                if (nextAttachPoint != nullptr) {
                    attachX = attachX + (nextAttachPoint->x - attachX) * blend;
                    attachY = attachY + (nextAttachPoint->y - attachY) * blend;
                }
            }
        }
    }

    attachX *= scaleX;
    attachY *= scaleY;

    const float pinX = viewSprite.anchorX + viewSprite.offsetX + translateX;
    const float pinY = viewSprite.anchorY + viewSprite.offsetY + translateY;
    const float theta =
        (viewSprite.rotationDeg + rotationDeg) * (static_cast<float>(DEG2RAD));
    const float cosT = std::cos(theta);
    const float sinT = std::sin(theta);
    const float canvasX = pinX + attachX * cosT - attachY * sinT;
    const float canvasY = pinY + attachX * sinT + attachY * cosT;

    ViewCanvas viewCanvas{};
    if (world.has<ViewCanvas>()) {
        viewCanvas = world.get<ViewCanvas>();
    }

    const float screenW = static_cast<float>(std::max(GetScreenWidth(), 1));
    const float screenH = static_cast<float>(std::max(GetScreenHeight(), 1));
    const int renderW = std::max(GetRenderWidth(), 1);
    const int renderH = std::max(GetRenderHeight(), 1);
    const ViewCanvasFit viewFit =
        makeViewCanvasFit(viewCanvas.width, viewCanvas.height, screenW, screenH);
    const float screenX = viewFit.offsetX + canvasX * viewFit.scale;
    const float screenY = viewFit.offsetY + canvasY * viewFit.scale;
    const Vector2 fboPoint{
        screenX * (static_cast<float>(renderW) / screenW),
        screenY * (static_cast<float>(renderH) / screenH),
    };

    flecs::entity player = world.lookup("Player");
    if (!player.is_valid() || !player.has<Lens>()) {
        return std::nullopt;
    }
    FirstPersonController controller{};
    ViewEyeOffset eyeOffset{};
    if (player.has<FirstPersonController>()) {
        controller = player.get<FirstPersonController>();
    }
    if (player.has<ViewEyeOffset>()) {
        eyeOffset = player.get<ViewEyeOffset>();
    }
    const Camera3D presentCam =
        presentationCamera(player.get<Lens>(), controller, eyeOffset);
    const float useDepth = depth > 0.0f ? depth : 0.35f;
    const Ray ray = GetScreenToWorldRayEx(fboPoint, presentCam, renderW, renderH);
    return Vector3Add(ray.position, Vector3Scale(ray.direction, useDepth));
}

}
