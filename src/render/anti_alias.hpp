#pragma once

#include <raylib.h>

#include <optional>
#include <string_view>

namespace slopengine {

class AssetStore;

enum class AntiAliasMode {
    Off,
    Fxaa,
    Smaa,
};

const char* antiAliasId(AntiAliasMode mode);
const char* antiAliasLabel(AntiAliasMode mode);
std::optional<AntiAliasMode> antiAliasFromId(std::string_view id);

struct AntiAliasState {
    Shader fxaa{};
    Shader smaaEdges{};
    Shader smaaWeights{};
    Shader smaaBlend{};

    int fxaaResolutionLoc = -1;
    int edgesResolutionLoc = -1;
    int weightsResolutionLoc = -1;
    int weightsAreaLoc = -1;
    int weightsSearchLoc = -1;
    int blendResolutionLoc = -1;
    int blendColorLoc = -1;

    Texture2D area{};
    Texture2D search{};

    RenderTexture2D edges{};
    RenderTexture2D weights{};
    RenderTexture2D output{};
    int targetW = 0;
    int targetH = 0;

    bool fxaaAttempted = false;
    bool smaaAttempted = false;

    AntiAliasState() = default;
    AntiAliasState(const AntiAliasState&) = delete;
    AntiAliasState& operator=(const AntiAliasState&) = delete;
    ~AntiAliasState();

    void unloadTargets();
};

bool prepareAntiAlias(
    AntiAliasState& state,
    AssetStore& assets,
    AntiAliasMode mode,
    int width,
    int height);

Texture2D runAntiAlias(AntiAliasState& state, AntiAliasMode mode, Texture2D input, bool toScreen);

}
