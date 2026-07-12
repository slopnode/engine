#pragma once

#include <raylib.h>

#include <functional>
#include <string>
#include <string_view>

namespace slopengine {

/** Parsed material definition loaded from a .mat file. */
struct MaterialAsset {
    std::string shader = "default";
    Color baseColor = WHITE;
    std::string albedoTexture;
};

using TextureResolver = std::function<Texture2D(std::string_view path)>;

/** Parses a material asset from its text source into @p asset. */
bool parseMaterialAsset(std::string_view source, MaterialAsset& asset);

/** Creates a raylib material from @p asset, resolving textures through @p resolveTexture. */
Material createRaylibMaterial(const MaterialAsset& asset, const TextureResolver& resolveTexture = {});

}
