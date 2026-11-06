#include "render/gpu_skinning.hpp"

#include "core/frame_counters.hpp"
#include "render/components.hpp"

#include <raymath.h>
#include <rlgl.h>
#include "external/glad.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

namespace slopengine {

namespace {

struct SkinningLocs {
    int bones = -1;
    int skinned = -1;
};

SkinningLocs skinningLocs(Shader shader) {
    static std::unordered_map<unsigned int, SkinningLocs> cache;
    auto [it, inserted] = cache.try_emplace(shader.id);
    if (inserted) {
        it->second.bones = GetShaderLocation(shader, "boneMatrices");
        it->second.skinned = GetShaderLocation(shader, "skinned");
    }
    return it->second;
}

bool meshHasBoneBuffers(const Mesh& mesh) {
    return mesh.boneIndices != nullptr && mesh.boneWeights != nullptr && mesh.vboId != nullptr &&
        mesh.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_BONEINDICES] != 0 &&
        mesh.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_BONEWEIGHTS] != 0;
}

void setSkinned(Shader shader, int loc, int value) {
    if (loc < 0) {
        return;
    }
    rlEnableShader(shader.id);
    rlSetUniform(loc, &value, RL_SHADER_UNIFORM_INT, 1);
    rlDisableShader();
}

const ShaderParams* g_activeShaderParams = nullptr;

struct DeformLocs {
    int time = -1;
    int maskBound = -1;
    float lastTime = -1.0f;
    int lastMaskBound = -1;
    std::unordered_map<std::string, int> params;
};

DeformLocs& deformLocs(Shader shader) {
    static std::unordered_map<unsigned int, DeformLocs> cache;
    auto [it, inserted] = cache.try_emplace(shader.id);
    if (inserted) {
        it->second.time = glGetUniformLocation(shader.id, "deformTime");
        it->second.maskBound = glGetUniformLocation(shader.id, "deformMaskBound");
    }
    return it->second;
}

const ShaderParams* g_uploadedParams = nullptr;
unsigned int g_uploadedParamsShader = 0;

void clearUploadedParams() {
    if (g_uploadedParams != nullptr && g_uploadedParamsShader != 0) {
        Shader shader{};
        shader.id = g_uploadedParamsShader;
        DeformLocs& locs = deformLocs(shader);
        const Vector4 zero{0.0f, 0.0f, 0.0f, 0.0f};
        rlEnableShader(shader.id);
        for (const auto& [name, value] : g_uploadedParams->values) {
            const auto it = locs.params.find(name);
            if (it != locs.params.end() && it->second >= 0) {
                rlSetUniform(it->second, &zero, RL_SHADER_UNIFORM_VEC4, 1);
            }
        }
        rlDisableShader();
    }
    g_uploadedParams = nullptr;
    g_uploadedParamsShader = 0;
}

void uploadDeform(Shader shader, const Material& material) {
    if (shader.id == 0) {
        return;
    }
    DeformLocs& locs = deformLocs(shader);
    const float time = static_cast<float>(std::floor(GetTime() * 240.0) / 240.0);
    const int bound = material.maps != nullptr && material.maps[MATERIAL_MAP_HEIGHT].texture.id != 0 ? 1 : 0;
    const bool setTime = locs.time >= 0 && time != locs.lastTime;
    const bool setBound = locs.maskBound >= 0 && bound != locs.lastMaskBound;
    const bool setParams = g_activeShaderParams != nullptr &&
        (g_uploadedParams != g_activeShaderParams || g_uploadedParamsShader != shader.id);
    if (!setTime && !setBound && !setParams) {
        return;
    }
    rlEnableShader(shader.id);
    if (setTime) {
        rlSetUniform(locs.time, &time, RL_SHADER_UNIFORM_FLOAT, 1);
        locs.lastTime = time;
    }
    if (setBound) {
        rlSetUniform(locs.maskBound, &bound, RL_SHADER_UNIFORM_INT, 1);
        locs.lastMaskBound = bound;
    }
    if (setParams) {
        for (const auto& [name, value] : g_activeShaderParams->values) {
            auto it = locs.params.find(name);
            if (it == locs.params.end()) {
                it = locs.params.emplace(name, glGetUniformLocation(shader.id, name.c_str())).first;
            }
            if (it->second >= 0) {
                rlSetUniform(it->second, &value, RL_SHADER_UNIFORM_VEC4, 1);
            }
        }
        g_uploadedParams = g_activeShaderParams;
        g_uploadedParamsShader = shader.id;
    }
    rlDisableShader();
}

