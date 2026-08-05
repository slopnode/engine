#include "render/render_pass_world.hpp"

#include "assets/asset_services.hpp"
#include "assets/asset_store.hpp"
#include "map/bsp.hpp"
#include "map/graph.hpp"
#include "map/light_components.hpp"
#include "map/light_sample.hpp"
#include "map/fac.hpp"
#include "map/pvs.hpp"
#include "particles/particle_sim.hpp"
#include "render/animation_player.hpp"
#include "render/components.hpp"
#include "render/dynamic_light.hpp"
#include "render/dynamic_light_shadows.hpp"
#include "physics/rigid_mover.hpp"
#include "render/fx_local_light.hpp"
#include "render/render_debug.hpp"
#include "render/sprite_animator.hpp"
#include "render/sprite_billboard.hpp"
#include "script/first_person_script.hpp"
#include "ui/ui_state.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include "external/glad.h"

namespace slopengine {

namespace {

constexpr float kDetailMeshPolygonOffsetFactor = 1.0f;
constexpr float kDetailMeshPolygonOffsetUnits = 2.0f;
constexpr float kTransparentMeshPolygonOffsetFactor = -1.0f;
constexpr float kTransparentMeshPolygonOffsetUnits = -2.0f;

const MapLightmapState* mapLightmapState(flecs::world& world) {
    flecs::entity mapEntity = world.lookup("MapStatic");
    if (!mapEntity.is_valid() || !mapEntity.has<MapLightmapState>()) {
        return nullptr;
    }
    const MapLightmapState& lightmaps = mapEntity.get<MapLightmapState>();
    if (!lightmaps.available || lightmaps.lightmapShader.id == 0) {
        return nullptr;
    }
    return &lightmaps;
}

void uploadLightmapDynamicLights(flecs::world& world, Shader shader) {
    if (shader.id == 0 || !world.has<DynamicLightFrameState>()) {
        return;
    }
    const DynamicLightFrameState& frame = world.get<DynamicLightFrameState>();
    if (!frame.bindings.resolved) {
        return;
    }
    const DynamicLightShadowState* shadows = nullptr;
    if (frame.shadowMapsActive && world.has<DynamicLightShadowState>()) {
        const DynamicLightShadowState& shadowState = world.get<DynamicLightShadowState>();
        if (shadowState.ready) {
            shadows = &shadowState;
        }
    }
    uploadDynamicLightsToShader(shader, frame.bindings, frame.lights, shadows);
}

void prepareLightmapShaderDraw(
    Shader shader,
    int useLightmapLoc,
    int useLightmap,
    const Matrix& modelMatrix,
    flecs::world& world) {
    if (shader.locs[SHADER_LOC_MATRIX_MODEL] < 0) {
        shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shader, "matModel");
    }
    if (shader.locs[SHADER_LOC_MATRIX_MODEL] >= 0) {
        SetShaderValueMatrix(shader, shader.locs[SHADER_LOC_MATRIX_MODEL], modelMatrix);
    }
    if (useLightmapLoc >= 0) {
        SetShaderValue(shader, useLightmapLoc, &useLightmap, SHADER_UNIFORM_INT);
    }
    if (world.has<DynamicLightFrameState>()) {
        const DynamicLightFrameState& frame = world.get<DynamicLightFrameState>();
        if (frame.bindings.resolved) {
            const DynamicLightShadowState* shadowState = nullptr;
            if (frame.shadowMapsActive && world.has<DynamicLightShadowState>()) {
                const DynamicLightShadowState& state = world.get<DynamicLightShadowState>();
                if (state.ready) {
                    shadowState = &state;
                }
            }
            bindLightmapShadowMapsForDraw(
                shader, frame.bindings, frame.shadowMapsActive, shadowState);
            uploadLightmapDynamicLights(world, shader);
        }
    }
}

void drawModelMeshes(
    const Model& model,
    const std::unordered_set<int>* skipMeshIndices,
    const std::unordered_set<int>* onlyMeshIndices,
    const std::unordered_set<int>* polygonOffsetBackMeshIndices) {
    for (int meshIndex = 0; meshIndex < model.meshCount; ++meshIndex) {
        if (skipMeshIndices != nullptr && skipMeshIndices->count(meshIndex) > 0) {
            continue;
        }
        if (onlyMeshIndices != nullptr && onlyMeshIndices->count(meshIndex) == 0) {
            continue;
        }
        const bool offsetBack =
            polygonOffsetBackMeshIndices != nullptr &&
            polygonOffsetBackMeshIndices->count(meshIndex) > 0;
        if (offsetBack) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(kDetailMeshPolygonOffsetFactor, kDetailMeshPolygonOffsetUnits);
        }
        DrawMesh(model.meshes[meshIndex], model.materials[meshIndex], MatrixIdentity());
        if (offsetBack) {
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
    }
}

Vector3 cameraForwardDir(const Camera3D& camera) {
    const Vector3 forward = Vector3Subtract(camera.target, camera.position);
    const float lenSq = Vector3LengthSqr(forward);
    if (lenSq < 1e-8f) {
        return {0.0f, 0.0f, 1.0f};
    }
    return Vector3Scale(forward, 1.0f / std::sqrt(lenSq));
}

float viewDepthAlongAxis(Vector3 point, Vector3 cameraPos, Vector3 cameraForward) {
    return Vector3DotProduct(Vector3Subtract(point, cameraPos), cameraForward);
}

float meshMinViewDepth(
    const Mesh& mesh,
    const Matrix& worldMatrix,
    Vector3 cameraPos,
    Vector3 cameraForward) {
    const BoundingBox bounds = GetMeshBoundingBox(mesh);
    const Vector3 localCorners[8] = {
        {bounds.min.x, bounds.min.y, bounds.min.z},
        {bounds.max.x, bounds.min.y, bounds.min.z},
        {bounds.min.x, bounds.max.y, bounds.min.z},
        {bounds.max.x, bounds.max.y, bounds.min.z},
        {bounds.min.x, bounds.min.y, bounds.max.z},
        {bounds.max.x, bounds.min.y, bounds.max.z},
        {bounds.min.x, bounds.max.y, bounds.max.z},
        {bounds.max.x, bounds.max.y, bounds.max.z},
    };
    float minDepth = std::numeric_limits<float>::max();
    for (const Vector3& local : localCorners) {
        const Vector3 world = Vector3Transform(local, worldMatrix);
        minDepth = std::min(minDepth, viewDepthAlongAxis(world, cameraPos, cameraForward));
    }
    return minDepth;
}

struct SpriteDrawItem {
    const SpriteInstance* sprite = nullptr;
    const GlobalTransformation* global = nullptr;
    const SpriteAnimator* animator = nullptr;
    float viewDepth = 0.0f;
    int layer = 0;
};

enum class TransparentDrawKind {
    MapMesh,
    Sprite,
    Particle,
};

struct TransparentDrawItem {
    float viewDepth = 0.0f;
    int sortLayer = 0;
    TransparentDrawKind kind = TransparentDrawKind::MapMesh;
    int mapMeshIndex = -1;
    SpriteDrawItem sprite{};
    ParticleDrawItem particle{};
};

} // namespace

