#pragma once

#include <flecs.h>
#include <raylib.h>

#include <unordered_map>
#include <vector>

namespace slopengine {

constexpr int kMaxModelStaticLights = 4;
constexpr float kModelStaticLightWrap = 0.35f;

struct ModelLightingUniforms {
    Vector3 probeCube[6]{};
    int lightCount = 0;
    Vector4 lightPosRange[kMaxModelStaticLights]{};
    Vector4 lightColor[kMaxModelStaticLights]{};
    Vector4 lightDirCone[kMaxModelStaticLights]{};
    bool viewSpace = false;
    Matrix worldFromView{};
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
    std::vector<std::pair<flecs::entity_t, float>> visibility;
    Vector3 visibilityCenter{};
    double visibilityTime = 0.0;
    bool visibilityValid = false;
    ModelLightingUniforms uniforms{};
    Vector3 uniformsCenter{};
    double uniformsTime = 0.0;
    bool uniformsValid = false;
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
    int probeCube = -1;
    int lightCount = -1;
    int lightPosRange = -1;
    int lightColor = -1;
    int lightDirCone = -1;
    int lightWrap = -1;
    int viewSpace = -1;
    int worldFromView = -1;
};

float staticLightScoreAtPoint(const StaticModelLight& light, Vector3 point, float radius);

void gatherStaticModelLights(flecs::world& world, ModelLightingState& state);

bool computeModelLighting(
    flecs::world& world,
    ModelLightingState& state,
    flecs::entity entity,
    const Model& model,
    const Matrix& globalMatrix,
    ModelLightingUniforms& out,
    const Vector3* samplePoint = nullptr);

Matrix firstPersonWorldFromView(const Camera3D& camera);

bool modelLightingSupported(Shader shader);

void uploadModelLighting(Shader shader, const ModelLightingUniforms* uniforms);

}
