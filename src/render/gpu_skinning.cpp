#include "render/gpu_skinning.hpp"

#include "render/components.hpp"

#include <rlgl.h>
#include "external/glad.h"

#include <cmath>
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

void uploadDeform(Shader shader, const Material& material, bool reset) {
    if (shader.id == 0) {
        return;
    }
    DeformLocs& locs = deformLocs(shader);
    const float time = static_cast<float>(std::floor(GetTime() * 240.0) / 240.0);
    const int bound = material.maps != nullptr && material.maps[MATERIAL_MAP_HEIGHT].texture.id != 0 ? 1 : 0;
    const bool setTime = !reset && locs.time >= 0 && time != locs.lastTime;
    const bool setBound = !reset && locs.maskBound >= 0 && bound != locs.lastMaskBound;
    if (!setTime && !setBound && g_activeShaderParams == nullptr) {
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
    if (g_activeShaderParams != nullptr) {
        const Vector4 zero{0.0f, 0.0f, 0.0f, 0.0f};
        for (const auto& [name, value] : g_activeShaderParams->values) {
            auto it = locs.params.find(name);
            if (it == locs.params.end()) {
                it = locs.params.emplace(name, glGetUniformLocation(shader.id, name.c_str())).first;
            }
            if (it->second >= 0) {
                rlSetUniform(it->second, reset ? &zero : &value, RL_SHADER_UNIFORM_VEC4, 1);
            }
        }
    }
    rlDisableShader();
}

void drawDeformedMesh(const Mesh& mesh, const Material& material, Matrix transform) {
    uploadDeform(material.shader, material, false);
    DrawMesh(mesh, material, transform);
    uploadDeform(material.shader, material, true);
}

}

void setActiveShaderParams(const ShaderParams* params) {
    g_activeShaderParams = params;
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
    if (!meshHasBoneBuffers(mesh) || !modelUsesGpuSkinning(model)) {
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

    rlEnableShader(drawMaterial.shader.id);
    rlSetUniformMatrices(locs.bones, model.boneMatrices, model.skeleton.boneCount);
    rlDisableShader();
    setSkinned(drawMaterial.shader, locs.skinned, 1);
    drawDeformedMesh(mesh, drawMaterial, transform);
    setSkinned(drawMaterial.shader, locs.skinned, 0);
}

}
