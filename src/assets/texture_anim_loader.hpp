#pragma once

#include "render/material_anim_types.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace slopengine {

/** One timed frame inside a texture clip. */
struct TextureAnimFrame {
    std::string texture; /**< Virtual texture path. */
    float duration = 0.0f; /**< Hold time in seconds. */
};

/** One named clip from a .texanim file. */
struct TextureAnimClip {
    std::string name;
    bool loop = true;
    std::vector<TextureAnimFrame> frames;
};

/** Parsed texture animation bank with name lookup. */
struct TextureAnimBank {
    std::vector<TextureAnimClip> clips;
    std::unordered_map<std::string, std::size_t> clipIndexByName;
};

/** Parses .texanim text into @p bank. */
bool parseTextureAnimBank(std::string_view source, TextureAnimBank& bank);

/** Serializes @p bank to .texanim text. */
std::string serializeTextureAnimBank(const TextureAnimBank& bank);

/** Returns clip duration in seconds, or 0 when empty. */
float textureAnimClipDuration(const TextureAnimClip& clip);

/** Resolves @p time within @p clip to a frame index. */
int textureAnimFrameIndexAt(const TextureAnimClip& clip, float time);

/** Returns the texture path for @p frameIndex in @p clip, or empty when out of range. */
std::string_view textureAnimFrameTexture(const TextureAnimClip& clip, int frameIndex);

class AssetStore;
struct GeoAsset;

/** Fills @p out with animated material bindings from @p geo. */
void collectMaterialAnimTargets(const GeoAsset& geo, AssetStore& assets, MaterialAnimTargets& out);

}
