#include "render/dynamic_light_shadows.hpp"

#include "render/sprite_billboard.hpp"

#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace slopengine {

namespace {

RenderTexture2D loadShadowmapRenderTexture(int width, int height) {
    RenderTexture2D target{};
    target.id = rlLoadFramebuffer();
    target.texture.width = width;
    target.texture.height = height;

    if (target.id > 0) {
        rlEnableFramebuffer(target.id);
        target.depth.id = rlLoadTextureDepth(width, height, false);
        target.depth.width = width;
        target.depth.height = height;
        target.depth.format = 19;
        target.depth.mipmaps = 1;
        rlFramebufferAttach(
            target.id,
            target.depth.id,
            RL_ATTACHMENT_DEPTH,
            RL_ATTACHMENT_TEXTURE2D,
            0);
        rlFramebufferComplete(target.id);
        rlDisableFramebuffer();
    }
    return target;
}

void unloadShadowmapRenderTexture(RenderTexture2D& target) {
    if (target.id > 0) {
        rlUnloadFramebuffer(target.id);
        target = {};
    }
}

Vector3 cubeFaceTarget(int face) {
    switch (face) {
    case 0:
        return {1.0f, 0.0f, 0.0f};
    case 1:
        return {-1.0f, 0.0f, 0.0f};
    case 2:
        return {0.0f, 1.0f, 0.0f};
    case 3:
        return {0.0f, -1.0f, 0.0f};
    case 4:
        return {0.0f, 0.0f, 1.0f};
    default:
        return {0.0f, 0.0f, -1.0f};
    }
}

Vector3 cubeFaceUp(int face) {
    switch (face) {
    case 2:
        return {0.0f, 0.0f, 1.0f};
    case 3:
        return {0.0f, 0.0f, -1.0f};
    default:
        return {0.0f, -1.0f, 0.0f};
    }
}

Camera3D makeSpotLightCamera(const RankedDynamicLight& light) {
    Camera3D camera{};
    camera.position = light.position;
    const Vector3 forward = Vector3Normalize(light.direction);
    camera.target = Vector3Add(light.position, forward);
    const float align = std::fabs(Vector3DotProduct(forward, {0.0f, 1.0f, 0.0f}));
    camera.up = align > 0.95f ? Vector3{0.0f, 0.0f, 1.0f} : Vector3{0.0f, 1.0f, 0.0f};
    const float coneDeg = light.light.coneAngle * RAD2DEG;
    camera.fovy = std::clamp(coneDeg * 2.0f * 1.15f, 10.0f, 170.0f);
    camera.projection = CAMERA_PERSPECTIVE;
    return camera;
}

Camera3D makePointLightFaceCamera(const RankedDynamicLight& light, int face) {
    Camera3D camera{};
    camera.position = light.position;
    camera.target = Vector3Add(light.position, cubeFaceTarget(face));
    camera.up = cubeFaceUp(face);
    camera.fovy = 90.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    return camera;
}

void drawShadowCasters(
    flecs::world& world,
    AssetStore& assets,
    Shader depthShader,
    int useAlphaClipLoc,
    Vector3 viewPosition,
    float cameraYaw) {
    const int alphaOff = 0;
    const int alphaOn = 1;
    if (useAlphaClipLoc >= 0) {
        SetShaderValue(depthShader, useAlphaClipLoc, &alphaOff, SHADER_UNIFORM_INT);
    }

    world.each([&](flecs::entity entity, Model3D& model, GlobalTransformation& global) {
        if (!entity.has<WorldSpace>()) {
            return;
        }
        std::vector<Shader> previous;
        previous.reserve(static_cast<std::size_t>(std::max(0, model.model.materialCount)));
        for (int i = 0; i < model.model.materialCount; ++i) {
            previous.push_back(model.model.materials[i].shader);
            model.model.materials[i].shader = depthShader;
        }
        rlPushMatrix();
        rlMultMatrixf(MatrixToFloatV(global.matrix).v);
        DrawModel(model.model, Vector3Zero(), 1.0f, WHITE);
        rlPopMatrix();
        for (int i = 0; i < model.model.materialCount; ++i) {
            model.model.materials[i].shader = previous[static_cast<std::size_t>(i)];
        }
    });

    if (useAlphaClipLoc >= 0) {
        SetShaderValue(depthShader, useAlphaClipLoc, &alphaOn, SHADER_UNIFORM_INT);
    }

    world.each([&](flecs::entity entity, SpriteInstance& sprite, GlobalTransformation& global) {
        if (!entity.has<WorldSpace>()) {
            return;
        }
        const auto billboard = resolveSpriteBillboard(sprite, global, viewPosition, cameraYaw, assets);
        if (!billboard || billboard->texture == nullptr) {
            return;
        }

        const Texture2D& texture = *billboard->texture;
        const Rectangle source = billboard->source;
        const float texW = static_cast<float>(texture.width);
        const float texH = static_cast<float>(texture.height);
        const Vector2 texcoords[4] = {
            {source.x / texW, (source.y + source.height) / texH},
            {(source.x + source.width) / texW, (source.y + source.height) / texH},
            {(source.x + source.width) / texW, source.y / texH},
            {source.x / texW, source.y / texH},
        };

        rlSetTexture(texture.id);
        BeginShaderMode(depthShader);
        rlBegin(RL_QUADS);
        for (int i = 0; i < 4; ++i) {
            rlColor4ub(255, 255, 255, 255);
            rlTexCoord2f(texcoords[i].x, texcoords[i].y);
            rlVertex3f(billboard->points[i].x, billboard->points[i].y, billboard->points[i].z);
        }
        rlEnd();
        EndShaderMode();
        rlSetTexture(0);
    });
}

void renderShadowFace(
    DynamicLightShadowState& shadowState,
    DynamicLightShadowSlot& slot,
    int faceIndex,
    const Camera3D& camera,
    float nearPlane,
    float farPlane,
    flecs::world& world,
    AssetStore& assets) {
    RenderTexture2D& target = slot.faces[faceIndex];
    BeginTextureMode(target);
    ClearBackground(WHITE);
    rlSetClipPlanes(nearPlane, farPlane);
    BeginMode3D(camera);
    const Matrix lightView = rlGetMatrixModelview();
    const Matrix lightProj = rlGetMatrixProjection();
    slot.viewProj[faceIndex] = MatrixMultiply(lightView, lightProj);
    drawShadowCasters(
        world,
        assets,
        shadowState.depthShader,
        shadowState.useAlphaClipLoc,
        camera.position,
        horizontalCameraYaw(camera.position, camera.target));
    EndMode3D();
    EndTextureMode();
    rlSetClipPlanes(RL_CULL_DISTANCE_NEAR, RL_CULL_DISTANCE_FAR);
}

} // namespace

