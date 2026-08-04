#include "render/skybox_render.hpp"

#include "map/lightmap.hpp"

#include <raymath.h>
#include <rlgl.h>

#include <string>

namespace slopengine {

namespace {

Matrix viewRotationMatrix(Camera3D camera) {
    Matrix view = MatrixLookAt(camera.position, camera.target, camera.up);
    view.m12 = 0.0f;
    view.m13 = 0.0f;
    view.m14 = 0.0f;
    return view;
}

void uploadMat4Rotation(Shader shader, int location, const Matrix& matrix) {
    if (location < 0) {
        return;
    }
    SetShaderValueMatrix(shader, location, matrix);
}

Mesh& skyboxCubeMesh() {
    static Mesh mesh = GenMeshCube(1.0f, 1.0f, 1.0f);
    static bool uploaded = false;
    if (!uploaded) {
        UploadMesh(&mesh, false);
        uploaded = true;
    }
    return mesh;
}

std::string cubemapKey(const SkyboxSettings& settings) {
    std::string key;
    for (const std::string& face : settings.cubeFaces) {
        key.append(face);
        key.push_back('\0');
    }
    return key;
}

} // namespace

void applySkyShaderUniforms(
    Shader shader,
    AssetStore& assets,
    SkyboxShaderState& shaderState,
    const SkyboxSettings& settings) {
    if (shader.id == 0) {
        return;
    }

    const int skyMode = static_cast<int>(settings.mode);
    const float solidColor[3] = {settings.solidColor.x, settings.solidColor.y, settings.solidColor.z};
    float gradientColors[16] = {};
    float gradientPositions[4] = {};
    for (int i = 0; i < settings.gradientStopCount && i < 4; ++i) {
        const SkyGradientStop& stop = settings.gradientStops[static_cast<std::size_t>(i)];
        gradientPositions[i] = stop.position;
        gradientColors[i * 4 + 0] = stop.color.x;
        gradientColors[i * 4 + 1] = stop.color.y;
        gradientColors[i * 4 + 2] = stop.color.z;
        gradientColors[i * 4 + 3] = 1.0f;
    }

    const int skyModeLoc = GetShaderLocation(shader, "skyMode");
    const int skySolidColorLoc = GetShaderLocation(shader, "skySolidColor");
    const int skyCubeLoc = GetShaderLocation(shader, "skyCube");
    const int skyGradientColorsLoc = GetShaderLocation(shader, "skyGradientColors");
    const int skyGradientPositionsLoc = GetShaderLocation(shader, "skyGradientPositions");

    if (skyModeLoc >= 0) {
        SetShaderValue(shader, skyModeLoc, &skyMode, SHADER_UNIFORM_INT);
    }
    if (skySolidColorLoc >= 0) {
        SetShaderValue(shader, skySolidColorLoc, solidColor, SHADER_UNIFORM_VEC3);
    }
    if (skyGradientColorsLoc >= 0) {
        SetShaderValue(shader, skyGradientColorsLoc, gradientColors, SHADER_UNIFORM_VEC4);
    }
    if (skyGradientPositionsLoc >= 0) {
        SetShaderValue(shader, skyGradientPositionsLoc, gradientPositions, SHADER_UNIFORM_FLOAT);
    }

    if (settings.mode != SkyboxMode::Cube || skyCubeLoc < 0) {
        return;
    }

    const std::string key = cubemapKey(settings);
    if (shaderState.boundCubemap.id == 0 || shaderState.boundCubemapKey != key) {
        shaderState.boundCubemap = assets.getCubemapFaces(
            settings.cubeFaces[0],
            settings.cubeFaces[1],
            settings.cubeFaces[2],
            settings.cubeFaces[3],
            settings.cubeFaces[4],
            settings.cubeFaces[5]);
        shaderState.boundCubemapKey = key;
    }
    if (shaderState.boundCubemap.id != 0) {
        SetShaderValueTexture(shader, skyCubeLoc, shaderState.boundCubemap);
    }
}

SkyboxShaderState& ensureSkyboxShaders(AssetStore& assets) {
    static SkyboxShaderState state{};
    if (state.backgroundShader.id != 0 && state.faceShader.id != 0) {
        return state;
    }

    state.backgroundShader = loadSkyboxBackgroundShader(assets);
    state.faceShader = loadSkyFaceShader(assets);
    if (state.backgroundShader.id == 0 || state.faceShader.id == 0) {
        TraceLog(LOG_WARNING, "SKY: failed to load skybox shaders");
        return state;
    }

    state.backgroundMatProjectionLoc = GetShaderLocation(state.backgroundShader, "matProjection");
    state.backgroundMatViewRotLoc = GetShaderLocation(state.backgroundShader, "matViewRot");
    state.faceMatViewRotLoc = GetShaderLocation(state.faceShader, "matViewRot");
    state.faceCameraPosLoc = GetShaderLocation(state.faceShader, "cameraPos");
    return state;
}

void bindSkyboxUniforms(
    SkyboxShaderState& shaderState,
    AssetStore& assets,
    const SkyboxSettings& settings) {
    applySkyShaderUniforms(shaderState.backgroundShader, assets, shaderState, settings);
    applySkyShaderUniforms(shaderState.faceShader, assets, shaderState, settings);
}

void drawSkyboxBackground(
    Camera3D camera,
    AssetStore& assets,
    SkyboxShaderState& shaderState,
    const SkyboxSettings& settings) {
    if (shaderState.backgroundShader.id == 0) {
        ensureSkyboxShaders(assets);
    }
    if (shaderState.backgroundShader.id == 0) {
        return;
    }

    bindSkyboxUniforms(shaderState, assets, settings);

    const float aspect = GetRenderWidth() > 0 && GetRenderHeight() > 0
        ? static_cast<float>(GetRenderWidth()) / static_cast<float>(GetRenderHeight())
        : static_cast<float>(GetScreenWidth()) / static_cast<float>(GetScreenHeight());
    const Matrix projection = MatrixPerspective(
        camera.fovy * DEG2RAD,
        aspect,
        rlGetCullDistanceNear(),
        rlGetCullDistanceFar());
    const Matrix viewRot = viewRotationMatrix(camera);
    SetShaderValueMatrix(
        shaderState.backgroundShader,
        shaderState.backgroundMatProjectionLoc,
        projection);
    uploadMat4Rotation(
        shaderState.backgroundShader,
        shaderState.backgroundMatViewRotLoc,
        viewRot);

    rlDisableDepthMask();
    rlEnableDepthTest();
    BeginShaderMode(shaderState.backgroundShader);
    DrawMesh(skyboxCubeMesh(), Material{}, MatrixScale(1000.0f, 1000.0f, 1000.0f));
    EndShaderMode();
    rlEnableDepthMask();
}

}
