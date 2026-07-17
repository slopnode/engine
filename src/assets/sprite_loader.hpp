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

struct SpriteHitPartDef {
    std::string name;
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
};

struct SpriteRotation {
    std::string texturePath;
    std::optional<std::string> hitMaskPath;
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
    std::vector<SpriteHitPartDef> hitParts;
    std::vector<SpriteFrame> frames;
};

struct SpriteAtlasRect {
    int atlasIndex = 0;
    Rectangle source = {};
};

struct SpriteHitmask {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> parts;
    std::vector<std::string> partNames;
};

struct SpriteAtlas {
    std::vector<Texture2D> textures;
    std::unordered_map<std::string, SpriteAtlasRect> rects;
    std::unordered_map<std::string, SpriteHitmask> hitmasks;
};

bool parseSpriteAsset(std::string_view source, SpriteAsset& asset);

SpriteAtlas buildSpriteAtlas(
    const SpriteAsset& asset,
    const std::function<std::optional<std::filesystem::path>(std::string_view)>& resolveTexturePath);

void unloadSpriteAtlas(SpriteAtlas& atlas);

const SpriteFrame* findSpriteFrame(const SpriteAsset& asset, std::string_view frameId);

const SpriteRotation* selectSpriteRotation(const SpriteFrame& frame, int rotation);

int doomRotationFromViewYaw(float relativeYawRadians);

std::uint8_t hitmaskPartAt(const SpriteHitmask& mask, int x, int y);

bool hitmaskTest(const SpriteHitmask& mask, int x, int y);

bool hitmaskTestUv(const SpriteHitmask& mask, float u, float v, bool mirror);

const char* hitmaskPartName(const SpriteHitmask& mask, std::uint8_t part);

}
