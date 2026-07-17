#pragma once

#include <raylib.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace slopengine {

constexpr int kSpriteRotationCount = 9;

struct SpriteRotation {
    std::string texturePath;
    bool mirror = false;
    int pixelWidth = 0;
    int pixelHeight = 0;
};

struct SpriteFrame {
    std::string id;
    std::optional<SpriteRotation> rotations[kSpriteRotationCount];
};

struct SpriteAsset {
    float pixelsPerMeter = 64.0f;
    std::vector<SpriteFrame> frames;
};

struct SpriteAtlasRect {
    int atlasIndex = 0;
    Rectangle source = {};
};

struct SpriteAtlas {
    std::vector<Texture2D> textures;
    std::unordered_map<std::string, SpriteAtlasRect> rects;
};

bool parseSpriteAsset(std::string_view source, SpriteAsset& asset);

SpriteAtlas buildSpriteAtlas(
    const SpriteAsset& asset,
    const std::function<std::optional<std::filesystem::path>(std::string_view)>& resolveTexturePath);

void unloadSpriteAtlas(SpriteAtlas& atlas);

const SpriteFrame* findSpriteFrame(const SpriteAsset& asset, std::string_view frameId);

const SpriteRotation* selectSpriteRotation(const SpriteFrame& frame, int rotation);

int doomRotationFromViewYaw(float relativeYawRadians);

}
