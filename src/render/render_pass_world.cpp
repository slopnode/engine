#include "render/render_pass_world.hpp"

#include "assets/asset_services.hpp"
#include "assets/asset_store.hpp"
#include "core/frame_counters.hpp"
#include "map/bsp.hpp"
#include "map/graph.hpp"
#include "map/light_components.hpp"
#include "map/light_sample.hpp"
#include "map/pvs.hpp"
#include "map/area_gate_runtime.hpp"
#include "map/pvs_audit.hpp"
#include "particles/particle_sim.hpp"
#include "render/gpu_skinning.hpp"
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
#include <limits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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

flecs::entity mapStaticEntity(flecs::world& world) {
    static const ecs_world_t* cachedWorld = nullptr;
    static flecs::entity_t cachedId = 0;
    if (cachedWorld == world.c_ptr() && cachedId != 0 && world.is_alive(cachedId)) {
        const char* name = ecs_get_name(world.c_ptr(), cachedId);
        if (name != nullptr && std::strcmp(name, "MapStatic") == 0) {
            return world.entity(cachedId);
        }
    }
    flecs::entity mapEntity = world.lookup("MapStatic");
    cachedWorld = world.c_ptr();
    cachedId = mapEntity.is_valid() ? mapEntity.id() : 0;
    return mapEntity;
}