void renderWorldModel(
    flecs::entity entity,
    Model3D& model,
    GlobalTransformation& globalTransform,
    const Lens& lens,
    bool unlit,
    const std::unordered_set<int>* skipMeshIndices,
    const std::unordered_set<int>* onlyMeshIndices) {
    rlPushMatrix();
    rlMultMatrixf(MatrixToFloatV(globalTransform.matrix).v);

    flecs::world world = entity.world();
    std::vector<Shader> previousShaders;
    bool swappedPropShader = false;
    const MapLightmapState* mapLightmaps = nullptr;
    const int mapUseLightmap = unlit ? 0 : 1;

    if (entity.has<MapLightmapState>()) {
        const MapLightmapState& lightmaps = entity.get<MapLightmapState>();
        if (lightmaps.available && lightmaps.lightmapShader.id != 0) {
            prepareLightmapShaderDraw(
                lightmaps.lightmapShader,
                lightmaps.useLightmapLoc,
                mapUseLightmap,
                globalTransform.matrix,
                world);
        }
    } else if (model.model.materialCount > 0) {
        mapLightmaps = mapLightmapState(world);
        if (mapLightmaps != nullptr) {
            previousShaders.reserve(static_cast<std::size_t>(model.model.materialCount));
            for (int i = 0; i < model.model.materialCount; ++i) {
                previousShaders.push_back(model.model.materials[i].shader);
                model.model.materials[i].shader = mapLightmaps->lightmapShader;
            }
            swappedPropShader = true;
            prepareLightmapShaderDraw(
                mapLightmaps->lightmapShader,
                mapLightmaps->useLightmapLoc,
                0,
                globalTransform.matrix,
                world);
        }
    }

    if (entity.has<ShaderCavity>()) {
        ShaderCavity& shader = entity.get_mut<ShaderCavity>();
        if (!shader.resolved) {
            shader.modelLoc = GetShaderLocation(shader.shader, "model");
            shader.viewLoc = GetShaderLocation(shader.shader, "view");
            shader.projectionLoc = GetShaderLocation(shader.shader, "projection");
            shader.resolved = true;
        }
        SetShaderValueMatrix(shader.shader, shader.modelLoc, globalTransform.matrix);
        SetShaderValueMatrix(shader.shader, shader.viewLoc, GetCameraMatrix(lens.camera));
        SetShaderValueMatrix(
            shader.shader,
            shader.projectionLoc,
            MatrixPerspective(
                lens.camera.fovy,
                static_cast<float>(GetScreenWidth()) / static_cast<float>(GetScreenHeight()),
                0.1f,
                100.0f));
    }

    if (skipMeshIndices != nullptr || onlyMeshIndices != nullptr) {
        const std::unordered_set<int>* polygonOffsetBack = nullptr;
        std::unordered_set<int> detailOffset;
        if (entity.has<MapLightmapState>()) {
            const MapLightmapState& lightmaps = entity.get<MapLightmapState>();
            if (!lightmaps.detailMeshIndices.empty()) {
                detailOffset.insert(
                    lightmaps.detailMeshIndices.begin(),
                    lightmaps.detailMeshIndices.end());
                polygonOffsetBack = &detailOffset;
            }
        }
        drawModelMeshes(model.model, skipMeshIndices, onlyMeshIndices, polygonOffsetBack);
    } else {
        const std::unordered_set<int>* polygonOffsetBack = nullptr;
        std::unordered_set<int> detailOffset;
        if (entity.has<MapLightmapState>()) {
            const MapLightmapState& lightmaps = entity.get<MapLightmapState>();
            if (!lightmaps.detailMeshIndices.empty()) {
                detailOffset.insert(
                    lightmaps.detailMeshIndices.begin(),
                    lightmaps.detailMeshIndices.end());
                polygonOffsetBack = &detailOffset;
            }
        }
        if (polygonOffsetBack != nullptr) {
            drawModelMeshes(model.model, nullptr, nullptr, polygonOffsetBack);
        } else {
            DrawModel(model.model, Vector3Zero(), 1.0f, model.color);
        }
    }

    if (swappedPropShader) {
        for (int i = 0; i < model.model.materialCount; ++i) {
            model.model.materials[i].shader = previousShaders[static_cast<std::size_t>(i)];
        }
        if (mapLightmaps != nullptr && mapLightmaps->useLightmapLoc >= 0) {
            SetShaderValue(
                mapLightmaps->lightmapShader,
                mapLightmaps->useLightmapLoc,
                &mapUseLightmap,
                SHADER_UNIFORM_INT);
        }
    }

    rlPopMatrix();
}

