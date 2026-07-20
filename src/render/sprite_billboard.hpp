#pragma once

#include "assets/asset_store.hpp"
#include "assets/sprite_loader.hpp"
#include "render/components.hpp"

#include <cstdint>
#include <optional>
#include <string>

#include <raylib.h>

namespace slopengine {

/** Resolved billboard geometry and atlas sample for one sprite instance. */
struct SpriteBillboard {
    Vector3 points[4]{};
    Vector2 size{};
    Vector3 position{};
    const SpriteHitmask* hitmask = nullptr;
    bool mirror = false;
    int pixelWidth = 0;
    int pixelHeight = 0;
    const Texture2D* texture = nullptr;
    Rectangle source{};
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

/** Builds a billboard facing @p viewPosition. */
std::optional<SpriteBillboard> resolveSpriteBillboard(
    const SpriteInstance& sprite,
    const GlobalTransformation& global,
    Vector3 viewPosition,
    AssetStore& assets);

/** Builds a billboard facing the Lens camera. */
std::optional<SpriteBillboard> resolveSpriteBillboard(
    const SpriteInstance& sprite,
    const GlobalTransformation& global,
    const Lens& lens,
    AssetStore& assets);

/** Raycasts the billboard quad and optional hit mask. */
std::optional<SpriteBillboardHit> raycastSpriteBillboard(
    const Ray& ray,
    const SpriteBillboard& billboard,
    float maxDistance);

/** Draws hit-mask debug overlay for @p billboard. */
void drawSpriteMaskDebug(const SpriteBillboard& billboard);

} // namespace slopengine