const MapLightmapState* mapLightmapState(flecs::world& world) {
    flecs::entity mapEntity = mapStaticEntity(world);
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
    DynamicLightFrameState& frame = world.get_mut<DynamicLightFrameState>();
    if (!frame.bindings.resolved || frame.uploadedShaderId == shader.id) {
        return;
    }
    frame.uploadedShaderId = shader.id;
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

// Mirrors raylib's DrawModelEx tint trick: temporarily multiply the mesh material's diffuse
// color, draw, then restore. Needed because we can't route this through raylib's own
// DrawModel/DrawModelEx without losing the per-mesh emissionPower uniform upload below.
Color tintMeshDiffuse(Material& material, Color tint) {
    MaterialMap& albedoMap = material.maps[MATERIAL_MAP_ALBEDO];
    const Color original = albedoMap.color;
    albedoMap.color = {
        static_cast<unsigned char>((static_cast<int>(original.r) * tint.r) / 255),
        static_cast<unsigned char>((static_cast<int>(original.g) * tint.g) / 255),
        static_cast<unsigned char>((static_cast<int>(original.b) * tint.b) / 255),
        static_cast<unsigned char>((static_cast<int>(original.a) * tint.a) / 255),
    };
    return original;
}

void drawModelMeshes(
    const Model& model,
    const std::unordered_set<int>* skipMeshIndices,
    const std::unordered_set<int>* onlyMeshIndices,
    const std::unordered_set<int>* polygonOffsetBackMeshIndices,
    const std::unordered_set<int>* twoSidedMeshIndices = nullptr,
    int emissionPowerLoc = -1,
    Color tint = WHITE,
    Shader skinFallback = {},
    int* drawCounter = nullptr,
    const std::vector<unsigned char>* meshMask = nullptr) {
    for (int meshIndex = 0; meshIndex < model.meshCount; ++meshIndex) {
        if (meshMask != nullptr && meshIndex < static_cast<int>(meshMask->size()) &&
            (*meshMask)[static_cast<std::size_t>(meshIndex)] == 0) {
            continue;
        }
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
        const bool twoSided =
            twoSidedMeshIndices != nullptr && twoSidedMeshIndices->count(meshIndex) > 0;
        if (twoSided) {
            rlDisableBackfaceCulling();
        }
        Material& material = model.materials[meshIndex];
        if (emissionPowerLoc >= 0) {
            SetShaderValue(
                material.shader,
                emissionPowerLoc,
                &material.maps[MATERIAL_MAP_SPECULAR].value,
                SHADER_UNIFORM_FLOAT);
        }
        const Color originalDiffuse = tintMeshDiffuse(material, tint);
        drawModelMesh(model, meshIndex, material, skinFallback);
        if (drawCounter != nullptr) {
            ++*drawCounter;
        }
        material.maps[MATERIAL_MAP_ALBEDO].color = originalDiffuse;
        if (twoSided) {
            rlEnableBackfaceCulling();
        }
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

float boundsCenterViewDepth(
    const BoundingBox& worldBounds, Vector3 cameraPos, Vector3 cameraForward) {
    const Vector3 center{
        (worldBounds.min.x + worldBounds.max.x) * 0.5f,
        (worldBounds.min.y + worldBounds.max.y) * 0.5f,
        (worldBounds.min.z + worldBounds.max.z) * 0.5f,
    };
    return viewDepthAlongAxis(center, cameraPos, cameraForward);
}

// Depth of the point on `worldBounds` closest to `referencePos` — i.e. the face's local
// depth right where the other item actually is, rather than one depth for the whole face.
// A long transparent surface (e.g. a walkway grate spanning a whole corridor) has wildly
// different depth at each end, so a single per-face key can't order it against a sprite
// correctly no matter where on the surface that sprite stands.
float boundsNearestPointViewDepth(
    const BoundingBox& worldBounds,
    Vector3 referencePos,
    Vector3 cameraPos,
    Vector3 cameraForward) {
    const Vector3 closest{
        std::clamp(referencePos.x, worldBounds.min.x, worldBounds.max.x),
        std::clamp(referencePos.y, worldBounds.min.y, worldBounds.max.y),
        std::clamp(referencePos.z, worldBounds.min.z, worldBounds.max.z),
    };
    return viewDepthAlongAxis(closest, cameraPos, cameraForward);
}

float boundsMinViewDepth(const BoundingBox& worldBounds, Vector3 cameraPos, Vector3 cameraForward) {
    float minDepth = std::numeric_limits<float>::max();
    for (int corner = 0; corner < 8; ++corner) {
        const Vector3 point{
            (corner & 1) != 0 ? worldBounds.max.x : worldBounds.min.x,
            (corner & 2) != 0 ? worldBounds.max.y : worldBounds.min.y,
            (corner & 4) != 0 ? worldBounds.max.z : worldBounds.min.z,
        };
        minDepth = std::min(minDepth, viewDepthAlongAxis(point, cameraPos, cameraForward));
    }
    return minDepth;
}

// Plane of a (near-)flat face, derived from its actual vertices rather than assumed from
// bounding-box axis alignment, so an angled face still gets a correct normal.
struct FacePlane {
    Vector3 normal{0.0f, 1.0f, 0.0f};
    float d = 0.0f;
};

struct SpriteDrawItem {
    const SpriteInstance* sprite = nullptr;
    const GlobalTransformation* global = nullptr;
    const SpriteAnimator* animator = nullptr;
    float viewDepth = 0.0f;
    int layer = 0;
    std::uint32_t hiddenParts = 0;
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
    BoundingBox worldBounds{};
    FacePlane facePlane{};
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
    const std::unordered_set<int>* onlyMeshIndices,
    const ModelLightingUniforms* modelLighting,
    const std::vector<unsigned char>* meshMask) {
    rlPushMatrix();
    rlMultMatrixf(MatrixToFloatV(globalTransform.matrix).v);
    setActiveShaderParams(entity.has<ShaderParams>() ? &entity.get<ShaderParams>() : nullptr);

    flecs::world world = entity.world();
    int* drawCounter = entity.has<MapLightmapState>() ? &frameCounters().mapMeshDraws
                                                      : &frameCounters().modelMeshDraws;
    std::vector<Shader> previousShaders;
    bool swappedPropShader = false;
    const MapLightmapState* mapLightmaps = nullptr;
    const int mapUseLightmap = unlit ? 0 : 1;
    int emissionPowerLoc = -1;
    Shader skinFallback{};
    if (modelUsesGpuSkinning(model.model) && world.has<AssetServices>() &&
        world.get<AssetServices>().store != nullptr) {
        skinFallback = world.get_mut<AssetServices>().store->getSkinningShader();
    }

    if (entity.has<MapLightmapState>()) {
        const MapLightmapState& lightmaps = entity.get<MapLightmapState>();
        if (lightmaps.available && lightmaps.lightmapShader.id != 0) {
            prepareLightmapShaderDraw(
                lightmaps.lightmapShader,
                lightmaps.useLightmapLoc,
                mapUseLightmap,
                globalTransform.matrix,
                world);
            emissionPowerLoc = lightmaps.emissionPowerLoc;
        }
    } else if (model.model.materialCount > 0 && (modelLighting != nullptr || !entity.has<ViewSpace>())) {
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
                entity.has<BakedLightmapModel>() ? mapUseLightmap : 0,
                globalTransform.matrix,
                world);
            uploadModelLighting(mapLightmaps->lightmapShader, modelLighting);
            emissionPowerLoc = mapLightmaps->emissionPowerLoc;
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
        const std::unordered_set<int>* twoSided = nullptr;
        std::unordered_set<int> twoSidedOffset;
        if (entity.has<MapLightmapState>()) {
            const MapLightmapState& lightmaps = entity.get<MapLightmapState>();
            if (!lightmaps.detailMeshIndices.empty()) {
                detailOffset.insert(
                    lightmaps.detailMeshIndices.begin(),
                    lightmaps.detailMeshIndices.end());
                polygonOffsetBack = &detailOffset;
            }
            if (!lightmaps.twoSidedMeshIndices.empty()) {
                twoSidedOffset.insert(
                    lightmaps.twoSidedMeshIndices.begin(),
                    lightmaps.twoSidedMeshIndices.end());
                twoSided = &twoSidedOffset;
            }
        }
        drawModelMeshes(
            model.model,
            skipMeshIndices,
            onlyMeshIndices,
            polygonOffsetBack,
            twoSided,
            emissionPowerLoc,
            WHITE,
            skinFallback,
            drawCounter,
            meshMask);
    } else {
        const std::unordered_set<int>* polygonOffsetBack = nullptr;
        std::unordered_set<int> detailOffset;
        const std::unordered_set<int>* twoSided = nullptr;
        std::unordered_set<int> twoSidedOffset;
        if (entity.has<MapLightmapState>()) {
            const MapLightmapState& lightmaps = entity.get<MapLightmapState>();
            if (!lightmaps.detailMeshIndices.empty()) {
                detailOffset.insert(
                    lightmaps.detailMeshIndices.begin(),
                    lightmaps.detailMeshIndices.end());
                polygonOffsetBack = &detailOffset;
            }
            if (!lightmaps.twoSidedMeshIndices.empty()) {
                twoSidedOffset.insert(
                    lightmaps.twoSidedMeshIndices.begin(),
                    lightmaps.twoSidedMeshIndices.end());
                twoSided = &twoSidedOffset;
            }
        }
        if (polygonOffsetBack != nullptr || twoSided != nullptr) {
            drawModelMeshes(
                model.model,
                nullptr,
                nullptr,
                polygonOffsetBack,
                twoSided,
                emissionPowerLoc,
                WHITE,
                skinFallback,
                drawCounter,
                meshMask);
        } else {
            const Color tint = (swappedPropShader && modelLighting != nullptr) ? WHITE : model.color;
            drawModelMeshes(
                model.model,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                emissionPowerLoc,
                tint,
                skinFallback,
                drawCounter,
                meshMask);
        }
    }

    if (swappedPropShader) {
        for (int i = 0; i < model.model.materialCount; ++i) {
            model.model.materials[i].shader = previousShaders[static_cast<std::size_t>(i)];
        }
        if (mapLightmaps != nullptr && modelLighting != nullptr) {
            uploadModelLighting(mapLightmaps->lightmapShader, nullptr);
        }
        if (mapLightmaps != nullptr && mapLightmaps->useLightmapLoc >= 0) {
            SetShaderValue(
                mapLightmaps->lightmapShader,
                mapLightmaps->useLightmapLoc,
                &mapUseLightmap,
                SHADER_UNIFORM_INT);
        }
    }

    setActiveShaderParams(nullptr);
    rlPopMatrix();
}

namespace {

const char* primitiveNameForMesh(flecs::entity entity, int meshIndex) {
    static std::string buffer;
    if (!entity.has<Model3D>()) {
        return "?";
    }
    const Model3D& model = entity.get<Model3D>();
    if (meshIndex < 0 || meshIndex >= model.model.materialCount) {
        return "?";
    }
    const Texture2D& albedo = model.model.materials[meshIndex].maps[MATERIAL_MAP_ALBEDO].texture;
    buffer = "material texture " + std::to_string(albedo.id);
    return buffer.c_str();
}

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

struct PvsCull {
    const BspTree* tree = nullptr;
    const PvsFile* pvs = nullptr;
    std::int32_t fromLeaf = -1;
    AreaGate gate{};
};

PvsCull makePvsCull(flecs::world& world, Vector3 cameraPos) {
    PvsCull cull{};
    if (!world.has<MapPvs>() || !world.has<MapBsp>()) {
        return cull;
    }
    cull.tree = &world.get<MapBsp>().tree;
    cull.pvs = &world.get<MapPvs>().pvs;
    cull.fromLeaf = pvsSampleLeaf(*cull.tree, cameraPos);
    cull.gate = areaGateFromPoint(world, cameraPos);
    return cull;
}

bool pvsCullPoint(const PvsCull& cull, Vector3 point) {
    if (cull.tree == nullptr || cull.pvs == nullptr) {
        return true;
    }
    ++frameCounters().pvsTests;
    return pvsVisiblePointFromLeaf(*cull.tree, *cull.pvs, cull.fromLeaf, point, &cull.gate);
}

bool pvsCullBox(const PvsCull& cull, const BoundingBox& box) {
    if (cull.tree == nullptr || cull.pvs == nullptr) {
        return true;
    }
    ++frameCounters().pvsTests;
    return pvsVisibleBoxFromLeaf(*cull.tree, *cull.pvs, cull.fromLeaf, box, &cull.gate);
}

bool pvsCullLeaves(const PvsCull& cull, const std::vector<std::int32_t>& leaves) {
    if (cull.tree == nullptr || cull.pvs == nullptr) {
        return true;
    }
    ++frameCounters().pvsTests;
    return pvsVisibleLeavesFromLeaf(*cull.tree, *cull.pvs, cull.fromLeaf, leaves, &cull.gate);
}

struct SpriteBillboardShader {
    Shader shader{};
    int albedoRectLoc = -1;
    int atlasSizeLoc = -1;
    int useBrightmapLoc = -1;
    int brightMapLoc = -1;
    int usePartMaskLoc = -1;
    int partMaskLoc = -1;
    int hiddenPartsMaskLoc = -1;
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
    state.partMaskLoc = GetShaderLocation(state.shader, "texture2");
    state.usePartMaskLoc = GetShaderLocation(state.shader, "usePartMask");
    state.hiddenPartsMaskLoc = GetShaderLocation(state.shader, "hiddenPartsMask");
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
    const SpriteAnimator* animator,
    std::uint32_t hiddenPartsMask) {
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

    const bool hasBrightMask = billboard->brightTexture != nullptr && billboard->brightTexture->id != 0;
    const bool useBrightmap = billboard->fullbright && hasBrightMask;
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
            if (auto feet = sampleLightProbe(*lighting, feetOrigin, {0.0f, -1.0f, 0.0f})) {
                colorFeet = *feet;
            } else if (auto feetRay =
                           sampleMapLight(*lighting, feetOrigin, {0.0f, -1.0f, 0.0f}, 2.0f)) {
                colorFeet = *feetRay;
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
                if (auto head = sampleLightProbe(*lighting, headPos, headDir)) {
                    colorHead = *head;
                } else if (auto headRay = sampleMapLight(*lighting, headPos, headDir, 4.0f)) {
                    colorHead = *headRay;
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

        const bool usePartMask =
            hiddenPartsMask != 0 && billboard->partMaskTexture != nullptr &&
            billboard->partMaskTexture->id != 0;
        const int usePartMaskInt = usePartMask ? 1 : 0;
        if (billboardShader->usePartMaskLoc >= 0) {
            SetShaderValue(
                billboardShader->shader,
                billboardShader->usePartMaskLoc,
                &usePartMaskInt,
                SHADER_UNIFORM_INT);
        }
        if (usePartMask) {
            if (billboardShader->hiddenPartsMaskLoc >= 0) {
                const int hiddenPartsMaskInt = static_cast<int>(hiddenPartsMask);
                SetShaderValue(
                    billboardShader->shader,
                    billboardShader->hiddenPartsMaskLoc,
                    &hiddenPartsMaskInt,
                    SHADER_UNIFORM_INT);
            }
            if (billboardShader->partMaskLoc >= 0) {
                SetShaderValueTexture(
                    billboardShader->shader,
                    billboardShader->partMaskLoc,
                    *billboard->partMaskTexture);
            }
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
    int maxLights,
    int maxShadowed) {
    std::vector<RankedDynamicLight> rankedLights;
    if (unlit || !enableDynamicLights) {
        return rankedLights;
    }
    std::vector<RankedDynamicLight> candidates;
    const PvsCull cull = makePvsCull(world, lens.camera.position);
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
            ++frameCounters().lightsCulled;
            return;
        }
        if (!viewSpace && !pvsCullPoint(cull, ranked.position)) {
            ++frameCounters().lightsCulled;
            return;
        }
        ranked.linearRgb = dynamicLightLinearRgb(light);
        candidates.push_back(ranked);
    });
    rankedLights = rankDynamicLights(
        candidates,
        lens.camera.position,
        maxLights,
        maxShadowed);
    frameCounters().lightsActive = static_cast<int>(rankedLights.size());
    return rankedLights;
}

void storeDynamicLightFrameState(
    flecs::world& world,
    const std::vector<RankedDynamicLight>& rankedLights) {
    if (world.has<DynamicLightFrameState>()) {
        DynamicLightFrameState& frame = world.get_mut<DynamicLightFrameState>();
        frame.lights = rankedLights;
        frame.uploadedShaderId = 0;
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
            frameState.uploadedShaderId = mapShader.id;
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
    const MapLightmapState* propLightmaps = mapLightmapState(world);
    const bool perPixelModels = !unlit && propLightmaps != nullptr && world.has<MapLighting>() &&
        world.has<ModelLightingState>() && modelLightingSupported(propLightmaps->lightmapShader);
    ModelLightingState* modelLightingState = nullptr;
    if (perPixelModels) {
        modelLightingState = &world.get_mut<ModelLightingState>();
        gatherStaticModelLights(world, *modelLightingState);
    }
    const PvsCull cull = makePvsCull(world, lens.camera.position);
    context.worldModelQuery.each([&](flecs::entity modelEntity, Model3D& model, GlobalTransformation& global) {
        ModelLightingUniforms modelLighting{};
        bool useModelLighting = false;
        if (!modelEntity.has<MapLightmapState>()) {
            const BoundingBox localBounds = modelLocalBounds(model);
            const BoundingBox worldBounds = transformAabb(localBounds, global.matrix);
            if (!aabbInFrustum(frustum, worldBounds)) {
                ++frameCounters().modelsFrustumCulled;
                return;
            }
            if (!pvsCullBox(cull, worldBounds)) {
                ++frameCounters().modelsPvsCulled;
                return;
            }
            if (!modelEntity.has<BakedLightmapModel>()) {
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
                if (perPixelModels && !modelEntity.has<RigidMover>()) {
                    useModelLighting = computeModelLighting(
                        world, *modelLightingState, modelEntity, model.model, global.matrix, modelLighting);
                }
                if (!useModelLighting && mapLightmapState(world) != nullptr) {
                    model.color = sampleBakeTintColorForModel(
                        world, model.model, global.matrix, unlit, closedMatrix, &localBounds);
                } else if (!useModelLighting) {
                    model.color = sampleReceiverTintColorForModel(
                        world, model.model, global.matrix, unlit, closedMatrix, &localBounds);
                } else if (mapLightmapState(world) == nullptr) {
                    model.color = sampleBakeTintColorForModel(
                        world, model.model, global.matrix, false, nullptr, &localBounds);
                }
            }
        }
        const std::unordered_set<int>* skipMeshes = nullptr;
        std::unordered_set<int> transparentSkip;
        std::unordered_set<int> opaqueSkip;
        std::vector<unsigned char> meshMask;
        const bool cullMapMeshes =
            !world.has<DebugUiState>() || world.get<DebugUiState>().mapMeshCull;
        static const bool cullAudit = std::getenv("SLOPENGINE_CULL_AUDIT") != nullptr;
        static double lastCullAudit = 0.0;
        const bool auditNow = cullAudit && GetTime() - lastCullAudit > 1.0;
        if (modelEntity.has<MapLightmapState>()) {
            const MapLightmapState& lightmaps = modelEntity.get<MapLightmapState>();
            if (!lightmaps.opaqueMeshes.empty() && cullMapMeshes) {
                meshMask.assign(static_cast<std::size_t>(std::max(0, model.model.meshCount)), 1);
                for (const MapMeshCull& meshCull : lightmaps.opaqueMeshes) {
                    if (meshCull.meshIndex < 0 || meshCull.meshIndex >= model.model.meshCount) {
                        continue;
                    }
                    const BoundingBox worldBounds = transformAabb(meshCull.bounds, global.matrix);
                    const char* culledBy = nullptr;
                    if (!aabbInFrustum(frustum, worldBounds)) {
                        meshMask[static_cast<std::size_t>(meshCull.meshIndex)] = 0;
                        ++frameCounters().mapFrustumCulled;
                        culledBy = "frustum";
                    } else if (!meshCull.leaves.empty() && !pvsCullLeaves(cull, meshCull.leaves)) {
                        meshMask[static_cast<std::size_t>(meshCull.meshIndex)] = 0;
                        ++frameCounters().mapPvsCulled;
                        culledBy = "pvs";
                    }
                    if (auditNow && culledBy != nullptr && cull.tree != nullptr) {
                        const Mesh& mesh = model.model.meshes[meshCull.meshIndex];
                        const int triangles = mesh.triangleCount;
                        const int step = std::max(1, triangles / 24);
                        for (int t = 0; t < triangles; t += step) {
                            Vector3 centroid{};
                            for (int k = 0; k < 3; ++k) {
                                const int vi = mesh.indices != nullptr ? mesh.indices[t * 3 + k] : t * 3 + k;
                                centroid.x += mesh.vertices[vi * 3 + 0];
                                centroid.y += mesh.vertices[vi * 3 + 1];
                                centroid.z += mesh.vertices[vi * 3 + 2];
                            }
                            centroid = Vector3Scale(centroid, 1.0f / 3.0f);
                            const Vector3 worldPoint = Vector3Transform(centroid, global.matrix);
                            const Vector3 toCam = Vector3Subtract(lens.camera.position, worldPoint);
                            const float dist = Vector3Length(toCam);
                            if (dist < 1e-3f) {
                                continue;
                            }
                            const Vector3 probe = Vector3Add(worldPoint, Vector3Scale(toCam, 0.03f / dist));
                            if (segmentOpenThroughTree(*cull.tree, lens.camera.position, probe)) {
                                TraceLog(
                                    LOG_WARNING,
                                    "CULL: mesh %d (%s) culled by %s but visible at (%.1f %.1f %.1f) from camera (%.1f %.1f %.1f) leaf %d bounds (%.1f %.1f %.1f)-(%.1f %.1f %.1f) leaves %d",
                                    meshCull.meshIndex,
                                    primitiveNameForMesh(modelEntity, meshCull.meshIndex),
                                    culledBy,
                                    worldPoint.x, worldPoint.y, worldPoint.z,
                                    lens.camera.position.x, lens.camera.position.y, lens.camera.position.z,
                                    cull.fromLeaf,
                                    worldBounds.min.x, worldBounds.min.y, worldBounds.min.z,
                                    worldBounds.max.x, worldBounds.max.y, worldBounds.max.z,
                                    static_cast<int>(meshCull.leaves.size()));
                                break;
                            }
                        }
                    }
                }
                if (auditNow) {
                    lastCullAudit = GetTime();
                }
            }
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
        renderWorldModel(
            modelEntity,
            model,
            global,
            lens,
            unlit,
            skipMeshes,
            nullptr,
            useModelLighting ? &modelLighting : nullptr,
            meshMask.empty() ? nullptr : &meshMask);
    });
    rlDisableShader();
    if (!world.has<DebugUiState>() || !world.get<DebugUiState>().showSkeletons) {
        return;
    }
    context.animOverlayQuery.each(
        [&](flecs::entity modelEntity, Model3D& model, GlobalTransformation& global, AnimationPlayer& animationPlayer) {
            if (!modelEntity.has<MapLightmapState>()) {
                const BoundingBox localBounds = modelLocalBounds(model);
                const BoundingBox worldBounds = transformAabb(localBounds, global.matrix);
                if (!aabbInFrustum(frustum, worldBounds)) {
                    return;
                }
                if (!pvsCullBox(cull, worldBounds)) {
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
    const PvsCull cull = makePvsCull(world, lens.camera.position);
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
                ++frameCounters().spritesFrustumCulled;
                return;
            }
            if (!pvsCullPoint(cull, position)) {
                ++frameCounters().spritesPvsCulled;
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
                spriteEntity.has<SpriteHiddenParts>() ? spriteEntity.get<SpriteHiddenParts>().mask
                                                       : 0,
            });
        });
}

void collectWorldDepthParticleDrawItems(
    flecs::world& world,
    AssetStore& assets,
    const Camera3D& camera,
    const Frustum& frustum,
    bool unlit,
    std::vector<ParticleDrawItem>& out) {
    out.clear();
    out.reserve(256);
    world.each([&](flecs::entity entity, const ParticleSystemInstance& instance) {
        if (entity.has<ParticleFollowAttachPoint>()) {
            return;
        }
        appendParticleDrawItems(instance, assets, camera, out, &frustum, unlit);
    });
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
        if (!lightmaps.transparentMeshes.empty()) {
            const PvsCull cull = makePvsCull(world, lens.camera.position);
            const Vector3 origin = Vector3Transform(Vector3Zero(), mapGlobal.matrix);
            for (const MapTransparentMesh& mesh : lightmaps.transparentMeshes) {
                if (mesh.meshIndex < 0 || mesh.meshIndex >= mapModel.model.meshCount) {
                    continue;
                }
                TransparentDrawItem item{};
                item.kind = TransparentDrawKind::MapMesh;
                item.mapMeshIndex = mesh.meshIndex;
                item.worldBounds = transformAabb(mesh.bounds, mapGlobal.matrix);
                if (!aabbInFrustum(frustum, item.worldBounds)) {
                    ++frameCounters().transparentFrustumCulled;
                    continue;
                }
                const bool pvsVisible = mesh.leaves.empty()
                    ? pvsCullBox(cull, item.worldBounds)
                    : pvsCullLeaves(cull, mesh.leaves);
                if (!pvsVisible) {
                    ++frameCounters().transparentPvsCulled;
                    continue;
                }
                const Vector3 normal = Vector3Subtract(
                    Vector3Transform(mesh.planeNormal, mapGlobal.matrix), origin);
                const float normalLenSq = Vector3LengthSqr(normal);
                item.facePlane.normal =
                    normalLenSq > 1e-12f ? Vector3Scale(normal, 1.0f / std::sqrt(normalLenSq)) : mesh.planeNormal;
                item.facePlane.d = Vector3DotProduct(
                    item.facePlane.normal, Vector3Transform(mesh.planePoint, mapGlobal.matrix));
                item.viewDepth =
                    boundsCenterViewDepth(item.worldBounds, lens.camera.position, camForward);
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
    collectWorldDepthParticleDrawItems(world, assets, lens.camera, frustum, unlit, particleDrawList);
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

    const auto itemPosition = [](const TransparentDrawItem& item) -> Vector3 {
        switch (item.kind) {
            case TransparentDrawKind::Sprite:
                return translationFromMatrix(item.sprite.global->matrix);
            case TransparentDrawKind::Particle:
                return item.particle.position;
            default:
                return Vector3Zero();
        }
    };
    // A flat face can only occlude something that's on the far side of its plane from the
    // camera. A sprite standing just above a floor-level grate is on the *same* side as the
    // camera looking down at it, so the grate geometrically cannot be between them — no
    // matter how close their forward-axis depths land (which, for a near-level camera, can
    // be an near-exact tie that floating-point rounding resolves the wrong way).
    const auto meshCanOcclude = [](const TransparentDrawItem& mesh, Vector3 cameraPos, Vector3 otherPos) {
        const float sideCamera =
            Vector3DotProduct(mesh.facePlane.normal, cameraPos) - mesh.facePlane.d;
        const float sideOther =
            Vector3DotProduct(mesh.facePlane.normal, otherPos) - mesh.facePlane.d;
        return (sideCamera > 0.0f) != (sideOther > 0.0f);
    };
    // Resolve each mesh's sort depth once, up front, instead of recomputing it inside the
    // comparator against whichever other item it's currently being weighed against. A depth
    // that depends on the specific pairing isn't a fixed property of the item — the same face
    // can come out "farther than sprite A" and "nearer than sprite B" while A also sorts farther
    // than B, a cycle no comparator can resolve consistently. std::sort assumes a strict weak
    // ordering; a cyclic one just makes the result depend on the pivot/partition choices, so the
    // draw order flips based on camera angle and whatever else happens to be in the list — the
    // intermittent misordering seen when a few sprites stand near the same transparent surface.
    struct OccludedCandidate {
        float viewDepth = 0.0f;
        Vector3 position{};
    };
    std::vector<OccludedCandidate> candidates;
    candidates.reserve(drawList.size());
    for (const TransparentDrawItem& item : drawList) {
        if (item.kind != TransparentDrawKind::MapMesh) {
            candidates.push_back({item.viewDepth, itemPosition(item)});
        }
    }
    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const OccludedCandidate& a, const OccludedCandidate& b) { return a.viewDepth < b.viewDepth; });
    for (TransparentDrawItem& mesh : drawList) {
        if (mesh.kind != TransparentDrawKind::MapMesh || candidates.empty()) {
            continue;
        }
        const float minDepth = boundsMinViewDepth(mesh.worldBounds, lens.camera.position, camForward);
        float nearest = mesh.viewDepth;
        auto first = std::lower_bound(
            candidates.begin(),
            candidates.end(),
            minDepth,
            [](const OccludedCandidate& candidate, float depth) { return candidate.viewDepth < depth; });
        for (auto it = first; it != candidates.end() && nearest > minDepth; ++it) {
            if (!meshCanOcclude(mesh, lens.camera.position, it->position)) {
                continue;
            }
            const float candidate = boundsNearestPointViewDepth(
                mesh.worldBounds, it->position, lens.camera.position, camForward);
            if (candidate < nearest) {
                nearest = candidate;
            }
        }
        mesh.viewDepth = nearest;
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
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    BlendMode activeBlend = BLEND_ALPHA;
    BeginBlendMode(activeBlend);

    const ParticleBillboardBasis particleBasis = makeParticleBillboardBasis(lens.camera);
    const Texture2D* particleFilterTex = nullptr;
    bool polygonOffsetActive = false;
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
            if (!polygonOffsetActive) {
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(
                    kTransparentMeshPolygonOffsetFactor,
                    kTransparentMeshPolygonOffsetUnits);
                polygonOffsetActive = true;
            }
            Material& transparentMaterial = mapDrawModel->model.materials[item.mapMeshIndex];
            if (mapDrawLightmaps != nullptr && mapDrawLightmaps->emissionPowerLoc >= 0) {
                SetShaderValue(
                    transparentMaterial.shader,
                    mapDrawLightmaps->emissionPowerLoc,
                    &transparentMaterial.maps[MATERIAL_MAP_SPECULAR].value,
                    SHADER_UNIFORM_FLOAT);
            }
            DrawMesh(
                mapDrawModel->model.meshes[item.mapMeshIndex],
                transparentMaterial,
                MatrixIdentity());
            ++frameCounters().transparentMeshDraws;
            rlPopMatrix();
        } else if (item.kind == TransparentDrawKind::Sprite) {
            if (polygonOffsetActive) {
                rlDrawRenderBatchActive();
                glDisable(GL_POLYGON_OFFSET_FILL);
                polygonOffsetActive = false;
            }
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
                item.sprite.animator,
                item.sprite.hiddenParts);
            ++frameCounters().spritesDrawn;
        } else if (item.kind == TransparentDrawKind::Particle) {
            const ParticleDrawItem& particle = item.particle;
            if (particle.texture == nullptr || particle.texture->id == 0 || particle.size <= 0.0f) {
                continue;
            }
            if (polygonOffsetActive) {
                rlDrawRenderBatchActive();
                glDisable(GL_POLYGON_OFFSET_FILL);
                polygonOffsetActive = false;
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
            drawParticleBillboard(particle, particleBasis, particlePremultipliedTint(particle.color));
        }
    }

    rlSetTexture(0);
    if (particleFilterTex != nullptr) {
        SetTextureFilter(*particleFilterTex, TEXTURE_FILTER_POINT);
    }
    rlDrawRenderBatchActive();
    if (polygonOffsetActive) {
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    EndBlendMode();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    rlEnableDepthMask();

    if (world.has<DebugUiState>()) {
        const DebugUiState& debugUi = world.get<DebugUiState>();
        drawLightProbeDebugOverlays(lighting, debugUi, lens, assets, context.worldSpriteQuery);
        spriteAimStatus = drawSpriteDebugOverlays(
            lens,
            assets,
            debugUi,
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
    }

    if (world.has<DebugUiState>() && world.get<DebugUiState>().showGraphs &&
        world.has<MapGraphs>()) {
        drawGraphDebugOverlays(world.get<MapGraphs>().document);
    }

    if (world.has<DebugUiState>() && world.get<DebugUiState>().showNavPaths) {
        drawNavDebugOverlays(world, world.get<DebugUiState>());
    }

    if (world.has<DebugUiState>() && world.has<MapNavProfiles>()) {
        const DebugUiState& debugUi = world.get<DebugUiState>();
        // A small fixed palette so each toggled-on profile stays a distinct, stable color
        // frame to frame regardless of how many profiles a package declares.
        constexpr Color kPalette[] = {
            Color{60, 220, 140, 255},
            Color{220, 140, 60, 255},
            Color{140, 60, 220, 255},
            Color{60, 160, 220, 255},
            Color{220, 60, 140, 255},
            Color{200, 200, 60, 255},
        };
        const auto& profiles = world.get<MapNavProfiles>().profiles;
        // Sorted so each profile's palette slot (and therefore color) stays the same
        // across map reloads, not dependent on unordered_map's iteration order.
        std::vector<std::string> names;
        names.reserve(profiles.size());
        for (const auto& [name, nav] : profiles) {
            names.push_back(name);
        }
        std::sort(names.begin(), names.end());

        for (std::size_t i = 0; i < names.size(); ++i) {
            const auto shown = debugUi.shownNavProfiles.find(names[i]);
            if (shown == debugUi.shownNavProfiles.end() || !shown->second) {
                continue;
            }
            const Color outlineColor = kPalette[i % (sizeof(kPalette) / sizeof(kPalette[0]))];
            const Color fillColor{outlineColor.r, outlineColor.g, outlineColor.b, 90};
            drawNavPolyDebugOverlays(profiles.at(names[i]), fillColor, outlineColor);
        }
    }

    if (world.has<DebugUiState>() && world.get<DebugUiState>().showActorColliders) {
        drawActorColliderDebugOverlays(world, world.get<DebugUiState>());
    }

    if (world.has<DebugUiState>()) {
        const DebugUiState& audioDebugUi = world.get<DebugUiState>();
        if (audioDebugUi.showAudioSources || audioDebugUi.showAudioOcclusionMesh) {
            drawAudioDebugOverlays(world, audioDebugUi);
        }
    }

    if (world.has<DebugLinePool>()) {
        drawDebugLinePool(world.get<DebugLinePool>());
    }
}

}