namespace {

Vector3 translationFromMatrix(const Matrix& matrix) {
    return {matrix.m12, matrix.m13, matrix.m14};
}

Vector3 directionFromMatrix(const Matrix& matrix) {
    const Vector3 dir{matrix.m8, matrix.m9, matrix.m10};
    if (Vector3LengthSqr(dir) < 1e-8f) {
        return {0.0f, 0.0f, 1.0f};
    }
    return Vector3Normalize(dir);
}

bool pvsVisibleFromCamera(
    flecs::world& world,
    Vector3 cameraPos,
    Vector3 objectPos) {
    if (!world.has<MapPvs>() || !world.has<MapBsp>()) {
        return true;
    }
    return pvsVisiblePoints(
        world.get<MapBsp>().tree,
        world.get<MapPvs>().pvs,
        cameraPos,
        objectPos);
}

struct SpriteBillboardShader {
    Shader shader{};
    int albedoRectLoc = -1;
    int atlasSizeLoc = -1;
    int useBrightmapLoc = -1;
    int brightMapLoc = -1;
    bool ready = false;
};

SpriteBillboardShader& spriteBillboardShader(AssetStore& assets) {
    static SpriteBillboardShader state{};
    if (state.ready || state.shader.id != 0) {
        return state;
    }
    const std::string vert = assets.getShaderSource("default/sprite_billboard_vert");
    const std::string frag = assets.getShaderSource("default/sprite_billboard_frag");
    if (vert.empty() || frag.empty()) {
        return state;
    }
    state.shader = LoadShaderFromMemory(vert.c_str(), frag.c_str());
    if (state.shader.id == 0) {
        return state;
    }
    state.shader.locs[SHADER_LOC_MAP_ALBEDO] = GetShaderLocation(state.shader, "texture0");
    state.brightMapLoc = GetShaderLocation(state.shader, "texture1");
    state.albedoRectLoc = GetShaderLocation(state.shader, "albedoRect");
    state.atlasSizeLoc = GetShaderLocation(state.shader, "atlasSize");
    state.useBrightmapLoc = GetShaderLocation(state.shader, "useBrightmap");
    state.ready = true;
    return state;
}

void drawWorldSprite(
    const SpriteInstance& sprite,
    const GlobalTransformation& global,
    const Lens& lens,
    AssetStore& assets,
    const MapLighting* lighting,
    const std::vector<RankedDynamicLight>* dynamicLights,
    const FxLightFrameState* fxLights,
    bool unlit,
    const SpriteAnimator* animator) {
    SpriteAnimTween tween{};
    const SpriteAnimTween* tweenPtr = nullptr;
    if (animator != nullptr && animator->hasTween() && !animator->nextFrame.empty()) {
        tween.nextFrame = animator->nextFrame;
        tween.blend = animator->transformBlend;
        tween.tweenRotation = animator->tweenRotation;
        tween.tweenScale = animator->tweenScale;
        tween.tweenTranslate = animator->tweenTranslate;
        tweenPtr = &tween;
    }
    const auto billboard = resolveSpriteBillboard(sprite, global, lens, assets, tweenPtr);
    if (!billboard || billboard->texture == nullptr) {
        return;
    }

    const bool useBrightmap = billboard->brightTexture != nullptr && billboard->brightTexture->id != 0;
    const bool forceFullbright = billboard->fullbright && !useBrightmap;

    Color colorFeet = WHITE;
    Color colorHead = WHITE;
    if (!unlit && !forceFullbright && lighting != nullptr) {
        colorFeet = lighting->ambient;
        colorHead = lighting->ambient;
        if (lighting->available) {
            const Vector3 feetOrigin{
                billboard->position.x,
                billboard->position.y + 0.05f,
                billboard->position.z};
            if (auto feet =
                    sampleMapLight(*lighting, feetOrigin, {0.0f, -1.0f, 0.0f}, 2.0f)) {
                colorFeet = *feet;
            }

            const Vector3 headPos{
                billboard->position.x,
                billboard->position.y + billboard->size.y,
                billboard->position.z};
            Vector3 headDir{
                lens.camera.position.x - headPos.x,
                0.0f,
                lens.camera.position.z - headPos.z,
            };
            const float headLenSq = Vector3LengthSqr(headDir);
            if (headLenSq > 1e-8f) {
                headDir = Vector3Scale(headDir, 1.0f / std::sqrt(headLenSq));
                if (auto head = sampleMapLight(*lighting, headPos, headDir, 4.0f)) {
                    colorHead = *head;
                } else {
                    colorHead = colorFeet;
                }
            } else {
                colorHead = colorFeet;
            }
        }
    }

    if (!unlit && !forceFullbright) {
        const Vector3 feetPoint{
            billboard->position.x,
            billboard->position.y + 0.05f,
            billboard->position.z};
        const Vector3 headPoint{
            billboard->position.x,
            billboard->position.y + billboard->size.y,
            billboard->position.z};
        const Vector3 normal{0.0f, 1.0f, 0.0f};
        const QuadBvh* occlusionBvh =
            (lighting != nullptr && lighting->available && !lighting->surfaceBvh.empty())
                ? &lighting->surfaceBvh
                : nullptr;
        const std::vector<char>* occlusionSkip =
            (lighting != nullptr && lighting->available && !lighting->faceTransparentSkip.empty())
                ? &lighting->faceTransparentSkip
                : nullptr;
        colorFeet = composeBakeTintWithOverlay(
            colorFeet,
            evaluateOverlayLightsAtPoint(
                dynamicLights, fxLights, feetPoint, normal, occlusionBvh, occlusionSkip));
        colorHead = composeBakeTintWithOverlay(
            colorHead,
            evaluateOverlayLightsAtPoint(
                dynamicLights, fxLights, headPoint, normal, occlusionBvh, occlusionSkip));
    }
    if (billboard->fullbright && useBrightmap) {
        colorFeet = WHITE;
        colorHead = WHITE;
    }

    auto multiplyTint = [](Color lit, Color tint) {
        return Color{
            static_cast<unsigned char>(
                std::clamp(static_cast<int>(lit.r) * static_cast<int>(tint.r) / 255, 0, 255)),
            static_cast<unsigned char>(
                std::clamp(static_cast<int>(lit.g) * static_cast<int>(tint.g) / 255, 0, 255)),
            static_cast<unsigned char>(
                std::clamp(static_cast<int>(lit.b) * static_cast<int>(tint.b) / 255, 0, 255)),
            static_cast<unsigned char>(
                std::clamp(static_cast<int>(lit.a) * static_cast<int>(tint.a) / 255, 0, 255)),
        };
    };
    colorFeet = multiplyTint(colorFeet, billboard->tint);
    colorHead = multiplyTint(colorHead, billboard->tint);

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
    const Color colors[4] = {colorFeet, colorFeet, colorHead, colorHead};

    SpriteBillboardShader* billboardShader = &spriteBillboardShader(assets);
    if (!billboardShader->ready) {
        billboardShader = nullptr;
    }

    if (billboardShader != nullptr) {
        const Vector4 albedoRect{
            source.x,
            source.y,
            source.width,
            source.height,
        };
        const Vector2 atlasSize{texW, texH};
        const int useBright = useBrightmap ? 1 : 0;
        BeginShaderMode(billboardShader->shader);
        if (billboardShader->albedoRectLoc >= 0) {
            SetShaderValue(
                billboardShader->shader,
                billboardShader->albedoRectLoc,
                &albedoRect,
                SHADER_UNIFORM_VEC4);
        }
        if (billboardShader->atlasSizeLoc >= 0) {
            SetShaderValue(
                billboardShader->shader,
                billboardShader->atlasSizeLoc,
                &atlasSize,
                SHADER_UNIFORM_VEC2);
        }
        if (billboardShader->useBrightmapLoc >= 0) {
            SetShaderValue(
                billboardShader->shader,
                billboardShader->useBrightmapLoc,
                &useBright,
                SHADER_UNIFORM_INT);
        }
        if (useBrightmap && billboardShader->brightMapLoc >= 0) {
            SetShaderValueTexture(
                billboardShader->shader,
                billboardShader->brightMapLoc,
                *billboard->brightTexture);
        }
    }

    rlSetTexture(texture.id);
    rlBegin(RL_QUADS);
    for (int i = 0; i < 4; ++i) {
        rlColor4ub(colors[i].r, colors[i].g, colors[i].b, colors[i].a);
        rlTexCoord2f(texcoords[i].x, texcoords[i].y);
        rlVertex3f(billboard->points[i].x, billboard->points[i].y, billboard->points[i].z);
    }
    rlEnd();
    rlSetTexture(0);
    if (billboardShader != nullptr) {
        EndShaderMode();
    }
}

} // namespace

