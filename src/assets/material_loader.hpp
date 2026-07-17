#pragma once

#include <raylib.h>

#include <functional>
#include <string>
#include <string_view>

namespace slopengine {

struct MaterialAsset {
    std::string shader = "default";
    Color baseColor = WHITE;
    std::string albedoTexture;
    float pixelsPerMeter = 64.0f;
    std::string emissionTexture;
    Color emissionColor = {0, 0, 0, 255};
    float emissionPower = 0.0f;
};

using TextureResolver = std::function<Texture2D(std::string_view path)>;

bool parseMaterialAsset(std::string_view source, MaterialAsset& asset);

Material createRaylibMaterial(const MaterialAsset& asset, const TextureResolver& resolveTexture = {});

}
