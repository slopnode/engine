#pragma once

#include "assets/asset_store.hpp"
#include "assets/sprite_loader.hpp"
#include "render/components.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <raylib.h>

namespace slopengine {

/** Resolved billboard geometry and atlas sample for one sprite instance. */
struct SpriteBillboard {
    Vector3 points[4]{};
    Vector2 size{};
    Vector3 position{};
    const SpriteHitmask* hitmask = nullptr;
    bool mirror = false;
    bool fullbright = false;
    SpriteBlendMode blend = SpriteBlendMode::Alpha;
    Color tint = WHITE;
    int pixelWidth = 0;
    int pixelHeight = 0;
    const Texture2D* texture = nullptr;
    Rectangle source{};
    const Texture2D* brightTexture = nullptr;
};

/** Ray hit against a sprite billboard, including hit-part. */
struct SpriteBillboardHit {
    float distance = 0.0f;
    Vector3 point{};
    int pixelX = 0;
    int pixelY = 0;
    std::uint8_t part = 0;
    std::string partName;
};

/** Optional anim-* tween between the current frame and nextFrame. */
struct SpriteAnimTween {
    std::string_view nextFrame;
    float blend = 0.0f;
    bool tweenRotation = false;
    bool tweenScale = false;
    bool tweenTranslate = false;

    bool hasTween() const {
        return tweenRotation || tweenScale || tweenTranslate;
    }
};

/** Builds a billboard facing @p viewPosition. */
std::optional<SpriteBillboard> resolveSpriteBillboard(
    const SpriteInstance& sprite,
    const GlobalTransformation& global,
    Vector3 viewPosition,
    float cameraYaw,
    AssetStore& assets,
    const SpriteAnimTween* tween = nullptr);

/** Builds a billboard from an already-loaded asset/atlas. */
std::optional<SpriteBillboard> resolveSpriteBillboard(
    const SpriteAsset& asset,
    const SpriteAtlas& atlas,
    std::string_view frameId,
    float facingYaw,
    const GlobalTransformation& global,
    Vector3 viewPosition,
    float cameraYaw,
    const SpriteAnimTween* tween = nullptr);

/** Builds a billboard using an explicit Doom rotation index (0..8), ignoring camera yaw for rot pick. */
std::optional<SpriteBillboard> resolveSpriteBillboardForcedRot(
    const SpriteAsset& asset,
    const SpriteAtlas& atlas,
    std::string_view frameId,
    int rotation,
    float facingYaw,
    const GlobalTransformation& global,
    Vector3 viewPosition,
    float cameraYaw,
    const SpriteAnimTween* tween = nullptr);

/** Builds a billboard facing the Lens camera. */
std::optional<SpriteBillboard> resolveSpriteBillboard(
    const SpriteInstance& sprite,
    const GlobalTransformation& global,
    const Lens& lens,
    AssetStore& assets,
    const SpriteAnimTween* tween = nullptr);

/** Horizontal look yaw (radians) from eye toward target on XZ. */
float horizontalCameraYaw(Vector3 eye, Vector3 target);

/** Atlas sample for a screen-space view sprite (rot 0 / non-directional). */
struct ViewSpriteFrame {
    const Texture2D* texture = nullptr;
    Rectangle source{};
    int pixelWidth = 0;
    int pixelHeight = 0;
    int offsetX = 0;
    int offsetY = 0;
    bool hasOffset = false;
    bool mirror = false;
    float rotationDeg = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float translateX = 0.0f;
    float translateY = 0.0f;
    float animRotationDeg = 0.0f;
    float animScaleX = 1.0f;
    float animScaleY = 1.0f;
    float animTranslateX = 0.0f;
    float animTranslateY = 0.0f;
};

std::optional<ViewSpriteFrame> resolveViewSpriteFrame(
    const SpriteInstance& sprite,
    AssetStore& assets);

/** Atlas sample from an already-loaded asset/atlas (rot 0). */
std::optional<ViewSpriteFrame> resolveViewSpriteFrame(
    const SpriteAsset& asset,
    const SpriteAtlas& atlas,
    std::string_view frameId);

/** Atlas sample for an explicit Doom rotation index. */
std::optional<ViewSpriteFrame> resolveViewSpriteFrame(
    const SpriteAsset& asset,
    const SpriteAtlas& atlas,
    std::string_view frameId,
    int rotation);

/** Raycasts the billboard quad and optional hit mask. */
std::optional<SpriteBillboardHit> raycastSpriteBillboard(
    const Ray& ray,
    const SpriteBillboard& billboard,
    float maxDistance);

/**
 * Raycasts only the billboard's horizontal footprint (its bottom edge projected onto XZ),
 * ignoring pitch/vertical extent and the pixel hitmask entirely. For games with no vertical
 * aim, this avoids hitmask gaps (legs, thin frames, floating enemies) that would otherwise
 * make a level shot miss a target that's clearly in front of the player.
 */
std::optional<SpriteBillboardHit> raycastSpriteBillboardXZ(
    const Ray& ray,
    const SpriteBillboard& billboard,
    float maxDistance);

/** Draws hit-mask debug overlay for @p billboard. */
void drawSpriteMaskDebug(const SpriteBillboard& billboard);

} // namespace slopengine