constexpr int kMaterialMapCount = 12;

struct LeanShaderState {
    std::uint32_t serial = 0;
    Matrix view{};
    Matrix projection{};
    bool viewValid = false;
    bool projectionValid = false;
};

Vector4 colorToVector4(Color color) {
    return {
        static_cast<float>(color.r) / 255.0f,
        static_cast<float>(color.g) / 255.0f,
        static_cast<float>(color.b) / 255.0f,
        static_cast<float>(color.a) / 255.0f,
    };
}

bool sameMatrix(const Matrix& a, const Matrix& b) {
    return std::memcmp(&a, &b, sizeof(Matrix)) == 0;
}

void drawMeshLean(const Mesh& mesh, const Material& material, Matrix transform) {
    const Shader& shader = material.shader;
    if (mesh.vaoId == 0 || shader.id == 0 || shader.locs == nullptr || rlIsStereoRenderEnabled()) {
        DrawMesh(mesh, material, transform);
        return;
    }
    static std::unordered_map<unsigned int, LeanShaderState> states;
    LeanShaderState& state = states[shader.id];
    if (state.serial != renderFrameSerial()) {
        state = {};
        state.serial = renderFrameSerial();
    }

    rlEnableShader(shader.id);
    if (shader.locs[SHADER_LOC_COLOR_DIFFUSE] != -1) {
        const Vector4 diffuse = colorToVector4(material.maps[MATERIAL_MAP_DIFFUSE].color);
        rlSetUniform(shader.locs[SHADER_LOC_COLOR_DIFFUSE], &diffuse, SHADER_UNIFORM_VEC4, 1);
    }
    if (shader.locs[SHADER_LOC_COLOR_SPECULAR] != -1) {
        const Vector4 specular = colorToVector4(material.maps[MATERIAL_MAP_SPECULAR].color);
        rlSetUniform(shader.locs[SHADER_LOC_COLOR_SPECULAR], &specular, SHADER_UNIFORM_VEC4, 1);
    }

    const Matrix matView = rlGetMatrixModelview();
    const Matrix matProjection = rlGetMatrixProjection();
    if (shader.locs[SHADER_LOC_MATRIX_VIEW] != -1 && (!state.viewValid || !sameMatrix(state.view, matView))) {
        rlSetUniformMatrix(shader.locs[SHADER_LOC_MATRIX_VIEW], matView);
        state.view = matView;
        state.viewValid = true;
    }
    if (shader.locs[SHADER_LOC_MATRIX_PROJECTION] != -1 &&
        (!state.projectionValid || !sameMatrix(state.projection, matProjection))) {
        rlSetUniformMatrix(shader.locs[SHADER_LOC_MATRIX_PROJECTION], matProjection);
        state.projection = matProjection;
        state.projectionValid = true;
    }
    const Matrix matModel = MatrixMultiply(transform, rlGetMatrixTransform());
    if (shader.locs[SHADER_LOC_MATRIX_MODEL] != -1) {
        rlSetUniformMatrix(shader.locs[SHADER_LOC_MATRIX_MODEL], matModel);
    }
    const Matrix matModelView = MatrixMultiply(matModel, matView);
    if (shader.locs[SHADER_LOC_MATRIX_NORMAL] != -1) {
        rlSetUniformMatrix(shader.locs[SHADER_LOC_MATRIX_NORMAL], MatrixTranspose(MatrixInvert(matModel)));
    }

    for (int i = 0; i < kMaterialMapCount; i++) {
        if (material.maps[i].texture.id > 0) {
            rlActiveTextureSlot(i);
            if (i == MATERIAL_MAP_IRRADIANCE || i == MATERIAL_MAP_PREFILTER || i == MATERIAL_MAP_CUBEMAP) {
                rlEnableTextureCubemap(material.maps[i].texture.id);
            } else {
                rlEnableTexture(material.maps[i].texture.id);
            }
            rlSetUniform(shader.locs[SHADER_LOC_MAP_DIFFUSE + i], &i, SHADER_UNIFORM_INT, 1);
        }
    }

    if (!rlEnableVertexArray(mesh.vaoId)) {
        DrawMesh(mesh, material, transform);
        return;
    }
    const Matrix matModelViewProjection = MatrixMultiply(matModelView, matProjection);
    rlSetUniformMatrix(shader.locs[SHADER_LOC_MATRIX_MVP], matModelViewProjection);
    if (mesh.indices != nullptr) {
        rlDrawVertexArrayElements(0, mesh.triangleCount * 3, nullptr);
    } else {
        rlDrawVertexArray(0, mesh.vertexCount);
    }
    for (int i = 0; i < kMaterialMapCount; i++) {
        if (material.maps[i].texture.id > 0) {
            rlActiveTextureSlot(i);
            if (i == MATERIAL_MAP_IRRADIANCE || i == MATERIAL_MAP_PREFILTER || i == MATERIAL_MAP_CUBEMAP) {
                rlDisableTextureCubemap();
            } else {
                rlDisableTexture();
            }
        }
    }
    rlActiveTextureSlot(0);
}

