#pragma once

#include "map/sky_types.hpp"

#include <raylib.h>

#include <array>
#include <functional>
#include <string>
#include <string_view>

namespace slopengine {

/** Parsed .mat material fields (albedo + emission; no PBR maps). */
struct MaterialAsset {
    std::string shader = "default";
    Color baseColor = WHITE;
    std::string albedoTexture; /**< Texture virtual path, or empty. */
    float pixelsPerMeter = 64.0f; /**< World texel density (texel-size). */
    std::string emissionTexture;
    Color emissionColor = {0, 0, 0, 255};
    float emissionPower = 0.0f;
    float emissionRange = 0.0f; /**< Max cast distance in world units; 0 = unlimited. */
    bool sky = false; /**< Bake-time sun aperture; not a lightmap receiver. */
    SkyboxMode skyMode = SkyboxMode::Solid;
    bool haveSkyMode = false;
    Vector3 skySolidColor{0.4f, 0.7f, 1.0f};
    std::string skyCubeFaces[6];
    std::array<SkyGradientStop, 4> skyGradientStops{};
    int skyGradientStopCount = 0;
};

using TextureResolver = std::function<Texture2D(std::string_view path)>;

/** Parses .mat text into @p asset. */
bool parseMaterialAsset(std::string_view source, MaterialAsset& asset);

/** Builds a raylib Material from @p asset, resolving textures via @p resolveTexture. */
Material createRaylibMaterial(const MaterialAsset& asset, const TextureResolver& resolveTexture = {});

}