DynamicLightShadowState::DynamicLightShadowState(DynamicLightShadowState&& other) noexcept
    : depthShader(other.depthShader)
    , useAlphaClipLoc(other.useAlphaClipLoc)
    , ready(other.ready) {
    for (int i = 0; i < kMaxShadowedDynamicLights; ++i) {
        slots[i] = other.slots[i];
        other.slots[i] = {};
    }
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
    depthShader = other.depthShader;
    useAlphaClipLoc = other.useAlphaClipLoc;
    ready = other.ready;
    other.depthShader = {};
    other.useAlphaClipLoc = -1;
    other.ready = false;
    return *this;
}

DynamicLightShadowState::~DynamicLightShadowState() {
    unload();
}

void DynamicLightShadowState::unload() {
    for (int slotIndex = 0; slotIndex < kMaxShadowedDynamicLights; ++slotIndex) {
        for (int face = 0; face < kDynamicShadowFacesPerSlot; ++face) {
            unloadShadowmapRenderTexture(slots[slotIndex].faces[face]);
        }
        slots[slotIndex] = {};
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

    for (int slotIndex = 0; slotIndex < kMaxShadowedDynamicLights; ++slotIndex) {
        for (int face = 0; face < kDynamicShadowFacesPerSlot; ++face) {
            state.slots[slotIndex].faces[face] =
                loadShadowmapRenderTexture(kDynamicShadowMapResolution, kDynamicShadowMapResolution);
        }
    }
    state.ready = state.depthShader.id != 0;
    return state;
}

void renderDynamicLightShadows(
    DynamicLightShadowState& shadowState,
    const std::vector<RankedDynamicLight>& lights,
    flecs::world& world,
    AssetStore& assets) {
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
        constexpr float kNear = 0.05f;

        if (light.light.kind == DynamicLightKind::Spot) {
            slot.faceCount = 1;
            renderShadowFace(
                shadowState,
                slot,
                0,
                makeSpotLightCamera(light),
                kNear,
                slot.farPlane,
                world,
                assets);
        } else {
            slot.faceCount = kDynamicShadowFacesPerSlot;
            for (int face = 0; face < kDynamicShadowFacesPerSlot; ++face) {
                renderShadowFace(
                    shadowState,
                    slot,
                    face,
                    makePointLightFaceCamera(light, face),
                    kNear,
                    slot.farPlane,
                    world,
                    assets);
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
    bindings.shadowBiasLoc = -1;
    bindings.shadowVpLoc = -1;
    for (int& shadowMapLoc : bindings.shadowMapLoc) {
        shadowMapLoc = -1;
    }
    bindings.resolved = bindings.lightCountLoc >= 0 && bindings.lightPosRangeLoc >= 0 &&
        bindings.lightColorIntensityLoc >= 0;
    TraceLog(
        LOG_INFO,
        "MAP: dynamic light uniforms resolved=%s count=%d pos=%d color=%d dir=%d meta=%d",
        bindings.resolved ? "yes" : "no",
        bindings.lightCountLoc,
        bindings.lightPosRangeLoc,
        bindings.lightColorIntensityLoc,
        bindings.lightDirConeLoc,
        bindings.lightMetaLoc);
}

void uploadDynamicLightsToShader(
    Shader shader,
    const DynamicLightShaderBindings& bindings,
    const std::vector<RankedDynamicLight>& lights,
    const DynamicLightShadowState* shadowState) {
    (void)shadowState;
    if (shader.id == 0 || !bindings.resolved) {
        return;
    }

    const int count = static_cast<int>(std::min<std::size_t>(lights.size(), kMaxDynamicLights));
    SetShaderValue(shader, bindings.lightCountLoc, &count, SHADER_UNIFORM_INT);

    Vector4 posRange[kMaxDynamicLights]{};
    Vector4 colorIntensity[kMaxDynamicLights]{};
    Vector4 dirCone[kMaxDynamicLights]{};
    Vector4 meta[kMaxDynamicLights]{};
    for (int i = 0; i < kMaxDynamicLights; ++i) {
        dirCone[i] = {0.0f, 0.0f, 1.0f, 0.0f};
        meta[i] = {0.0f, -1.0f, 0.0f, 0.0f};
        if (i >= count) {
            continue;
        }
        const RankedDynamicLight& light = lights[static_cast<std::size_t>(i)];
        posRange[i] = {
            light.position.x,
            light.position.y,
            light.position.z,
            light.light.range,
        };
        colorIntensity[i] = {
            light.linearRgb.x,
            light.linearRgb.y,
            light.linearRgb.z,
            light.light.intensity,
        };
        const Vector3 dir = Vector3Normalize(light.direction);
        dirCone[i] = {dir.x, dir.y, dir.z, light.light.coneAngle};
        meta[i] = {
            light.light.kind == DynamicLightKind::Spot ? 1.0f : 0.0f,
            -1.0f,
            0.0f,
            0.0f,
        };
    }

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
}

}
