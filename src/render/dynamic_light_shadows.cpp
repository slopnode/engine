#include "render/dynamic_light_shadows.hpp"

#include "render/dynamic_light_shadow_math.hpp"
#include "map/bsp.hpp"
#include "render/render_frustum.hpp"

#include <rlgl.h>
#include "external/glad.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>
#include <vector>

namespace slopengine {

namespace {

void drawShadowCasters(
    flecs::world& world,
    Shader depthShader,
    int useAlphaClipLoc,
    Vector3 lightPosition) {
    const int alphaOff = 0;
    if (useAlphaClipLoc >= 0) {
        SetShaderValue(depthShader, useAlphaClipLoc, &alphaOff, SHADER_UNIFORM_INT);
    }

    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 2.0f);
    world.each([&](flecs::entity entity, Model3D& model, GlobalTransformation& global) {
        if (!entity.has<WorldSpace>()) {
            return;
        }
        const BoundingBox localBounds = GetModelBoundingBox(model.model);
        const BoundingBox worldBounds = transformAabb(localBounds, global.matrix);
        if (shouldSkipShadowCaster(entity.has<MapLightmapState>(), worldBounds, lightPosition)) {
            return;
        }
        const std::unordered_set<int>* skipMeshes = nullptr;
        std::unordered_set<int> meshSkip;
        if (entity.has<MapLightmapState>()) {
            const MapLightmapState& lightmaps = entity.get<MapLightmapState>();
            if (!lightmaps.transparentMeshIndices.empty()) {
                meshSkip.insert(
                    lightmaps.transparentMeshIndices.begin(),
                    lightmaps.transparentMeshIndices.end());
            }
            if (!lightmaps.skyMeshIndices.empty()) {
                meshSkip.insert(
                    lightmaps.skyMeshIndices.begin(),
                    lightmaps.skyMeshIndices.end());
            }
            if (!meshSkip.empty()) {
                skipMeshes = &meshSkip;
            }
        }
        std::vector<Shader> previous;
        previous.reserve(static_cast<std::size_t>(std::max(0, model.model.materialCount)));
        for (int i = 0; i < model.model.materialCount; ++i) {
            previous.push_back(model.model.materials[i].shader);
            model.model.materials[i].shader = depthShader;
        }
        rlPushMatrix();
        rlMultMatrixf(MatrixToFloatV(global.matrix).v);
        if (skipMeshes != nullptr) {
            for (int meshIndex = 0; meshIndex < model.model.meshCount; ++meshIndex) {
                if (skipMeshes->count(meshIndex) > 0) {
                    continue;
                }
                DrawMesh(
                    model.model.meshes[meshIndex],
                    model.model.materials[meshIndex],
                    MatrixIdentity());
            }
        } else {
            DrawModel(model.model, Vector3Zero(), 1.0f, WHITE);
        }
        rlPopMatrix();
        for (int i = 0; i < model.model.materialCount; ++i) {
            model.model.materials[i].shader = previous[static_cast<std::size_t>(i)];
        }
    });
    glDisable(GL_POLYGON_OFFSET_FILL);
}

void renderShadowFace(
    DynamicLightShadowState& shadowState,
    int slotIndex,
    int faceIndex,
    const Camera3D& camera,
    float nearPlane,
    float farPlane,
    flecs::world& world,
    Vector3 lightPosition) {
    const int layer = slotIndex * kDynamicShadowFacesPerSlot + faceIndex;
    DynamicLightShadowSlot& slot = shadowState.slots[slotIndex];

    BeginTextureMode(shadowState.scratch);
    rlEnableFramebuffer(shadowState.fboId);
    glFramebufferTextureLayer(
        GL_FRAMEBUFFER,
        GL_DEPTH_ATTACHMENT,
        shadowState.depthArrayId,
        0,
        layer);
    glClear(GL_DEPTH_BUFFER_BIT);

    rlSetClipPlanes(nearPlane, farPlane);
    BeginMode3D(camera);
    const Matrix lightView = rlGetMatrixModelview();
    const Matrix lightProj = rlGetMatrixProjection();
    slot.viewProj[faceIndex] = MatrixMultiply(lightView, lightProj);
    drawShadowCasters(
        world,
        shadowState.depthShader,
        shadowState.useAlphaClipLoc,
        lightPosition);
    EndMode3D();
    rlSetClipPlanes(RL_CULL_DISTANCE_NEAR, RL_CULL_DISTANCE_FAR);
    EndTextureMode();
}

} // namespace

