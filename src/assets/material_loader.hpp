#pragma once

#include <raylib.h>

#include <functional>
#include <string>
#include <string_view>

namespace daggerlike {

struct MaterialAsset {
    std::string shader = "default";
    Color baseColor = WHITE;
    std::string albedoTexture;
};

using TextureResolver = std::function<Texture2D(std::string_view path)>;

bool parseMaterialAsset(std::string_view source, MaterialAsset& asset);
Material createRaylibMaterial(const MaterialAsset& asset, const TextureResolver& resolveTexture = {});

}
