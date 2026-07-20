#pragma once

#include "assets/asset_store.hpp"
#include "render/dynamic_light.hpp"
#include "render/components.hpp"

#include <flecs.h>
#include <raylib.h>

#include <vector>

namespace slopengine {

struct DynamicLightShadowSlot {
    RenderTexture2D faces[kDynamicShadowFacesPerSlot]{};
    Matrix viewProj[kDynamicShadowFacesPerSlot]{};
    int faceCount = 0;
    DynamicLightKind kind = DynamicLightKind::Point;
    Vector3 lightPosition{};
    float farPlane = 8.0f;
    bool active = false;
};

struct DynamicLightShadowState {
    DynamicLightShadowSlot slots[kMaxShadowedDynamicLights]{};
    Shader depthShader{};
    int useAlphaClipLoc = -1;
    bool ready = false;

    DynamicLightShadowState() = default;
    DynamicLightShadowState(const DynamicLightShadowState&) = delete;
    DynamicLightShadowState& operator=(const DynamicLightShadowState&) = delete;

    DynamicLightShadowState(DynamicLightShadowState&& other) noexcept;
    DynamicLightShadowState& operator=(DynamicLightShadowState&& other) noexcept;
    ~DynamicLightShadowState();

    void unload();
};

struct DynamicLightShaderBindings {
    int lightCountLoc = -1;
    int lightPosRangeLoc = -1;
    int lightColorIntensityLoc = -1;
    int lightDirConeLoc = -1;
    int lightMetaLoc = -1;
    int shadowVpLoc = -1;
    int shadowMapLoc[kMaxShadowedDynamicLights * kDynamicShadowFacesPerSlot]{};
    int shadowBiasLoc = -1;
    bool resolved = false;
};

struct DynamicLightFrameState {
    std::vector<RankedDynamicLight> lights;
    DynamicLightShaderBindings bindings{};
};

DynamicLightShadowState createDynamicLightShadowState(AssetStore& assets);
void renderDynamicLightShadows(
    DynamicLightShadowState& shadowState,
    const std::vector<RankedDynamicLight>& lights,
    flecs::world& world,
    AssetStore& assets);

void resolveDynamicLightShaderBindings(Shader shader, DynamicLightShaderBindings& bindings);
void uploadDynamicLightsToShader(
    Shader shader,
    const DynamicLightShaderBindings& bindings,
    const std::vector<RankedDynamicLight>& lights,
    const DynamicLightShadowState* shadowState = nullptr);

}
