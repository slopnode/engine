#pragma once

#include <raylib.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace slopengine {

constexpr int kSpriteRotationCount = 9;
constexpr int kSpriteHitmaskAlphaThreshold = 128;

/** Named hit-part color key from a .spr file. */
struct SpriteHitPartDef {
    std::string name;
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
};

/** One view rotation of a sprite frame. */
struct SpriteRotation {
    std::string texturePath;
    std::optional<std::string> hitMaskPath;
    std::optional<std::string> brightMapPath;
    bool mirror = false;
    bool hasOffset = false;
    int offsetX = 0; /**< Pixel origin X from top-left when hasOffset. */
    int offsetY = 0; /**< Pixel origin Y from top-left when hasOffset. */
    float rotationDeg = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float translateX = 0.0f; /**< Canvas-space shift (screenspace), not rotated with the sprite. */
    float translateY = 0.0f;
    float animRotationDeg = 0.0f;
    float animScaleX = 1.0f;
    float animScaleY = 1.0f;
    float animTranslateX = 0.0f;
    float animTranslateY = 0.0f;
    int pixelWidth = 0;
    int pixelHeight = 0;
};

/** Named frame with up to nine Doom-style rotations. */
struct SpriteFrame {
    std::string id;
    std::optional<SpriteRotation> rotations[kSpriteRotationCount];
};

/** Optional first-person view defaults stored in a .spr (view …) block. */
struct SpriteViewDefaults {
    bool present = false;
    float canvasX = 160.0f;
    float canvasY = 200.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float rotationDeg = 0.0f;
    float originX = 0.5f;
    float originY = 1.0f;
    float eyeOffsetX = 0.0f;
    float eyeOffsetY = 0.0f;
    float eyeOffsetZ = 0.0f;
};

enum class SpriteBillboardMode {
    Face,
    Fixed,
    View,
    Screen,
};

/** Parsed .spr sprite asset. */
struct SpriteAsset {
    float pixelsPerMeter = 64.0f;
    SpriteBillboardMode billboardMode = SpriteBillboardMode::Face;
    bool fullbright = false;
    SpriteViewDefaults view{};
    std::vector<SpriteHitPartDef> hitParts;
    std::vector<SpriteFrame> frames;
};

/** Atlas placement for one frame+rotation. */
struct SpriteAtlasRect {
    int atlasIndex = 0;
    Rectangle source = {};
};

/** CPU hit-mask for multi-part sprite hits. */
struct SpriteHitmask {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> parts;
    std::vector<std::string> partNames;
};

/** Runtime atlas textures and lookups for a SpriteAsset. */
struct SpriteAtlas {
    std::vector<Texture2D> textures;
    std::unordered_map<std::string, SpriteAtlasRect> rects;
    std::unordered_map<std::string, SpriteHitmask> hitmasks;
    /** Grayscale brightmaps keyed by albedo texture path (standalone textures). */
    std::unordered_map<std::string, Texture2D> brightTextures;
};

/** Parses .spr text into @p asset. */
bool parseSpriteAsset(std::string_view source, SpriteAsset& asset);

/** Serializes @p asset to .spr text. */
std::string serializeSpriteAsset(const SpriteAsset& asset);

/** Packs frame textures into atlases using @p resolveTexturePath. */
SpriteAtlas buildSpriteAtlas(
    const SpriteAsset& asset,
    const std::function<std::optional<std::filesystem::path>(std::string_view)>& resolveTexturePath);

/** Unloads GPU textures in @p atlas. */
void unloadSpriteAtlas(SpriteAtlas& atlas);

/** Finds a frame by id, or nullptr. */
const SpriteFrame* findSpriteFrame(const SpriteAsset& asset, std::string_view frameId);

/** Picks a rotation with fallbacks when the requested angle is missing. */
const SpriteRotation* selectSpriteRotation(const SpriteFrame& frame, int rotation);

/** Maps relative view yaw to a Doom rotation index 0..7 (or 8 for single). */
int doomRotationFromViewYaw(float relativeYawRadians);

std::uint8_t hitmaskPartAt(const SpriteHitmask& mask, int x, int y);
bool hitmaskTest(const SpriteHitmask& mask, int x, int y);
bool hitmaskTestUv(const SpriteHitmask& mask, float u, float v, bool mirror);
const char* hitmaskPartName(const SpriteHitmask& mask, std::uint8_t part);

}