DynamicLightShadowState::DynamicLightShadowState(DynamicLightShadowState&& other) noexcept
    : depthArrayId(other.depthArrayId)
    , fboId(other.fboId)
    , scratch(other.scratch)
    , depthShader(other.depthShader)
    , useAlphaClipLoc(other.useAlphaClipLoc)
    , ready(other.ready) {
    for (int i = 0; i < kMaxShadowedDynamicLights; ++i) {
        slots[i] = other.slots[i];
        other.slots[i] = {};
    }
    other.depthArrayId = 0;
    other.fboId = 0;
    other.scratch = {};
    other.depthShader = {};
    other.useAlphaClipLoc = -1;
    other.ready = false;
}

DynamicLightShadowState& DynamicLightShadowState::operator=(DynamicLightShadowState&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    unload();
    for (int i = 0; i < kMaxShadowedDynamicLights; ++i) {
        slots[i] = other.slots[i];
        other.slots[i] = {};
    }
    depthArrayId = other.depthArrayId;
    fboId = other.fboId;
    scratch = other.scratch;
    depthShader = other.depthShader;
    useAlphaClipLoc = other.useAlphaClipLoc;
    ready = other.ready;
    other.depthArrayId = 0;
    other.fboId = 0;
    other.scratch = {};
    other.depthShader = {};
    other.useAlphaClipLoc = -1;
    other.ready = false;
    return *this;
}

DynamicLightShadowState::~DynamicLightShadowState() {
    unload();
}

void DynamicLightShadowState::unload() {
    for (int i = 0; i < kMaxShadowedDynamicLights; ++i) {
        slots[i] = {};
    }
    if (depthArrayId != 0) {
        glDeleteTextures(1, &depthArrayId);
        depthArrayId = 0;
    }
    if (fboId != 0) {
        rlUnloadFramebuffer(fboId);
        fboId = 0;
    }
    if (scratch.id != 0) {
        UnloadRenderTexture(scratch);
        scratch = {};
    }
    if (depthShader.id != 0) {
        UnloadShader(depthShader);
        depthShader = {};
    }
    useAlphaClipLoc = -1;
    ready = false;
}

