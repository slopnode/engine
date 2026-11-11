#include "render/anti_alias.hpp"

#include "assets/asset_store.hpp"

#include <string>

namespace slopengine {

namespace {

Shader loadPostShader(AssetStore& assets, std::string_view fragPath) {
    if (!assets.hasShader(fragPath) || !assets.hasShader("default/post_vert")) {
        return Shader{};
    }
    const std::string vert = assets.getShaderSource("default/post_vert");
    const std::string frag = assets.getShaderSource(fragPath);
    if (vert.empty() || frag.empty()) {
        return Shader{};
    }
    Shader compiled = LoadShaderFromMemory(vert.c_str(), frag.c_str());
    if (!IsShaderValid(compiled)) {
        if (compiled.id != 0) {
            UnloadShader(compiled);
        }
        return Shader{};
    }
    return compiled;
}

void unloadShader(Shader& shader) {
    if (shader.id != 0) {
        UnloadShader(shader);
    }
    shader = Shader{};
}

void unloadTarget(RenderTexture2D& target) {
    if (target.id != 0) {
        UnloadRenderTexture(target);
    }
    target = RenderTexture2D{};
}

void drawFullscreenFlippedY(const Texture2D& tex) {
    const Rectangle src{
        0.0f,
        0.0f,
        static_cast<float>(tex.width),
        -static_cast<float>(tex.height),
    };
    DrawTextureRec(tex, src, Vector2{0.0f, 0.0f}, WHITE);
}

void setResolution(const Shader& shader, int loc, int width, int height) {
    if (loc < 0) {
        return;
    }
    const float resolution[2] = {static_cast<float>(width), static_cast<float>(height)};
    SetShaderValue(shader, loc, resolution, SHADER_UNIFORM_VEC2);
}

bool prepareFxaa(AntiAliasState& state, AssetStore& assets) {
    if (state.fxaa.id != 0) {
        return true;
    }
    if (state.fxaaAttempted) {
        return false;
    }
    state.fxaaAttempted = true;
    state.fxaa = loadPostShader(assets, "default/fxaa_frag");
    if (state.fxaa.id == 0) {
        TraceLog(LOG_WARNING, "AA: failed to load default/fxaa_frag");
        return false;
    }
    state.fxaaResolutionLoc = GetShaderLocation(state.fxaa, "resolution");
    return true;
}

bool prepareSmaa(AntiAliasState& state, AssetStore& assets) {
    if (state.smaaBlend.id != 0) {
        return true;
    }
    if (state.smaaAttempted) {
        return false;
    }
    state.smaaAttempted = true;
    state.smaaEdges = loadPostShader(assets, "default/smaa_edges_frag");
    state.smaaWeights = loadPostShader(assets, "default/smaa_weights_frag");
    state.smaaBlend = loadPostShader(assets, "default/smaa_blend_frag");
    state.area = assets.getTexture("fx/smaa_area");
    state.search = assets.getTexture("fx/smaa_search");
    if (state.smaaEdges.id == 0 || state.smaaWeights.id == 0 || state.smaaBlend.id == 0 ||
        state.area.id == 0 || state.search.id == 0) {
        TraceLog(LOG_WARNING, "AA: failed to load SMAA shaders or lookup textures");
        unloadShader(state.smaaEdges);
        unloadShader(state.smaaWeights);
        unloadShader(state.smaaBlend);
        return false;
    }
    SetTextureFilter(state.area, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(state.area, TEXTURE_WRAP_CLAMP);
    SetTextureFilter(state.search, TEXTURE_FILTER_POINT);
    SetTextureWrap(state.search, TEXTURE_WRAP_CLAMP);
    state.edgesResolutionLoc = GetShaderLocation(state.smaaEdges, "resolution");
    state.weightsResolutionLoc = GetShaderLocation(state.smaaWeights, "resolution");
    state.weightsAreaLoc = GetShaderLocation(state.smaaWeights, "areaTex");
    state.weightsSearchLoc = GetShaderLocation(state.smaaWeights, "searchTex");
    state.blendResolutionLoc = GetShaderLocation(state.smaaBlend, "resolution");
    state.blendColorLoc = GetShaderLocation(state.smaaBlend, "colorTex");
    return true;
}

bool ensureTargets(AntiAliasState& state, AntiAliasMode mode, int width, int height) {
    const bool needsIntermediates = mode == AntiAliasMode::Smaa;
    const bool sized = state.targetW == width && state.targetH == height;
    if (sized && state.output.id != 0 && (!needsIntermediates || state.weights.id != 0)) {
        return true;
    }
    state.unloadTargets();
    state.output = LoadRenderTexture(width, height);
    if (needsIntermediates) {
        state.edges = LoadRenderTexture(width, height);
        state.weights = LoadRenderTexture(width, height);
    }
    if (state.output.id == 0 || (needsIntermediates && (state.edges.id == 0 || state.weights.id == 0))) {
        state.unloadTargets();
        return false;
    }
    SetTextureFilter(state.output.texture, TEXTURE_FILTER_POINT);
    if (needsIntermediates) {
        SetTextureFilter(state.edges.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureFilter(state.weights.texture, TEXTURE_FILTER_POINT);
    }
    state.targetW = width;
    state.targetH = height;
    return true;
}

} // namespace

const char* antiAliasId(AntiAliasMode mode) {
    switch (mode) {
    case AntiAliasMode::Off:
        return "off";
    case AntiAliasMode::Fxaa:
        return "fxaa";
    case AntiAliasMode::Smaa:
        return "smaa";
    }
    return "off";
}

const char* antiAliasLabel(AntiAliasMode mode) {
    switch (mode) {
    case AntiAliasMode::Off:
        return "Off";
    case AntiAliasMode::Fxaa:
        return "FXAA";
    case AntiAliasMode::Smaa:
        return "SMAA";
    }
    return "Off";
}

std::optional<AntiAliasMode> antiAliasFromId(std::string_view id) {
    if (id == "off") {
        return AntiAliasMode::Off;
    }
    if (id == "fxaa") {
        return AntiAliasMode::Fxaa;
    }
    if (id == "smaa") {
        return AntiAliasMode::Smaa;
    }
    return std::nullopt;
}

AntiAliasState::~AntiAliasState() {
    unloadTargets();
    unloadShader(fxaa);
    unloadShader(smaaEdges);
    unloadShader(smaaWeights);
    unloadShader(smaaBlend);
}

void AntiAliasState::unloadTargets() {
    unloadTarget(edges);
    unloadTarget(weights);
    unloadTarget(output);
    targetW = 0;
    targetH = 0;
}

bool prepareAntiAlias(
    AntiAliasState& state,
    AssetStore& assets,
    AntiAliasMode mode,
    int width,
    int height) {
    if (mode == AntiAliasMode::Off || width <= 0 || height <= 0) {
        return false;
    }
    const bool shadersReady =
        mode == AntiAliasMode::Fxaa ? prepareFxaa(state, assets) : prepareSmaa(state, assets);
    return shadersReady && ensureTargets(state, mode, width, height);
}

Texture2D runAntiAlias(AntiAliasState& state, AntiAliasMode mode, Texture2D input, bool toScreen) {
    const int width = state.targetW;
    const int height = state.targetH;
    SetTextureFilter(input, TEXTURE_FILTER_BILINEAR);

    if (mode == AntiAliasMode::Fxaa) {
        if (!toScreen) {
            BeginTextureMode(state.output);
            ClearBackground(BLACK);
        }
        setResolution(state.fxaa, state.fxaaResolutionLoc, width, height);
        BeginShaderMode(state.fxaa);
        drawFullscreenFlippedY(input);
        EndShaderMode();
        if (!toScreen) {
            EndTextureMode();
            return state.output.texture;
        }
        return input;
    }

    BeginTextureMode(state.edges);
    ClearBackground(BLANK);
    setResolution(state.smaaEdges, state.edgesResolutionLoc, width, height);
    BeginShaderMode(state.smaaEdges);
    drawFullscreenFlippedY(input);
    EndShaderMode();
    EndTextureMode();

    BeginTextureMode(state.weights);
    ClearBackground(BLANK);
    setResolution(state.smaaWeights, state.weightsResolutionLoc, width, height);
    BeginShaderMode(state.smaaWeights);
    SetShaderValueTexture(state.smaaWeights, state.weightsAreaLoc, state.area);
    SetShaderValueTexture(state.smaaWeights, state.weightsSearchLoc, state.search);
    drawFullscreenFlippedY(state.edges.texture);
    EndShaderMode();
    EndTextureMode();

    if (!toScreen) {
        BeginTextureMode(state.output);
        ClearBackground(BLACK);
    }
    setResolution(state.smaaBlend, state.blendResolutionLoc, width, height);
    BeginShaderMode(state.smaaBlend);
    SetShaderValueTexture(state.smaaBlend, state.blendColorLoc, input);
    drawFullscreenFlippedY(state.weights.texture);
    EndShaderMode();
    if (!toScreen) {
        EndTextureMode();
        return state.output.texture;
    }
    return input;
}

}
