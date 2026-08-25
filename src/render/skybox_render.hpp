#pragma once

#include "assets/asset_store.hpp"
#include "render/skybox.hpp"

#include <raylib.h>

namespace slopengine {

struct SkyboxShaderState {
    Shader backgroundShader{};
    Shader faceShader{};
    int backgroundMatProjectionLoc = -1;
    int backgroundMatViewRotLoc = -1;
    int faceMatViewRotLoc = -1;
    int faceCameraPosLoc = -1;
    TextureCubemap boundCubemap{};
    std::string boundCubemapKey;
    Texture2D boundCylinderTexture{};
    std::string boundCylinderTextureKey;
    Vector3 boundCylinderTopColor{1.0f, 1.0f, 1.0f};
    Vector3 boundCylinderBottomColor{1.0f, 1.0f, 1.0f};
};

/** Loads or returns cached skybox draw shaders. */
SkyboxShaderState& ensureSkyboxShaders(AssetStore& assets);

/** Uploads sky uniforms to a single shader (map sky faces or editor preview). */
void applySkyShaderUniforms(
    Shader shader,
    AssetStore& assets,
    SkyboxShaderState& shaderState,
    const SkyboxSettings& settings);

/** Uploads sky uniforms and binds cubemap textures when needed. */
void bindSkyboxUniforms(
    SkyboxShaderState& shaderState,
    AssetStore& assets,
    const SkyboxSettings& settings);

/** Draws the infinite background skybox as the first world pass. */
void drawSkyboxBackground(
    Camera3D camera,
    AssetStore& assets,
    SkyboxShaderState& shaderState,
    const SkyboxSettings& settings);

}