DynamicLightShadowState createDynamicLightShadowState(AssetStore& assets) {
    DynamicLightShadowState state{};
    const std::string vert = assets.getShaderSource("default/shadow_depth_vert");
    const std::string frag = assets.getShaderSource("default/shadow_depth_frag");
    if (!vert.empty() && !frag.empty()) {
        state.depthShader = LoadShaderFromMemory(vert.c_str(), frag.c_str());
        if (state.depthShader.id != 0) {
            state.depthShader.locs[SHADER_LOC_MAP_ALBEDO] =
                GetShaderLocation(state.depthShader, "texture0");
            state.useAlphaClipLoc = GetShaderLocation(state.depthShader, "useAlphaClip");
        }
    }

    glGenTextures(1, &state.depthArrayId);
    glBindTexture(GL_TEXTURE_2D_ARRAY, state.depthArrayId);
    glTexImage3D(
        GL_TEXTURE_2D_ARRAY,
        0,
        GL_DEPTH_COMPONENT24,
        kDynamicShadowMapResolution,
        kDynamicShadowMapResolution,
        kDynamicShadowMapCount,
        0,
        GL_DEPTH_COMPONENT,
        GL_FLOAT,
        nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    state.fboId = rlLoadFramebuffer();
    state.scratch = LoadRenderTexture(kDynamicShadowMapResolution, kDynamicShadowMapResolution);
    state.ready = state.depthShader.id != 0 && state.depthArrayId != 0 && state.fboId != 0 &&
        state.scratch.id != 0;
    if (!state.ready) {
        state.unload();
    }
    return state;
}

void renderDynamicLightShadows(
    DynamicLightShadowState& shadowState,
    const std::vector<RankedDynamicLight>& lights,
    flecs::world& world,
    AssetStore& assets) {
    (void)assets;
    if (!shadowState.ready) {
        return;
    }

    for (int i = 0; i < kMaxShadowedDynamicLights; ++i) {
        shadowState.slots[i].active = false;
        shadowState.slots[i].faceCount = 0;
    }

    for (const RankedDynamicLight& light : lights) {
        if (light.shadowSlot < 0 || light.shadowSlot >= kMaxShadowedDynamicLights) {
            continue;
        }
        DynamicLightShadowSlot& slot = shadowState.slots[light.shadowSlot];
        slot.active = true;
        slot.kind = light.light.kind;
        slot.lightPosition = light.position;
        slot.farPlane = std::max(light.light.range, 0.5f);
        const float nearPlane = shadowNearPlane(light.light.range);

        if (light.light.kind == DynamicLightKind::Spot) {
            slot.faceCount = 1;
            const ShadowCameraDesc desc =
                spotShadowCamera(light.position, light.direction, light.light.coneAngle);
            renderShadowFace(
                shadowState,
                light.shadowSlot,
                0,
                shadowCameraFromDesc(desc),
                nearPlane,
                slot.farPlane,
                world,
                light.position);
        } else {
            slot.faceCount = kDynamicShadowFacesPerSlot;
            for (int face = 0; face < kDynamicShadowFacesPerSlot; ++face) {
                const ShadowCameraDesc desc = pointShadowFaceCamera(light.position, face);
                renderShadowFace(
                    shadowState,
                    light.shadowSlot,
                    face,
                    shadowCameraFromDesc(desc),
                    nearPlane,
                    slot.farPlane,
                    world,
                    light.position);
            }
        }
    }
}

void resolveDynamicLightShaderBindings(Shader shader, DynamicLightShaderBindings& bindings) {
    bindings = {};
    if (shader.id == 0) {
        return;
    }

    auto locateArray = [&](const char* baseName) {
        int loc = GetShaderLocation(shader, baseName);
        if (loc < 0) {
            loc = GetShaderLocation(shader, TextFormat("%s[0]", baseName));
        }
        return loc;
    };

    bindings.lightCountLoc = GetShaderLocation(shader, "dynLightCount");
    bindings.lightPosRangeLoc = locateArray("dynLightPosRange");
    bindings.lightColorIntensityLoc = locateArray("dynLightColorIntensity");
    bindings.lightDirConeLoc = locateArray("dynLightDirCone");
    bindings.lightMetaLoc = locateArray("dynLightMeta");
    bindings.shadowBiasLoc = GetShaderLocation(shader, "dynShadowBias");
    bindings.shadowMapsLoc = GetShaderLocation(shader, "dynShadowMaps");
    for (int i = 0; i < kDynamicShadowMapCount; ++i) {
        bindings.shadowVpLoc[i] =
            GetShaderLocation(shader, TextFormat("dynShadowVp[%d]", i));
    }
    bindings.resolved = bindings.lightCountLoc >= 0 && bindings.lightPosRangeLoc >= 0 &&
        bindings.lightColorIntensityLoc >= 0;
    TraceLog(
        LOG_INFO,
        "MAP: dynamic light uniforms resolved=%s count=%d pos=%d color=%d dir=%d meta=%d bias=%d maps=%d",
        bindings.resolved ? "yes" : "no",
        bindings.lightCountLoc,
        bindings.lightPosRangeLoc,
        bindings.lightColorIntensityLoc,
        bindings.lightDirConeLoc,
        bindings.lightMetaLoc,
        bindings.shadowBiasLoc,
        bindings.shadowMapsLoc);
}

void uploadDynamicLightsToShader(
    Shader shader,
    const DynamicLightShaderBindings& bindings,
    const std::vector<RankedDynamicLight>& lights,
    const DynamicLightShadowState* shadowState) {
    if (shader.id == 0 || !bindings.resolved) {
        return;
    }

    Vector4 posRange[kMaxDynamicLights]{};
    Vector4 colorIntensity[kMaxDynamicLights]{};
    Vector4 dirCone[kMaxDynamicLights]{};
    Vector4 meta[kMaxDynamicLights]{};
    for (int i = 0; i < kMaxDynamicLights; ++i) {
        dirCone[i] = {0.0f, 0.0f, 1.0f, 0.0f};
        meta[i] = {0.0f, -1.0f, 0.0f, 0.0f};
    }

    int count = 0;
    const bool shadowsActive = shadowState != nullptr && shadowState->ready;
    for (const RankedDynamicLight& light : lights) {
        if (count >= kMaxDynamicLights) {
            break;
        }
        if (shadowsActive && light.light.castShadows && light.shadowSlot < 0) {
            continue;
        }
        posRange[count] = {
            light.position.x,
            light.position.y,
            light.position.z,
            light.light.range,
        };
        colorIntensity[count] = {
            light.linearRgb.x,
            light.linearRgb.y,
            light.linearRgb.z,
            light.light.intensity,
        };
        const Vector3 dir = Vector3Normalize(light.direction);
        dirCone[count] = {dir.x, dir.y, dir.z, light.light.coneAngle};
        const float shadowSlot =
            (shadowsActive && light.shadowSlot >= 0) ? static_cast<float>(light.shadowSlot) : -1.0f;
        meta[count] = {
            light.light.kind == DynamicLightKind::Spot ? 1.0f : 0.0f,
            shadowSlot,
            0.0f,
            0.0f,
        };
        ++count;
    }
    SetShaderValue(shader, bindings.lightCountLoc, &count, SHADER_UNIFORM_INT);

    SetShaderValueV(
        shader,
        bindings.lightPosRangeLoc,
        posRange,
        SHADER_UNIFORM_VEC4,
        kMaxDynamicLights);
    SetShaderValueV(
        shader,
        bindings.lightColorIntensityLoc,
        colorIntensity,
        SHADER_UNIFORM_VEC4,
        kMaxDynamicLights);
    if (bindings.lightDirConeLoc >= 0) {
        SetShaderValueV(
            shader,
            bindings.lightDirConeLoc,
            dirCone,
            SHADER_UNIFORM_VEC4,
            kMaxDynamicLights);
    }
    if (bindings.lightMetaLoc >= 0) {
        SetShaderValueV(shader, bindings.lightMetaLoc, meta, SHADER_UNIFORM_VEC4, kMaxDynamicLights);
    }

    if (bindings.shadowBiasLoc >= 0) {
        const float bias = kDynamicShadowBias;
        SetShaderValue(shader, bindings.shadowBiasLoc, &bias, SHADER_UNIFORM_FLOAT);
    }

    if (shadowState == nullptr || !shadowState->ready) {
        return;
    }

    for (int slotIndex = 0; slotIndex < kMaxShadowedDynamicLights; ++slotIndex) {
        const DynamicLightShadowSlot& slot = shadowState->slots[slotIndex];
        const int faceCount = slot.active ? slot.faceCount : 0;
        for (int face = 0; face < kDynamicShadowFacesPerSlot; ++face) {
            const int mapIndex = slotIndex * kDynamicShadowFacesPerSlot + face;
            if (bindings.shadowVpLoc[mapIndex] >= 0) {
                const Matrix vp =
                    (face < faceCount) ? slot.viewProj[face] : MatrixIdentity();
                SetShaderValueMatrix(shader, bindings.shadowVpLoc[mapIndex], vp);
            }
        }
    }
}

void bindDynamicLightShadowMaps(
    Shader shader,
    const DynamicLightShaderBindings& bindings,
    const DynamicLightShadowState& shadowState) {
    if (shader.id == 0 || !shadowState.ready || bindings.shadowMapsLoc < 0 ||
        shadowState.depthArrayId == 0) {
        return;
    }

    rlDrawRenderBatchActive();
    rlEnableShader(shader.id);
    const int unit = kDynamicShadowTextureUnit;
    rlActiveTextureSlot(unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, shadowState.depthArrayId);
    rlSetUniform(bindings.shadowMapsLoc, &unit, SHADER_UNIFORM_INT, 1);
    rlActiveTextureSlot(0);
    rlDisableShader();
}

}
