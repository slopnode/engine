#pragma once

#include "assets/asset_store.hpp"
#include "render/dynamic_light.hpp"
#include "render/components.hpp"

#include <flecs.h>
#include <raylib.h>

#include <vector>

namespace slopengine {

/** One shadow-map slot for a ranked shadowed dynamic light. */
struct DynamicLightShadowSlot {
    RenderTexture2D faces[kDynamicShadowFacesPerSlot]{};
    Matrix viewProj[kDynamicShadowFacesPerSlot]{};
    int faceCount = 0;
    DynamicLightKind kind = DynamicLightKind::Point;
    Vector3 lightPosition{};
    float farPlane = 8.0f;
    bool active = false;
};

/** GPU shadow resources for up to kMaxShadowedDynamicLights lights. */
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

/** Cached uniform locations for dynamic lights on a shader. */
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

/** Ranked lights and shader bindings for the current frame. */
struct DynamicLightFrameState {
    std::vector<RankedDynamicLight> lights;
    DynamicLightShaderBindings bindings{};
};

/** Creates shadow render targets and the depth shader. */
DynamicLightShadowState createDynamicLightShadowState(AssetStore& assets);

/** Renders shadow maps for ranked lights that cast shadows. */
void renderDynamicLightShadows(
    DynamicLightShadowState& shadowState,
    const std::vector<RankedDynamicLight>& lights,
    flecs::world& world,
    AssetStore& assets);

/** Resolves dynamic-light uniform locations on @p shader. */
void resolveDynamicLightShaderBindings(Shader shader, DynamicLightShaderBindings& bindings);

/** Uploads ranked lights (and optional shadows) to @p shader. */
void uploadDynamicLightsToShader(
    Shader shader,
    const DynamicLightShaderBindings& bindings,
    const std::vector<RankedDynamicLight>& lights,
    const DynamicLightShadowState* shadowState = nullptr);

}
