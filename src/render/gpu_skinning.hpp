#pragma once

#include <raylib.h>

namespace slopengine {

inline constexpr int kMaxGpuSkinBones = 128;

struct ShaderParams;

bool modelUsesGpuSkinning(const Model& model);

void setActiveShaderParams(const ShaderParams* params);

void drawModelMesh(
    const Model& model,
    int meshIndex,
    const Material& material,
    Shader skinFallback = {},
    Matrix transform = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f});

}
