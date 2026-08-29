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
    std::string textureAnimPath; /**< .texanim virtual path, or empty. */
    float pixelsPerMeter = 64.0f; /**< World texel density (texel-size). */
    float ior = 1.0f; /**< Bake-time index of refraction; 1.0 = no bend. */
    std::string emissionTexture;
    Color emissionColor = {0, 0, 0, 255};
    float emissionPower = 0.0f;
    float emissionRange = 0.0f; /**< Max cast distance in world units; 0 = unlimited. */
    /** Bake-time: scale the direct-light sample density with face length so emission-mask
     *  features (light strips, point clusters) cast as distinct shapes instead of smearing
     *  into one continuous glow across long faces. Costs more bake time; off by default. */
    bool preciseEmission = false;
    bool exactEmission = false;
    bool fullbright = false; /**< Render-time: mix masked-emissive texels to raw albedo, ignoring lighting. */
    /** Render-time, only relevant when fullbright is set: "add" (false, default) also adds the
     *  HDR emissionColor*emissionPower glow on top of the fully-lit masked texels, good for small
     *  lamp/strip masks that should read as a light source. "multiply" (true) skips that additive
     *  spike and only guarantees fully-lit shading, so a large mask (e.g. a whole monitor screen)
     *  keeps its own texture contrast instead of getting washed out by a flat HDR boost. */
    bool emissionMultiply = false;
    /** Render-time strength knob for the fullbright blend, independent of emissionPower (which
     *  also drives bake-time light cast onto neighboring surfaces - this only affects how this
     *  material itself renders). In "add" mode it scales the additive emission spike. In
     *  "multiply" mode it's the brightness target the masked texels are pushed to (1.0 = neutral
     *  fully-lit, as if unmasked; >1.0 pushes brighter while keeping albedo's own color ratios,
     *  since it's a uniform multiply rather than an injected emission color). Default 1.0. */
    float emissionBlendScale = 1.0f;
    /** Bake-time: add this face's own emission into its own baked irradiance (self-illumination
     *  physically baked onto the surface). Off by default - emission still lights other surfaces
     *  via radiosity and is still shown via the render-time emission mask/fullbright; this only
     *  controls whether the surface bakes a glow onto itself. Opt in for an intentional soft
     *  baked-glow look instead of the crisp render-time mask. */
    bool bakeEmission = false;
    bool sky = false; /**< Bake-time sun aperture; not a lightmap receiver. */
    SkyboxMode skyMode = SkyboxMode::Solid;
    bool haveSkyMode = false;
    Vector3 skySolidColor{0.4f, 0.7f, 1.0f};
    std::string skyCubeFaces[6];
    std::array<SkyGradientStop, 4> skyGradientStops{};
    int skyGradientStopCount = 0;
    std::string skyCylinderTexture;
    float skyCylinderOffset = 0.0f;
    float skyCylinderScale = 1.0f;
    int skyCylinderRepeat = 1;
};

using TextureResolver = std::function<Texture2D(std::string_view path)>;
using TextureAnimFrameResolver = std::function<Texture2D(std::string_view animPath, int frameIndex)>;

/** Parses .mat text into @p asset. */
bool parseMaterialAsset(std::string_view source, MaterialAsset& asset);

/** Builds a raylib Material from @p asset, resolving textures via @p resolveTexture. */
Material createRaylibMaterial(
    const MaterialAsset& asset,
    const TextureResolver& resolveTexture = {},
    const TextureAnimFrameResolver& resolveAnimFrame = {});

}
