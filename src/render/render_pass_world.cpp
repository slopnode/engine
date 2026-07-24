#include "render/render_pass_world.hpp"

#include "assets/asset_services.hpp"
#include "assets/asset_store.hpp"
#include "map/bsp.hpp"
#include "map/graph.hpp"
#include "map/light_components.hpp"
#include "map/light_sample.hpp"
#include "map/fac.hpp"
#include "map/pvs.hpp"
#include "render/animation_player.hpp"
#include "render/dynamic_light.hpp"
#include "render/dynamic_light_shadows.hpp"
#include "render/fx_local_light.hpp"
#include "render/render_debug.hpp"
#include "render/sprite_animator.hpp"
#include "render/sprite_billboard.hpp"
#include "script/first_person_script.hpp"
#include "ui/ui_state.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

namespace slopengine {

void renderWorldModel(
    flecs::entity entity,
    Model3D& model,
    GlobalTransformation& globalTransform,
    const Lens& lens) {
    rlPushMatrix();
    rlMultMatrixf(MatrixToFloatV(globalTransform.matrix).v);

    if (entity.has<MapLightmapState>()) {
        const MapLightmapState& lightmaps = entity.get<MapLightmapState>();
        if (lightmaps.available && model.model.materialCount > 0) {
            Shader shader = model.model.materials[0].shader;
            if (shader.id != 0) {
                if (shader.locs[SHADER_LOC_MATRIX_MODEL] < 0) {
                    shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shader, "matModel");
                }
                if (shader.locs[SHADER_LOC_MATRIX_MODEL] >= 0) {
                    SetShaderValueMatrix(
                        shader,
                        shader.locs[SHADER_LOC_MATRIX_MODEL],
                        globalTransform.matrix);
                }
            }
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

    DrawModel(model.model, Vector3Zero(), 1.0f, model.color);
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
        colorFeet = addLinearRgbToColor(
            colorFeet,
            evaluateOverlayLightsAtPoint(
                dynamicLights, fxLights, feetPoint, normal, occlusionBvh));
        colorHead = addLinearRgbToColor(
            colorHead,
            evaluateOverlayLightsAtPoint(
                dynamicLights, fxLights, headPoint, normal, occlusionBvh));
    }
    if (billboard->fullbright && useBrightmap) {
        colorFeet = WHITE;
        colorHead = WHITE;
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
    const Color colors[4] = {colorFeet, colorFeet, colorHead, colorHead};

    SpriteBillboardShader* brightShader = nullptr;
    if (useBrightmap) {
        brightShader = &spriteBillboardShader(assets);
        if (!brightShader->ready) {
            brightShader = nullptr;
        }
    }

    if (brightShader != nullptr) {
        const Vector4 albedoRect{
            source.x,
            source.y,
            source.width,
            source.height,
        };
        const Vector2 atlasSize{texW, texH};
        const int useBright = 1;
        BeginShaderMode(brightShader->shader);
        if (brightShader->albedoRectLoc >= 0) {
            SetShaderValue(
                brightShader->shader,
                brightShader->albedoRectLoc,
                &albedoRect,
                SHADER_UNIFORM_VEC4);
        }
        if (brightShader->atlasSizeLoc >= 0) {
            SetShaderValue(
                brightShader->shader,
                brightShader->atlasSizeLoc,
                &atlasSize,
                SHADER_UNIFORM_VEC2);
        }
        if (brightShader->useBrightmapLoc >= 0) {
            SetShaderValue(
                brightShader->shader,
                brightShader->useBrightmapLoc,
                &useBright,
                SHADER_UNIFORM_INT);
        }
        if (brightShader->brightMapLoc >= 0) {
            SetShaderValueTexture(
                brightShader->shader,
                brightShader->brightMapLoc,
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
    if (brightShader != nullptr) {
        EndShaderMode();
    }
}

} // namespace

std::vector<RankedDynamicLight> gatherDynamicLights(
    flecs::world& world,
    const Lens& lens,
    const Lens& presentLens,
    const Frustum& frustum,
    bool unlit) {
    std::vector<RankedDynamicLight> rankedLights;
    if (unlit) {
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
    rankedLights = rankDynamicLights(candidates, lens.camera.position);
    static int sLastDynCount = -1;
    if (static_cast<int>(rankedLights.size()) != sLastDynCount) {
        sLastDynCount = static_cast<int>(rankedLights.size());
        TraceLog(LOG_INFO, "MAP: active dynamic lights=%d", sLastDynCount);
        for (const RankedDynamicLight& light : rankedLights) {
            TraceLog(
                LOG_INFO,
                "MAP: dyn light pos=(%.2f,%.2f,%.2f) dir=(%.2f,%.2f,%.2f) intensity=%.2f kind=%s",
                light.position.x,
                light.position.y,
                light.position.z,
                light.direction.x,
                light.direction.y,
                light.direction.z,
                light.light.intensity,
                light.light.kind == DynamicLightKind::Spot ? "spot" : "point");
        }
    }
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
    bool unlit) {
    flecs::entity mapEntity = world.lookup("MapStatic");
    if (!mapEntity.is_valid() || !mapEntity.has<MapLightmapState>() || !mapEntity.has<Model3D>()) {
        return;
    }
    const MapLightmapState& lightmaps = mapEntity.get<MapLightmapState>();
    Model3D& model = mapEntity.get_mut<Model3D>();
    if (!lightmaps.available || model.model.materialCount <= 0) {
        return;
    }
    Shader& mapShader = model.model.materials[0].shader;
    if (mapShader.id == 0) {
        return;
    }
    if (world.has<DynamicLightFrameState>()) {
        DynamicLightFrameState& frameState = world.get_mut<DynamicLightFrameState>();
        if (!frameState.bindings.resolved) {
            resolveDynamicLightShaderBindings(mapShader, frameState.bindings);
        }
        uploadDynamicLightsToShader(mapShader, frameState.bindings, rankedLights, nullptr);
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
            const Vector3 origin{
                global.matrix.m12,
                global.matrix.m13 + 0.05f,
                global.matrix.m14,
            };
            model.color = sampleReceiverTintColor(world, origin, unlit);
        }
        renderWorldModel(modelEntity, model, global, lens);
    });
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

std::string drawWorldSprites(
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

    struct SpriteDrawItem {
        const SpriteInstance* sprite = nullptr;
        const GlobalTransformation* global = nullptr;
        const SpriteAnimator* animator = nullptr;
        float distSq = 0.0f;
    };
    std::vector<SpriteDrawItem> spriteDrawList;
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
            const float dx = position.x - lens.camera.position.x;
            const float dy = position.y - lens.camera.position.y;
            const float dz = position.z - lens.camera.position.z;
            spriteDrawList.push_back(SpriteDrawItem{
                &sprite,
                &global,
                spriteEntity.has<SpriteAnimator>() ? &spriteEntity.get<SpriteAnimator>()
                                                   : nullptr,
                dx * dx + dy * dy + dz * dz,
            });
        });
    std::sort(
        spriteDrawList.begin(),
        spriteDrawList.end(),
        [](const SpriteDrawItem& a, const SpriteDrawItem& b) {
            return a.distSq > b.distSq;
        });

    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();
    for (const SpriteDrawItem& item : spriteDrawList) {
        drawWorldSprite(
            *item.sprite,
            *item.global,
            lens,
            assets,
            lighting,
            dynamicLights,
            fxLights,
            unlit,
            item.animator);
    }
    rlEnableDepthMask();
    EndBlendMode();

    if (world.has<DebugUiState>()) {
        spriteAimStatus = drawSpriteDebugOverlays(
            lens,
            assets,
            world.get<DebugUiState>(),
            context.worldSpriteQuery);
    }
    return spriteAimStatus;
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
}

}