std::vector<RankedDynamicLight> gatherDynamicLights(
    flecs::world& world,
    const Lens& lens,
    const Lens& presentLens,
    const Frustum& frustum,
    bool unlit,
    bool enableDynamicLights,
    int maxShadowed) {
    std::vector<RankedDynamicLight> rankedLights;
    if (unlit || !enableDynamicLights) {
        return rankedLights;
    }
    std::vector<RankedDynamicLight> candidates;
    world.each([&](flecs::entity entity,
                   const DynamicLight& light,
                   const GlobalTransformation& global) {
        if (light.intensity <= 0.0f) {
            return;
        }
        RankedDynamicLight ranked{};
        ranked.light = light;
        const Vector3 localPos = translationFromMatrix(global.matrix);
        const Vector3 localDir = directionFromMatrix(global.matrix);
        const bool viewSpace = entity.has<ViewSpace>();
        if (viewSpace) {
            ranked.position = viewToWorldPoint(presentLens, localPos);
            ranked.direction = viewToWorldDirection(presentLens, localDir);
        } else {
            ranked.position = localPos;
            ranked.direction = localDir;
        }
        if (!sphereInFrustum(frustum, ranked.position, std::max(light.range, 0.0f))) {
            return;
        }
        if (!viewSpace &&
            !pvsVisibleFromCamera(world, lens.camera.position, ranked.position)) {
            return;
        }
        ranked.linearRgb = dynamicLightLinearRgb(light);
        candidates.push_back(ranked);
    });
    rankedLights = rankDynamicLights(
        candidates,
        lens.camera.position,
        kMaxDynamicLights,
        maxShadowed);
    return rankedLights;
}

