#include "render/world_sprite_attachment.hpp"

#include "assets/asset_services.hpp"
#include "assets/asset_store.hpp"
#include "assets/sprite_loader.hpp"
#include "camera/components.hpp"
#include "render/components.hpp"
#include "render/sprite_animator.hpp"
#include "render/sprite_billboard.hpp"
#include "render/transform.hpp"

namespace slopengine {

std::optional<Vector3> resolveWorldSpriteAttachmentWorld(
    flecs::world& world,
    flecs::entity host,
    const std::string& attachName) {
    if (!host.is_valid() || !host.has<SpriteInstance>() || !host.has<GlobalTransformation>()) {
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

    flecs::entity player = world.lookup("Player");
    if (!player.is_valid() || !player.has<Lens>()) {
        return std::nullopt;
    }
    const Lens& lens = player.get<Lens>();

    if (host.has<LocalTransformation>()) {
        updateTransform(host, host.get_mut<LocalTransformation>(), host.get_mut<GlobalTransformation>());
    }
    const GlobalTransformation& global = host.get<GlobalTransformation>();

    SpriteAnimTween tween{};
    const SpriteAnimTween* tweenPtr = nullptr;
    if (host.has<SpriteAnimator>()) {
        const SpriteAnimator& animator = host.get<SpriteAnimator>();
        if (animator.hasTween() && !animator.nextFrame.empty()) {
            tween.nextFrame = animator.nextFrame;
            tween.blend = animator.transformBlend;
            tween.tweenRotation = animator.tweenRotation;
            tween.tweenScale = animator.tweenScale;
            tween.tweenTranslate = animator.tweenTranslate;
            tweenPtr = &tween;
        }
    }

    const auto billboard = resolveSpriteBillboard(sprite, global, lens, assets, tweenPtr);
    if (!billboard) {
        return std::nullopt;
    }
    return spriteBillboardPointAt(*billboard, attachPoint->x, attachPoint->y);
}

}