void drawDeformedMesh(const Mesh& mesh, const Material& material, Matrix transform) {
    uploadDeform(material.shader, material);
    drawMeshLean(mesh, material, transform);
}

struct ModelDrawSession {
    const Model* model = nullptr;
    bool gpuSkinned = false;
    unsigned int bonesShader = 0;
};

ModelDrawSession g_modelDraw;

}

void setActiveShaderParams(const ShaderParams* params) {
    if (params != g_activeShaderParams) {
        clearUploadedParams();
    }
    g_activeShaderParams = params;
}

void beginModelDraw(const Model& model) {
    g_modelDraw = {&model, modelUsesGpuSkinning(model), 0};
}

void endModelDraw() {
    if (g_modelDraw.bonesShader != 0) {
        Shader shader{};
        shader.id = g_modelDraw.bonesShader;
        setSkinned(shader, skinningLocs(shader).skinned, 0);
    }
    g_modelDraw = {};
}

bool modelUsesGpuSkinning(const Model& model) {
    if (model.boneMatrices == nullptr || model.skeleton.boneCount <= 0 ||
        model.skeleton.boneCount > kMaxGpuSkinBones || model.meshes == nullptr) {
        return false;
    }
    for (int meshIndex = 0; meshIndex < model.meshCount; ++meshIndex) {
        if (meshHasBoneBuffers(model.meshes[meshIndex])) {
            return true;
        }
    }
    return false;
}

void drawModelMesh(
    const Model& model,
    int meshIndex,
    const Material& material,
    Shader skinFallback,
    Matrix transform) {
    const Mesh& mesh = model.meshes[meshIndex];
    const bool session = g_modelDraw.model == &model;
    const bool gpuSkinned = session ? g_modelDraw.gpuSkinned : modelUsesGpuSkinning(model);
    if (!meshHasBoneBuffers(mesh) || !gpuSkinned) {
        drawDeformedMesh(mesh, material, transform);
        return;
    }

    Material drawMaterial = material;
    SkinningLocs locs = skinningLocs(material.shader);
    if (locs.bones < 0 && skinFallback.id != 0) {
        drawMaterial.shader = skinFallback;
        locs = skinningLocs(skinFallback);
    }
    if (locs.bones < 0) {
        drawDeformedMesh(mesh, material, transform);
        return;
    }

    if (session && g_modelDraw.bonesShader == drawMaterial.shader.id) {
        drawDeformedMesh(mesh, drawMaterial, transform);
        return;
    }
    if (session && g_modelDraw.bonesShader != 0) {
        Shader previous{};
        previous.id = g_modelDraw.bonesShader;
        setSkinned(previous, skinningLocs(previous).skinned, 0);
    }
    rlEnableShader(drawMaterial.shader.id);
    rlSetUniformMatrices(locs.bones, model.boneMatrices, model.skeleton.boneCount);
    rlDisableShader();
    setSkinned(drawMaterial.shader, locs.skinned, 1);
    drawDeformedMesh(mesh, drawMaterial, transform);
    if (session) {
        g_modelDraw.bonesShader = drawMaterial.shader.id;
    } else {
        setSkinned(drawMaterial.shader, locs.skinned, 0);
    }
}

void setEmissionPower(Shader shader, int loc, float value) {
    static std::unordered_map<unsigned int, float> last;
    if (shader.id == 0 || loc < 0) {
        return;
    }
    auto [it, inserted] = last.try_emplace(shader.id, -1.0f);
    if (!inserted && it->second == value) {
        return;
    }
    it->second = value;
    rlEnableShader(shader.id);
    rlSetUniform(loc, &value, RL_SHADER_UNIFORM_FLOAT, 1);
    rlDisableShader();
}

}
