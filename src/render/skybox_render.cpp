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

void drawSkyCubeMesh() {
    Mesh& mesh = skyboxCubeMesh();
    if (!rlEnableVertexArray(mesh.vaoId)) {
        return;
    }
    rlDrawVertexArrayElements(0, mesh.triangleCount * 3, 0);
    rlDisableVertexArray();
}

constexpr int kSkyCubeTextureUnit = 12;
constexpr int kSkyCylinderTextureUnit = 13;

std::string cubemapKey(const SkyboxSettings& settings) {
    std::string key;
    for (const std::string& face : settings.cubeFaces) {
        key.append(face);
        key.push_back('\0');
    }
    return key;
}

int locateShaderArray(Shader shader, const char* baseName) {
    int loc = GetShaderLocation(shader, baseName);
    if (loc < 0) {
        loc = GetShaderLocation(shader, TextFormat("%s[0]", baseName));
    }
    return loc;
}

// Matches UZDoom/GZDoom's sky cap color: a flat average of the texture's
// top/bottom rows, used to fill the sky above/below the wrapped band instead
// of stretching or pinching the texture toward the poles.
Vector3 averageTextureRowBand(Texture2D texture, int rowStart, int rowCount) {
    Image image = LoadImageFromTexture(texture);
    if (image.data == nullptr) {
        return {1.0f, 1.0f, 1.0f};
    }
    rowStart = static_cast<int>(Clamp(static_cast<float>(rowStart), 0.0f, static_cast<float>(image.height - 1)));
    rowCount = static_cast<int>(
        Clamp(static_cast<float>(rowCount), 1.0f, static_cast<float>(image.height - rowStart)));

    double sumR = 0.0;
    double sumG = 0.0;
    double sumB = 0.0;
    const long long sampleCount = static_cast<long long>(rowCount) * image.width;
    for (int y = rowStart; y < rowStart + rowCount; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const Color pixel = GetImageColor(image, x, y);
            sumR += pixel.r;
            sumG += pixel.g;
            sumB += pixel.b;
        }
    }
    UnloadImage(image);

    if (sampleCount <= 0) {
        return {1.0f, 1.0f, 1.0f};
    }
    return {
        static_cast<float>(sumR / sampleCount / 255.0),
        static_cast<float>(sumG / sampleCount / 255.0),
        static_cast<float>(sumB / sampleCount / 255.0),
    };
}