void storeDynamicLightFrameState(
    flecs::world& world,
    const std::vector<RankedDynamicLight>& rankedLights) {
    if (world.has<DynamicLightFrameState>()) {
        world.get_mut<DynamicLightFrameState>().lights = rankedLights;
    }
}

void uploadMapDynamicLights(
    flecs::world& world,
    const std::vector<RankedDynamicLight>& rankedLights,
    bool unlit,
    const DynamicLightShadowState* shadowState) {
    if (unlit) {
        return;
    }
    flecs::entity mapEntity = world.lookup("MapStatic");
    if (!mapEntity.is_valid() || !mapEntity.has<MapLightmapState>()) {
        return;
    }
    MapLightmapState& lightmaps = mapEntity.get_mut<MapLightmapState>();
    if (!lightmaps.available || lightmaps.lightmapShader.id == 0) {
        return;
    }
    Shader& mapShader = lightmaps.lightmapShader;
    if (world.has<DynamicLightFrameState>()) {
        DynamicLightFrameState& frameState = world.get_mut<DynamicLightFrameState>();
        if (!frameState.bindings.resolved || frameState.bindings.shadowMapsLoc < 0) {
            resolveDynamicLightShaderBindings(mapShader, frameState.bindings);
        }
        if (frameState.bindings.resolved) {
            const DynamicLightShadowState* activeShadows =
                frameState.shadowMapsActive ? shadowState : nullptr;
            bindLightmapShadowMapsForDraw(
                mapShader,
                frameState.bindings,
                frameState.shadowMapsActive,
                activeShadows);
            uploadDynamicLightsToShader(
                mapShader, frameState.bindings, rankedLights, activeShadows);
        }
    }
    if (lightmaps.useLightmapLoc >= 0) {
        const int useLightmap = (!unlit) ? 1 : 0;
        SetShaderValue(
            mapShader,
            lightmaps.useLightmapLoc,
            &useLightmap,
            SHADER_UNIFORM_INT);
    }
}

void drawWorldModels(
    flecs::world& world,
    RenderContext& context,
    const Lens& lens,
    const Frustum& frustum,
    bool unlit) {
    context.worldModelQuery.each([&](flecs::entity modelEntity, Model3D& model, GlobalTransformation& global) {
        if (!modelEntity.has<MapLightmapState>()) {
            const BoundingBox localBounds = GetModelBoundingBox(model.model);
            const BoundingBox worldBounds = transformAabb(localBounds, global.matrix);
            if (!aabbInFrustum(frustum, worldBounds)) {
                return;
            }
            const Vector3 center{
                (worldBounds.min.x + worldBounds.max.x) * 0.5f,
                (worldBounds.min.y + worldBounds.max.y) * 0.5f,
                (worldBounds.min.z + worldBounds.max.z) * 0.5f,
            };
            if (!pvsVisibleFromCamera(world, lens.camera.position, center)) {
                return;
            }
            const Matrix* closedMatrix = nullptr;
            Matrix closedMatrixStorage{};
            if (modelEntity.has<RigidMover>()) {
                Vector3 scale{1.0f, 1.0f, 1.0f};
                if (modelEntity.has<LocalTransformation>()) {
                    scale = modelEntity.get<LocalTransformation>().scale;
                }
                closedMatrixStorage =
                    moverClosedMatrix(modelEntity.get<RigidMover>(), scale);
                closedMatrix = &closedMatrixStorage;
            }
            if (mapLightmapState(world) != nullptr) {
                model.color = sampleBakeTintColorForModel(
                    world, model.model, global.matrix, unlit, closedMatrix);
            } else {
                model.color = sampleReceiverTintColorForModel(
                    world, model.model, global.matrix, unlit, closedMatrix);
            }
        }
        const std::unordered_set<int>* skipMeshes = nullptr;
        std::unordered_set<int> transparentSkip;
        std::unordered_set<int> opaqueSkip;
        if (modelEntity.has<MapLightmapState>()) {
            const MapLightmapState& lightmaps = modelEntity.get<MapLightmapState>();
            if (!lightmaps.transparentMeshIndices.empty()) {
                transparentSkip = std::unordered_set<int>(
                    lightmaps.transparentMeshIndices.begin(),
                    lightmaps.transparentMeshIndices.end());
            }
            if (!lightmaps.skyMeshIndices.empty()) {
                opaqueSkip = std::unordered_set<int>(
                    lightmaps.skyMeshIndices.begin(),
                    lightmaps.skyMeshIndices.end());
                if (!transparentSkip.empty()) {
                    opaqueSkip.insert(transparentSkip.begin(), transparentSkip.end());
                }
                skipMeshes = &opaqueSkip;
            } else if (!transparentSkip.empty()) {
                skipMeshes = &transparentSkip;
            }
        }
        renderWorldModel(modelEntity, model, global, lens, unlit, skipMeshes, nullptr);
    });
    rlDisableShader();
    context.animOverlayQuery.each(
        [&](flecs::entity modelEntity, Model3D& model, GlobalTransformation& global, AnimationPlayer& animationPlayer) {
            if (!modelEntity.has<MapLightmapState>()) {
                const BoundingBox localBounds = GetModelBoundingBox(model.model);
                const BoundingBox worldBounds = transformAabb(localBounds, global.matrix);
                if (!aabbInFrustum(frustum, worldBounds)) {
                    return;
                }
                const Vector3 center{
                    (worldBounds.min.x + worldBounds.max.x) * 0.5f,
                    (worldBounds.min.y + worldBounds.max.y) * 0.5f,
                    (worldBounds.min.z + worldBounds.max.z) * 0.5f,
                };
                if (!pvsVisibleFromCamera(world, lens.camera.position, center)) {
                    return;
                }
            }
            rlPushMatrix();
            rlMultMatrixf(MatrixToFloatV(global.matrix).v);
            drawSkeletonOverlay(model.model, &animationPlayer);
            rlPopMatrix();
        });
}

