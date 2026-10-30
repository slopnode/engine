#pragma once

#include <flecs.h>
#include <raylib.h>

#include <unordered_map>
#include <vector>

namespace slopengine {

constexpr int kMaxModelStaticLights = 4;
constexpr float kModelStaticLightWrap = 0.35f;

struct ModelLightingUniforms {
    Vector3 probeSH[4]{};
    int lightCount = 0;
    Vector4 lightPosRange[kMaxModelStaticLights]{};
    Vector4 lightColor[kMaxModelStaticLights]{};
    Vector4 lightDirCone[kMaxModelStaticLights]{};
};

struct StaticModelLight {
    flecs::entity_t id = 0;
    Vector3 position{};
    Vector3 direction{0.0f, 0.0f, 1.0f};
    Vector3 radiance{1.0f, 1.0f, 1.0f};
    float range = 8.0f;
    float coneAngle = 0.0f;
    bool spot = false;
};

struct ModelLightTrack {
    flecs::entity_t light = 0;
    float fade = 0.0f;
};

struct ModelLightEntityTrack {
    std::vector<ModelLightTrack> lights;
    double lastTime = 0.0;
};

struct ModelLightingState {
    std::vector<StaticModelLight> lights;
    std::unordered_map<flecs::entity_t, std::size_t> lightIndexById;
    std::unordered_map<flecs::entity_t, ModelLightEntityTrack> tracks;
    double lastPruneTime = 0.0;
};

struct ModelLightingShaderLocs {
    unsigned int shaderId = 0;
    int modelLit = -1;
    int probeSH = -1;
    int lightCount = -1;
    int lightPosRange = -1;
    int lightColor = -1;
    int lightDirCone = -1;
    int lightWrap = -1;
};

float staticLightScoreAtPoint(const StaticModelLight& light, Vector3 point, float radius);

void gatherStaticModelLights(flecs::world& world, ModelLightingState& state);

bool computeModelLighting(
    flecs::world& world,
    ModelLightingState& state,
    flecs::entity entity,
    const Model& model,
    const Matrix& globalMatrix,
    ModelLightingUniforms& out);

bool modelLightingSupported(Shader shader);

void uploadModelLighting(Shader shader, const ModelLightingUniforms* uniforms);

}