void computeSkyCylinderCapColors(SkyboxShaderState& shaderState) {
    const Texture2D& texture = shaderState.boundCylinderTexture;
    if (texture.id == 0) {
        return;
    }
    constexpr int kCapRows = 30;
    shaderState.boundCylinderTopColor = averageTextureRowBand(texture, 0, kCapRows);
    if (texture.height > kCapRows) {
        shaderState.boundCylinderBottomColor =
            averageTextureRowBand(texture, texture.height - kCapRows, kCapRows);
    } else {
        shaderState.boundCylinderBottomColor = shaderState.boundCylinderTopColor;
    }
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
    const int skyCylinderLoc = GetShaderLocation(shader, "skyCylinder");
    const int skyCylinderOffsetLoc = GetShaderLocation(shader, "skyCylinderOffset");
    const int skyCylinderScaleLoc = GetShaderLocation(shader, "skyCylinderScale");
    const int skyCylinderRepeatLoc = GetShaderLocation(shader, "skyCylinderRepeat");
    const int skyCylinderTopColorLoc = GetShaderLocation(shader, "skyCylinderTopColor");
    const int skyCylinderBottomColorLoc = GetShaderLocation(shader, "skyCylinderBottomColor");
    const int skyGradientColorsLoc = locateShaderArray(shader, "skyGradientColors");
    const int skyGradientPositionsLoc = locateShaderArray(shader, "skyGradientPositions");

    if (skyModeLoc >= 0) {
        SetShaderValue(shader, skyModeLoc, &skyMode, SHADER_UNIFORM_INT);
    }
    if (skySolidColorLoc >= 0) {
        SetShaderValue(shader, skySolidColorLoc, solidColor, SHADER_UNIFORM_VEC3);
    }
    if (skyGradientColorsLoc >= 0) {
        SetShaderValueV(shader, skyGradientColorsLoc, gradientColors, SHADER_UNIFORM_VEC4, 4);
    }
    if (skyGradientPositionsLoc >= 0) {
        SetShaderValueV(shader, skyGradientPositionsLoc, gradientPositions, SHADER_UNIFORM_FLOAT, 4);
    }

    if (settings.mode == SkyboxMode::Cube && skyCubeLoc >= 0) {
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
            rlActiveTextureSlot(kSkyCubeTextureUnit);
            rlEnableTextureCubemap(shaderState.boundCubemap.id);
            SetShaderValue(shader, skyCubeLoc, &kSkyCubeTextureUnit, SHADER_UNIFORM_INT);
        }
    }

    if (settings.mode == SkyboxMode::Cylinder && skyCylinderLoc >= 0) {
        if (shaderState.boundCylinderTexture.id == 0 ||
            shaderState.boundCylinderTextureKey != settings.cylinderTexture) {
            shaderState.boundCylinderTexture = assets.getTexture(settings.cylinderTexture);
            shaderState.boundCylinderTextureKey = settings.cylinderTexture;
            if (shaderState.boundCylinderTexture.id != 0) {
                // S (yaw) wraps around the horizon like a real cylinder; T (elevation) is
                // clamped since the shader hard-cuts to a flat cap color past v = [0, 1].
                rlTextureParameters(
                    shaderState.boundCylinderTexture.id, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_REPEAT);
                rlTextureParameters(
                    shaderState.boundCylinderTexture.id, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
                GenTextureMipmaps(&shaderState.boundCylinderTexture);
                SetTextureFilter(shaderState.boundCylinderTexture, TEXTURE_FILTER_TRILINEAR);
                computeSkyCylinderCapColors(shaderState);
            }
        }
        if (shaderState.boundCylinderTexture.id != 0) {
            rlActiveTextureSlot(kSkyCylinderTextureUnit);
            rlEnableTexture(shaderState.boundCylinderTexture.id);
            SetShaderValue(shader, skyCylinderLoc, &kSkyCylinderTextureUnit, SHADER_UNIFORM_INT);
        }
        if (skyCylinderOffsetLoc >= 0) {
            SetShaderValue(shader, skyCylinderOffsetLoc, &settings.cylinderOffset, SHADER_UNIFORM_FLOAT);
        }
        if (skyCylinderScaleLoc >= 0) {
            SetShaderValue(shader, skyCylinderScaleLoc, &settings.cylinderScale, SHADER_UNIFORM_FLOAT);
        }
        if (skyCylinderRepeatLoc >= 0) {
            SetShaderValue(shader, skyCylinderRepeatLoc, &settings.cylinderRepeat, SHADER_UNIFORM_INT);
        }
        if (skyCylinderTopColorLoc >= 0) {
            const float topColor[3] = {
                shaderState.boundCylinderTopColor.x,
                shaderState.boundCylinderTopColor.y,
                shaderState.boundCylinderTopColor.z,
            };
            SetShaderValue(shader, skyCylinderTopColorLoc, topColor, SHADER_UNIFORM_VEC3);
        }
        if (skyCylinderBottomColorLoc >= 0) {
            const float bottomColor[3] = {
                shaderState.boundCylinderBottomColor.x,
                shaderState.boundCylinderBottomColor.y,
                shaderState.boundCylinderBottomColor.z,
            };
            SetShaderValue(shader, skyCylinderBottomColorLoc, bottomColor, SHADER_UNIFORM_VEC3);
        }
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
    drawSkyCubeMesh();
    EndShaderMode();
    rlEnableDepthMask();
}

}