namespace {

void collectWorldSpriteDrawItems(
    flecs::world& world,
    RenderContext& context,
    const Lens& lens,
    const Frustum& frustum,
    std::vector<SpriteDrawItem>& spriteDrawList) {
    spriteDrawList.clear();
    spriteDrawList.reserve(32);
    context.worldSpriteQuery.each(
        [&](flecs::entity spriteEntity, SpriteInstance& sprite, GlobalTransformation& global) {
            const Vector3 position = translationFromMatrix(global.matrix);
            const float scaleRadius = std::max(
                std::max(std::fabs(global.matrix.m0), std::fabs(global.matrix.m5)),
                std::max(std::fabs(global.matrix.m10), 1.0f));
            const float radius = std::max(2.0f, scaleRadius * 2.0f);
            const Vector3 cullCenter{
                position.x,
                position.y + radius * 0.5f,
                position.z,
            };
            if (!sphereInFrustum(frustum, cullCenter, radius)) {
                return;
            }
            if (!pvsVisibleFromCamera(world, lens.camera.position, position)) {
                return;
            }
            const Vector3 camForward = cameraForwardDir(lens.camera);
            spriteDrawList.push_back(SpriteDrawItem{
                &sprite,
                &global,
                spriteEntity.has<SpriteAnimator>() ? &spriteEntity.get<SpriteAnimator>()
                                                   : nullptr,
                viewDepthAlongAxis(position, lens.camera.position, camForward),
                spriteEntity.has<SpriteOverlay>() ? spriteEntity.get<SpriteOverlay>().layer : 0,
            });
        });
}

Color multiplyParticleTintLocal(Color particle, Color scene) {
    return {
        static_cast<unsigned char>(
            std::clamp(static_cast<int>(particle.r) * static_cast<int>(scene.r) / 255, 0, 255)),
        static_cast<unsigned char>(
            std::clamp(static_cast<int>(particle.g) * static_cast<int>(scene.g) / 255, 0, 255)),
        static_cast<unsigned char>(
            std::clamp(static_cast<int>(particle.b) * static_cast<int>(scene.b) / 255, 0, 255)),
        particle.a,
    };
}

void collectWorldDepthParticleDrawItems(
    flecs::world& world,
    AssetStore& assets,
    const Camera3D& camera,
    bool unlit,
    std::vector<ParticleDrawItem>& out) {
    out.clear();
    out.reserve(256);
    world.each([&](flecs::entity entity, const ParticleSystemInstance& instance) {
        if (entity.has<ParticleFollowViewMuzzle>()) {
            return;
        }
        appendParticleDrawItems(instance, assets, camera, out);
    });
    if (!unlit) {
        for (ParticleDrawItem& item : out) {
            if (item.unlit || !item.depthTest) {
                continue;
            }
            const Color scene = sampleReceiverTintColor(world, item.position, false, 64.0f);
            item.color = multiplyParticleTintLocal(item.color, scene);
        }
    }
    out.erase(
        std::remove_if(
            out.begin(),
            out.end(),
            [](const ParticleDrawItem& item) { return !item.depthTest; }),
        out.end());
}

} // namespace

std::string drawWorldTransparentPass(
    flecs::world& world,
    RenderContext& context,
    const Lens& lens,
    const Frustum& frustum,
    bool unlit) {
    std::string spriteAimStatus;
    if (!world.has<AssetServices>() || world.get<AssetServices>().store == nullptr) {
        return spriteAimStatus;
    }
    AssetStore& assets = *world.get_mut<AssetServices>().store;
    const MapLighting* lighting =
        world.has<MapLighting>() ? &world.get<MapLighting>() : nullptr;
    const std::vector<RankedDynamicLight>* dynamicLights =
        (!unlit && world.has<DynamicLightFrameState>())
            ? &world.get<DynamicLightFrameState>().lights
            : nullptr;
    const FxLightFrameState* fxLights =
        (!unlit && world.has<FxLightFrameState>()) ? &world.get<FxLightFrameState>() : nullptr;

    const Vector3 camForward = cameraForwardDir(lens.camera);
    std::vector<TransparentDrawItem> drawList;
    drawList.reserve(128);

    flecs::entity mapEntity = world.lookup("MapStatic");
    if (mapEntity.is_valid() && mapEntity.has<MapLightmapState>() && mapEntity.has<Model3D>()) {
        const MapLightmapState& lightmaps = mapEntity.get<MapLightmapState>();
        Model3D& mapModel = mapEntity.get_mut<Model3D>();
        const GlobalTransformation& mapGlobal = mapEntity.get<GlobalTransformation>();
        if (!lightmaps.transparentMeshIndices.empty()) {
            for (int meshIndex : lightmaps.transparentMeshIndices) {
                if (meshIndex < 0 || meshIndex >= mapModel.model.meshCount) {
                    continue;
                }
                TransparentDrawItem item{};
                item.kind = TransparentDrawKind::MapMesh;
                item.mapMeshIndex = meshIndex;
                item.viewDepth = meshMinViewDepth(
                    mapModel.model.meshes[meshIndex],
                    mapGlobal.matrix,
                    lens.camera.position,
                    camForward);
                drawList.push_back(item);
            }
        }
    }

    std::vector<SpriteDrawItem> spriteDrawList;
    collectWorldSpriteDrawItems(world, context, lens, frustum, spriteDrawList);
    for (const SpriteDrawItem& sprite : spriteDrawList) {
        TransparentDrawItem item{};
        item.kind = TransparentDrawKind::Sprite;
        item.sprite = sprite;
        item.viewDepth = sprite.viewDepth;
        item.sortLayer = sprite.layer;
        drawList.push_back(item);
    }

    std::vector<ParticleDrawItem> particleDrawList;
    collectWorldDepthParticleDrawItems(world, assets, lens.camera, unlit, particleDrawList);
    for (const ParticleDrawItem& particle : particleDrawList) {
        TransparentDrawItem item{};
        item.kind = TransparentDrawKind::Particle;
        item.particle = particle;
        item.viewDepth =
            viewDepthAlongAxis(particle.position, lens.camera.position, camForward);
        drawList.push_back(item);
    }

    if (drawList.empty()) {
        if (world.has<DebugUiState>()) {
            spriteAimStatus = drawSpriteDebugOverlays(
                lens,
                assets,
                world.get<DebugUiState>(),
                context.worldSpriteQuery);
        }
        return spriteAimStatus;
    }

    std::sort(
        drawList.begin(),
        drawList.end(),
        [](const TransparentDrawItem& a, const TransparentDrawItem& b) {
            if (a.viewDepth > b.viewDepth) {
                return true;
            }
            if (b.viewDepth > a.viewDepth) {
                return false;
            }
            return a.sortLayer < b.sortLayer;
        });

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BlendMode activeBlend = BLEND_ALPHA;
    BeginBlendMode(activeBlend);

    const Matrix matView = MatrixLookAt(lens.camera.position, lens.camera.target, lens.camera.up);
    const Vector3 screenUp = Vector3Normalize(Vector3{matView.m1, matView.m5, matView.m9});
    const Vector3 worldUp{0.0f, 1.0f, 0.0f};
    const Texture2D* particleFilterTex = nullptr;
    Model3D* mapDrawModel = nullptr;
    GlobalTransformation* mapDrawGlobal = nullptr;
    const MapLightmapState* mapDrawLightmaps = nullptr;
    if (mapEntity.is_valid() && mapEntity.has<MapLightmapState>() && mapEntity.has<Model3D>()) {
        mapDrawModel = &mapEntity.get_mut<Model3D>();
        mapDrawGlobal = &mapEntity.get_mut<GlobalTransformation>();
        mapDrawLightmaps = &mapEntity.get<MapLightmapState>();
        const int mapUseLightmap = unlit ? 0 : 1;
        if (mapDrawLightmaps->available && mapDrawLightmaps->lightmapShader.id != 0) {
            prepareLightmapShaderDraw(
                mapDrawLightmaps->lightmapShader,
                mapDrawLightmaps->useLightmapLoc,
                mapUseLightmap,
                mapDrawGlobal->matrix,
                world);
        }
    }

    for (const TransparentDrawItem& item : drawList) {
        if (item.kind == TransparentDrawKind::MapMesh) {
            if (mapDrawModel == nullptr || mapDrawGlobal == nullptr) {
                continue;
            }
            if (item.mapMeshIndex < 0 || item.mapMeshIndex >= mapDrawModel->model.meshCount) {
                continue;
            }
            if (activeBlend != BLEND_ALPHA) {
                EndBlendMode();
                activeBlend = BLEND_ALPHA;
                BeginBlendMode(activeBlend);
            }
            if (mapDrawLightmaps != nullptr && mapDrawLightmaps->lightmapShader.id != 0 && !unlit) {
                uploadLightmapDynamicLights(world, mapDrawLightmaps->lightmapShader);
            }
            rlPushMatrix();
            rlMultMatrixf(MatrixToFloatV(mapDrawGlobal->matrix).v);
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(
                kTransparentMeshPolygonOffsetFactor,
                kTransparentMeshPolygonOffsetUnits);
            DrawMesh(
                mapDrawModel->model.meshes[item.mapMeshIndex],
                mapDrawModel->model.materials[item.mapMeshIndex],
                MatrixIdentity());
            glDisable(GL_POLYGON_OFFSET_FILL);
            rlPopMatrix();
        } else if (item.kind == TransparentDrawKind::Sprite) {
            BlendMode want = BLEND_ALPHA;
            if (const SpriteAsset* asset = assets.getSpriteAsset(item.sprite.sprite->sprite);
                asset != nullptr && asset->blend == SpriteBlendMode::Additive) {
                want = BLEND_ADD_COLORS;
            }
            if (want != activeBlend) {
                EndBlendMode();
                activeBlend = want;
                BeginBlendMode(activeBlend);
            }
            drawWorldSprite(
                *item.sprite.sprite,
                *item.sprite.global,
                lens,
                assets,
                lighting,
                dynamicLights,
                fxLights,
                unlit,
                item.sprite.animator);
        } else if (item.kind == TransparentDrawKind::Particle) {
            const ParticleDrawItem& particle = item.particle;
            if (particle.texture == nullptr || particle.texture->id == 0 || particle.size <= 0.0f) {
                continue;
            }
            if (particleFilterTex != particle.texture) {
                if (particleFilterTex != nullptr) {
                    SetTextureFilter(*particleFilterTex, TEXTURE_FILTER_POINT);
                }
                SetTextureFilter(*particle.texture, TEXTURE_FILTER_BILINEAR);
                particleFilterTex = particle.texture;
            }
            const bool additive = particle.blend == ParticleBlendMode::Additive;
            const BlendMode want = additive ? BLEND_ADD_COLORS : BLEND_ALPHA_PREMULTIPLY;
            if (want != activeBlend) {
                EndBlendMode();
                activeBlend = want;
                BeginBlendMode(activeBlend);
            }
            const float alpha = static_cast<float>(particle.color.a) / 255.0f;
            const Color tint{
                static_cast<unsigned char>(
                    std::clamp(static_cast<float>(particle.color.r) * alpha, 0.0f, 255.0f)),
                static_cast<unsigned char>(
                    std::clamp(static_cast<float>(particle.color.g) * alpha, 0.0f, 255.0f)),
                static_cast<unsigned char>(
                    std::clamp(static_cast<float>(particle.color.b) * alpha, 0.0f, 255.0f)),
                particle.color.a,
            };
            const Vector2 size{particle.size, particle.size};
            const Vector2 origin = Vector2Scale(size, 0.5f);
            const Vector3 up =
                particle.billboard == SpriteBillboardMode::Screen ? screenUp : worldUp;
            DrawBillboardPro(
                lens.camera,
                *particle.texture,
                particle.source,
                particle.position,
                up,
                size,
                origin,
                0.0f,
                tint);
        }
    }

    if (particleFilterTex != nullptr) {
        SetTextureFilter(*particleFilterTex, TEXTURE_FILTER_POINT);
    }
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlEnableDepthMask();

    if (world.has<DebugUiState>()) {
        spriteAimStatus = drawSpriteDebugOverlays(
            lens,
            assets,
            world.get<DebugUiState>(),
            context.worldSpriteQuery);
    }
    return spriteAimStatus;
}

std::string drawWorldSprites(
    flecs::world& world,
    RenderContext& context,
    const Lens& lens,
    const Frustum& frustum,
    bool unlit) {
    return drawWorldTransparentPass(world, context, lens, frustum, unlit);
}

void drawWorldDebugOverlays(flecs::world& world) {
    if (world.has<DebugUiState>()) {
        const DebugUiState& debugUi = world.get<DebugUiState>();
        std::int32_t currentLeaf = -1;
        if (world.has<MapBsp>()) {
            const MapBsp& mapBsp = world.get<MapBsp>();
            flecs::entity camera{};
            if (world.has<PlayerEntity>()) {
                camera = world.get<PlayerEntity>().entity;
            }
            if (camera.is_valid() && camera.has<Lens>()) {
                currentLeaf = pointLeaf(mapBsp.tree, camera.get<Lens>().camera.position);
            }
            drawBspDebugOverlays(mapBsp.tree, debugUi, currentLeaf);
        }
        if (world.has<MapFac>()) {
            drawFacDebugOverlays(world.get<MapFac>().fac, debugUi, currentLeaf);
        }
    }

    if (world.has<DebugUiState>() && world.get<DebugUiState>().showGraphs &&
        world.has<MapGraphs>()) {
        drawGraphDebugOverlays(world.get<MapGraphs>().document);
    }

    if (world.has<DebugLinePool>()) {
        drawDebugLinePool(world.get<DebugLinePool>());
    }
}

}
