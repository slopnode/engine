#include "map/things_spawn.hpp"

#include "assets/asset_store.hpp"
#include "assets/geo_loader.hpp"
#include "assets/material_loader.hpp"
#include "assets/skeleton_loader.hpp"
#include "assets/sprite_anim_loader.hpp"
#include "audio/components.hpp"
#include "interact/components.hpp"
#include "map/bsp.hpp"
#include "map/csg_compile.hpp"
#include "map/brush_door.hpp"
#include "map/mover_brushes.hpp"
#include "map/things_script.hpp"
#include "map/light_components.hpp"
#include "render/dynamic_light.hpp"
#include "render/skybox.hpp"
#include "navigation/nav_components.hpp"
#include "physics/components.hpp"
#include "physics/physics_module.hpp"
#include "physics/rigid_mover.hpp"
#include "physics/sight_components.hpp"
#include "physics/trigger_components.hpp"
#include "render/components.hpp"
#include "render/material_anim.hpp"
#include "render/sprite_animator.hpp"
#include "particles/particle_module.hpp"
#include "particles/components.hpp"

#include <cstdint>

#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace slopengine {

namespace {

struct SpawnContext {
    flecs::world* world = nullptr;
    AssetStore* assets = nullptr;
    s7_scheme* scheme = nullptr;
    const std::vector<Brush>* brushes = nullptr;
    const RadFile* rad = nullptr;
    std::unordered_set<std::string> chartFaceIds;
    const std::vector<Texture2D>* lightmapAtlases = nullptr;
    bool hasLightmaps = false;
    PlayerStart playerStart{};
    std::unordered_set<std::string> usedIds;
    std::string idPrefix;
    bool inPrefab = false;
    Vector3 prefabAt{};
    Vector3 prefabAngles{};
    std::vector<std::string> nestStack;
};

MaterialUvInfo resolveThingMaterialUv(AssetStore& assets, std::string_view materialPath) {
    MaterialUvInfo info{};
    const MaterialAsset* asset = assets.getMaterialAsset(materialPath);
    if (asset != nullptr) {
        info.pixelsPerMeter = asset->pixelsPerMeter;
        if (!asset->albedoTexture.empty()) {
            const Texture2D texture = assets.getTexture(asset->albedoTexture);
            if (texture.id != 0 && texture.width > 0 && texture.height > 0) {
                info.textureWidth = static_cast<float>(texture.width);
                info.textureHeight = static_cast<float>(texture.height);
            }
        } else if (!asset->textureAnimPath.empty()) {
            const Texture2D texture = assets.resolveTextureAnimFrame(asset->textureAnimPath, "default", 0);
            if (texture.id != 0 && texture.width > 0 && texture.height > 0) {
                info.textureWidth = static_cast<float>(texture.width);
                info.textureHeight = static_cast<float>(texture.height);
            }
        }
    }
    return info;
}

bool applyMoverBrushDefaults(Thing& placement, SpawnContext& ctx) {
    if (placement.brush.empty()) {
        return true;
    }
    if (ctx.brushes == nullptr) {
        TraceLog(
            LOG_WARNING,
            "THING: mover '%s' brush '%s' but no map brushes available",
            placement.id.c_str(),
            placement.brush.c_str());
        return false;
    }
    const Brush* brush = findBrushById(*ctx.brushes, placement.brush);
    if (brush == nullptr) {
        TraceLog(
            LOG_WARNING,
            "THING: mover '%s' missing brush '%s'",
            placement.id.c_str(),
            placement.brush.c_str());
        return false;
    }
    if (brush->role != BrushRole::Detail) {
        TraceLog(
            LOG_WARNING,
            "THING: mover '%s' brush '%s' must be detail (got %s)",
            placement.id.c_str(),
            placement.brush.c_str(),
            brushRoleName(brush->role));
        return false;
    }

    const Vector3 center{
        0.5f * (brush->mins.x + brush->maxs.x),
        0.5f * (brush->mins.y + brush->maxs.y),
        0.5f * (brush->mins.z + brush->maxs.z),
    };
    if (!placement.haveAt) {
        placement.at = center;
        placement.haveAt = true;
    }
    if (!placement.haveMoverCollideSize) {
        placement.moverCollideSize = {
            brush->maxs.x - brush->mins.x,
            brush->maxs.y - brush->mins.y,
            brush->maxs.z - brush->mins.z,
        };
        if (placement.moverCollideSize.x < 1e-4f) {
            placement.moverCollideSize.x = 0.1f;
        }
        if (placement.moverCollideSize.y < 1e-4f) {
            placement.moverCollideSize.y = 0.1f;
        }
        if (placement.moverCollideSize.z < 1e-4f) {
            placement.moverCollideSize.z = 0.1f;
        }
        placement.haveMoverCollideSize = true;
    }
    return true;
}

bool claimId(SpawnContext& ctx, const std::string& id) {
    if (id.empty()) {
        return false;
    }
    if (!ctx.usedIds.insert(id).second) {
        TraceLog(LOG_WARNING, "THING: duplicate id '%s'", id.c_str());
        return false;
    }
    return true;
}

Thing transformThing(const SpawnContext& ctx, Thing placement) {
    if (!ctx.inPrefab) {
        return placement;
    }
    if (!ctx.idPrefix.empty()) {
        placement.id = ctx.idPrefix + "/" + placement.id;
        if (!placement.brush.empty()) {
            placement.brush = ctx.idPrefix + "/" + placement.brush;
        }
    }
    if (placement.haveAt) {
        const Matrix rotation = MatrixRotateXYZ(ctx.prefabAngles);
        placement.at = Vector3Add(Vector3Transform(placement.at, rotation), ctx.prefabAt);
    }
    if (placement.haveAngles) {
        placement.angles = {
            ctx.prefabAngles.x + placement.angles.x,
            ctx.prefabAngles.y + placement.angles.y,
            ctx.prefabAngles.z + placement.angles.z,
        };
        placement.yaw = placement.angles.y;
    } else {
        placement.yaw += ctx.prefabAngles.y;
    }
    return placement;
}

LocalTransformation makeLocalTransform(const Thing& placement, const SpawnContext& ctx) {
    LocalTransformation local{};
    local.position = placement.haveAt ? placement.at : Vector3{0.0f, 0.0f, 0.0f};
    local.scale = {1.0f, 1.0f, 1.0f};
    if (placement.haveAngles) {
        local.rotation = QuaternionFromEuler(
            placement.angles.x,
            placement.angles.y,
            placement.angles.z);
    } else {
        local.rotation = QuaternionFromAxisAngle({0.0f, 1.0f, 0.0f}, placement.yaw);
    }
    if (ctx.inPrefab && !placement.haveAngles) {
        const Quaternion instanceRotation = QuaternionFromEuler(
            ctx.prefabAngles.x,
            ctx.prefabAngles.y,
            ctx.prefabAngles.z);
        local.rotation = QuaternionMultiply(instanceRotation, local.rotation);
    }
    return local;
}

/** True if any of this brush's own faces has a baked lightmap chart, i.e. the map has been
 *  rebaked with sloprad since this brush was authored. */
bool brushHasLightmapChart(const Brush& brush, const SpawnContext& ctx) {
    if (!ctx.hasLightmaps || ctx.rad == nullptr || ctx.lightmapAtlases == nullptr) {
        return false;
    }
    for (const BrushFace& face : brush.faces) {
        if (ctx.chartFaceIds.count(face.id) != 0) {
            return true;
        }
    }
    return false;
}

/** Binds each mesh's baked atlas texture (per its lightmap chart); mirrors the static-mesh
 *  atlas binding in loadAndCompileMap. Does not touch material.shader — renderWorldModel
 *  already swaps any lightmap-less prop's shader to the shared map lightmap shader per frame. */
void bindBakedLightmapAtlases(Model& model, const CsgCompileResult& compiled, const SpawnContext& ctx) {
    std::unordered_map<std::string, std::int32_t> faceAtlasById;
    for (const LightmapChart& chart : ctx.rad->charts) {
        faceAtlasById[chart.faceId] = chart.atlasIndex;
    }
    for (int meshIndex = 0; meshIndex < model.meshCount; ++meshIndex) {
        const std::string& faceId = compiled.asset.primitives[static_cast<std::size_t>(meshIndex)].name;
        std::int32_t atlasIndex = 0;
        const auto atlasIt = faceAtlasById.find(faceId);
        if (atlasIt != faceAtlasById.end()) {
            atlasIndex = atlasIt->second;
        }
        if (atlasIndex >= 0 && atlasIndex < static_cast<std::int32_t>(ctx.lightmapAtlases->size())) {
            const Texture2D lightmap = (*ctx.lightmapAtlases)[static_cast<std::size_t>(atlasIndex)];
            if (lightmap.id != 0) {
                SetMaterialTexture(&model.materials[meshIndex], MATERIAL_MAP_METALNESS, lightmap);
            }
        }
    }
}

bool applyBrushPresentation(flecs::entity entity, const Thing& placement, SpawnContext& ctx) {
    if (ctx.brushes == nullptr || ctx.assets == nullptr) {
        return false;
    }
    const Brush* source = findBrushById(*ctx.brushes, placement.brush);
    if (source == nullptr) {
        return false;
    }

    const Vector3 origin = placement.haveAt ? placement.at : Vector3{};

    const auto resolveUv = [assets = ctx.assets](std::string_view materialPath) {
        return resolveThingMaterialUv(*assets, materialPath);
    };

    const bool baked = brushHasLightmapChart(*source, ctx);
    CsgCompileResult compiled =
        compileBrushesToGeo({*source}, resolveUv, baked ? ctx.rad : nullptr);
    for (Vector3& pos : compiled.buffer.positions) {
        pos = Vector3Subtract(pos, origin);
    }
    Model model = buildModelFromGeo(
        compiled.asset,
        compiled.buffer,
        [assets = ctx.assets](std::string_view path) { return assets->resolveMaterial(path); });
    if (model.meshCount <= 0) {
        TraceLog(
            LOG_WARNING,
            "THING: failed to compile brush '%s' for mover '%s'",
            placement.brush.c_str(),
            placement.id.c_str());
        return false;
    }

    if (baked) {
        bindBakedLightmapAtlases(model, compiled, ctx);
    }

    entity.add<WorldSpace>().set<LocalTransformation>(makeLocalTransform(placement, ctx));
    entity.set<Model3D>({model, WHITE, true});
    if (baked) {
        entity.add<BakedLightmapModel>();
    }
    attachMaterialAnimTargetsFromGeo(entity, compiled.asset, *ctx.assets);
    return true;
}

bool applyPresentation(flecs::entity entity, const Thing& placement, SpawnContext& ctx) {
    const bool hasSprite = !placement.sprite.empty();
    const bool hasGeo = !placement.geo.empty();
    const bool hasBrush = !placement.brush.empty();
    const int presentationCount =
        (hasSprite ? 1 : 0) + (hasGeo ? 1 : 0) + (hasBrush ? 1 : 0);
    if (presentationCount != 1) {
        TraceLog(
            LOG_WARNING,
            "THING: '%s' requires exactly one of sprite, geo, or brush",
            placement.id.c_str());
        return false;
    }

    if (hasBrush) {
        return applyBrushPresentation(entity, placement, ctx);
    }

    float facingYaw = placement.yaw;
    if (ctx.inPrefab && !placement.haveAngles) {
        facingYaw = placement.yaw;
    }

    entity.add<WorldSpace>().set<LocalTransformation>(makeLocalTransform(placement, ctx));

    if (hasSprite) {
        if (ctx.assets == nullptr || !ctx.assets->hasSprite(placement.sprite)) {
            TraceLog(
                LOG_WARNING,
                "THING: missing sprite '%s' for '%s'",
                placement.sprite.c_str(),
                placement.id.c_str());
            return false;
        }

        SpriteInstance sprite{
            .sprite = placement.sprite,
            .frame = placement.frame.empty() ? "A" : placement.frame,
            .facingYaw = facingYaw,
        };
        if (placement.haveAnim) {
            SpriteAnimator animator{};
            animator.animPath = placement.sprite;
            const SpriteAnimBank* bank =
                ctx.assets != nullptr ? ctx.assets->getSpriteAnimBank(placement.sprite)
                                      : nullptr;
            playSpriteAnim(animator, sprite, bank, placement.animClip, placement.animLoop);
            entity.set<SpriteInstance>(sprite);
            entity.set<SpriteAnimator>(animator);
        } else {
            entity.set<SpriteInstance>(sprite);
        }
        return true;
    }

    if (ctx.assets == nullptr || !ctx.assets->hasGeo(placement.geo)) {
        TraceLog(
            LOG_WARNING,
            "THING: missing geo '%s' for '%s'",
            placement.geo.c_str(),
            placement.id.c_str());
        return false;
    }

    const Model source = ctx.assets->getGeoModel(placement.geo);
    Model model = cloneGeoModelInstance(source);
    if (model.meshCount <= 0) {
        TraceLog(
            LOG_WARNING,
            "THING: failed to load geo '%s' for '%s'",
            placement.geo.c_str(),
            placement.id.c_str());
        return false;
    }

    entity.set<Model3D>({model, WHITE});

    GeoAsset geoAsset{};
    if (parseGeoAsset(ctx.assets->getGeoSource(placement.geo), geoAsset)) {
        attachMaterialAnimTargetsFromGeo(entity, geoAsset, *ctx.assets);
    }
    return true;
}

void spawnLight(flecs::entity entity, const Thing& placement, SpawnContext& ctx) {
    entity.add<WorldSpace>().set<LocalTransformation>(makeLocalTransform(placement, ctx));
    switch (placement.kind) {
    case ThingKind::PointLight:
        entity.set<PointLight>({
            .color = placement.color,
            .intensity = placement.intensity,
            .range = placement.range,
        });
        break;
    case ThingKind::SpotLight:
        entity.set<SpotLight>({
            .color = placement.color,
            .intensity = placement.intensity,
            .range = placement.range,
            .coneAngle = placement.coneAngle,
        });
        break;
    case ThingKind::AreaLight:
        entity.set<AreaLight>({
            .color = placement.color,
            .intensity = placement.intensity,
            .size = placement.size,
        });
        break;
    case ThingKind::Sun:
        entity.set<SunLight>({
            .color = placement.color,
            .intensity = placement.intensity,
        });
        break;
    case ThingKind::AmbientLight:
        entity.set<AmbientLight>({
            .color = placement.color,
            .intensity = placement.intensity,
        });
        break;
    case ThingKind::DynamicPointLight: {
        DynamicLight light{};
        light.kind = DynamicLightKind::Point;
        setDynamicLightRgb(light, placement.color);
        light.intensity = placement.intensity;
        light.range = placement.range;
        light.castShadows = placement.dynamicCastShadows;
        entity.set<DynamicLight>(light);
        break;
    }
    case ThingKind::DynamicSpotLight: {
        DynamicLight light{};
        light.kind = DynamicLightKind::Spot;
        setDynamicLightRgb(light, placement.color);
        light.intensity = placement.intensity;
        light.range = placement.range;
        light.coneAngle = placement.coneAngle;
        light.castShadows = placement.dynamicCastShadows;
        entity.set<DynamicLight>(light);
        break;
    }
    default:
        break;
    }
}

bool thingHasTrigger(const Thing& placement) {
    return placement.kind == ThingKind::Trigger || !placement.onEnter.empty() ||
        !placement.onExit.empty();
}

void applyTriggerVolume(flecs::entity entity, const Thing& placement) {
    TriggerVolume volume{};
    volume.size = placement.haveTriggerSize ? placement.triggerSize : Vector3{1.0f, 1.0f, 1.0f};
    volume.onEnter = placement.onEnter;
    volume.onExit = placement.onExit;
    volume.filterTags = placement.collideTags;
    volume.once = placement.triggerOnce;
    entity.set<TriggerVolume>(std::move(volume));
}

void spawnOne(SpawnContext& ctx, Thing placement);

void spawnPrefab(SpawnContext& ctx, const Thing& placement) {
    if (ctx.assets == nullptr || ctx.scheme == nullptr) {
        return;
    }
    if (!ctx.assets->hasPrefabThings(placement.prefabPath)) {
        return;
    }
    if (std::find(ctx.nestStack.begin(), ctx.nestStack.end(), placement.prefabPath) !=
        ctx.nestStack.end()) {
        TraceLog(LOG_WARNING, "THING: prefab cycle detected for '%s'", placement.prefabPath.c_str());
        return;
    }

    auto nested = loadPrefabThings(ctx.scheme, *ctx.assets, placement.prefabPath);
    if (!nested) {
        TraceLog(
            LOG_WARNING,
            "THING: failed to load prefab things '%s'",
            placement.prefabPath.c_str());
        return;
    }

    const std::string savedPrefix = ctx.idPrefix;
    const bool savedInPrefab = ctx.inPrefab;
    const Vector3 savedAt = ctx.prefabAt;
    const Vector3 savedAngles = ctx.prefabAngles;

    std::string instancePrefix = placement.id;
    if (!savedPrefix.empty()) {
        instancePrefix = savedPrefix + "/" + placement.id;
    }

    Vector3 worldAt = placement.haveAt ? placement.at : Vector3{};
    Vector3 worldAngles = placement.haveAngles ? placement.angles
                                               : Vector3{0.0f, placement.yaw, 0.0f};
    if (savedInPrefab) {
        const Matrix parentRotation = MatrixRotateXYZ(savedAngles);
        worldAt = Vector3Add(Vector3Transform(worldAt, parentRotation), savedAt);
        worldAngles = {
            savedAngles.x + worldAngles.x,
            savedAngles.y + worldAngles.y,
            savedAngles.z + worldAngles.z,
        };
    }

    ctx.idPrefix = instancePrefix;
    ctx.inPrefab = true;
    ctx.prefabAt = worldAt;
    ctx.prefabAngles = worldAngles;
    ctx.nestStack.push_back(placement.prefabPath);

    for (const Thing& child : nested->things) {
        spawnOne(ctx, child);
    }

    ctx.nestStack.pop_back();
    ctx.idPrefix = savedPrefix;
    ctx.inPrefab = savedInPrefab;
    ctx.prefabAt = savedAt;
    ctx.prefabAngles = savedAngles;
}

void spawnOne(SpawnContext& ctx, Thing placement) {
    placement = transformThing(ctx, std::move(placement));

    if (placement.kind == ThingKind::PlayerStart) {
        if (!claimId(ctx, placement.id)) {
            return;
        }
        if (ctx.playerStart.found) {
            TraceLog(LOG_WARNING, "THING: ignoring extra player-start '%s'", placement.id.c_str());
            return;
        }
        ctx.playerStart.position = placement.haveAt ? placement.at : ctx.playerStart.position;
        ctx.playerStart.yaw = placement.yaw;
        if (placement.havePitch) {
            ctx.playerStart.pitch = placement.pitch;
        } else if (placement.haveAngles) {
            ctx.playerStart.pitch = placement.angles.x;
        }
        if (placement.haveMotor) {
            ctx.playerStart.haveMotor = true;
            ctx.playerStart.motorRadius = placement.motorRadius;
            ctx.playerStart.motorHeight = placement.motorHeight;
            ctx.playerStart.motorEyeHeight = placement.motorEyeHeight;
        }
        ctx.playerStart.found = true;
        return;
    }

    if (placement.kind == ThingKind::Prefab) {
        if (!claimId(ctx, placement.id)) {
            return;
        }
        spawnPrefab(ctx, placement);
        return;
    }

    if (!claimId(ctx, placement.id)) {
        return;
    }

    if (placement.kind == ThingKind::Mover && !applyMoverBrushDefaults(placement, ctx)) {
        return;
    }

    flecs::entity entity = ctx.world->entity(placement.id.c_str());
    entity.add<MapOwned>();
    if (!placement.type.empty()) {
        entity.set<ThingTypeRef>(ThingTypeRef{placement.type});
    }

    if (thingKindNeedsPresentation(placement.kind)) {
        if (!applyPresentation(entity, placement, ctx)) {
            entity.destruct();
            return;
        }
        if (placement.kind == ThingKind::Usable) {
            entity.set<Interactable>({
                .prompt = placement.prompt,
                .onUse = placement.onUse,
                .maxDistance = 5.0f,
            });
        }
        if (placement.kind == ThingKind::Pickup && !placement.onUse.empty()) {
            entity.set<Interactable>({
                .prompt = {},
                .onUse = placement.onUse,
                .maxDistance = 5.0f,
            });
        }
        if (placement.kind == ThingKind::Mover) {
            RigidMover mover{};
            if (entity.has<LocalTransformation>()) {
                const LocalTransformation& local = entity.get<LocalTransformation>();
                mover.closedPos = local.position;
                mover.closedRot = local.rotation;
            }
            if (placement.haveMoverPivot) {
                mover.pivotLocal = placement.moverPivot;
            }
            if (placement.haveMoverOpenOffset) {
                mover.openPosOffset = placement.moverOpenOffset;
            }
            if (placement.haveMoverOpenAngle) {
                mover.openAngleRadians = placement.moverOpenAngle;
                switch (placement.moverRotAxis) {
                case 0:
                    mover.rotAxis = MoverRotAxis::Pitch;
                    break;
                case 2:
                    mover.rotAxis = MoverRotAxis::Roll;
                    break;
                case 1:
                default:
                    mover.rotAxis = MoverRotAxis::Yaw;
                    break;
                }
            }
            if (placement.haveMoverDuration) {
                mover.duration = placement.moverDuration > 0.0f ? placement.moverDuration : 0.8f;
            }
            if (placement.haveMoverAutoClose) {
                mover.autoClose = std::max(0.0f, placement.moverAutoClose);
            }
            if (placement.haveMoverCollideSize) {
                mover.collideHalfExtents = {
                    placement.moverCollideSize.x * 0.5f,
                    placement.moverCollideSize.y * 0.5f,
                    placement.moverCollideSize.z * 0.5f,
                };
            }
            if (placement.haveMoverCollideCenter) {
                mover.collideCenterLocal = placement.moverCollideCenter;
            } else if (!placement.brush.empty()) {
                mover.collideCenterLocal = {0.0f, 0.0f, 0.0f};
            } else {
                mover.collideCenterLocal = {
                    0.0f,
                    mover.collideHalfExtents.y,
                    0.0f,
                };
            }
            if (placement.moverBlockMode == "crush") {
                mover.blockMode = MoverBlockMode::Crush;
            } else {
                mover.blockMode = MoverBlockMode::Shove;
            }
            if (placement.moverPush == "horizontal") {
                mover.pushMode = MoverPushMode::Horizontal;
            } else if (placement.moverPush == "off") {
                mover.pushMode = MoverPushMode::Off;
            } else {
                mover.pushMode = MoverPushMode::Full;
            }
            mover.slide = placement.haveMoverSlide ? placement.moverSlide : true;
            mover.onCrush = placement.onCrush;
            mover.groupId = placement.moverGroup;
            mover.openSound = placement.moverOpenSound;
            mover.closeSound = placement.moverCloseSound;
            mover.soundVolume = placement.haveMoverSoundVolume ? placement.moverSoundVolume : 1.0f;
            entity.set<RigidMover>(mover);
            if (entity.has<LocalTransformation>() && placement.haveMoverCollideSize &&
                !placement.geo.empty()) {
                entity.get_mut<LocalTransformation>().scale = placement.moverCollideSize;
            }

            if (ctx.world->has<PhysicsContext>()) {
                PhysicsWorld* physics = ctx.world->get_mut<PhysicsContext>().world;
                if (physics != nullptr) {
                    Vector3 pos{};
                    Quaternion rot{};
                    computeMoverPose(mover, 0.0f, pos, rot);
                    physics->createKinematicBox(
                        static_cast<std::uint64_t>(entity.id()),
                        moverCollideWorldCenter(pos, rot, mover),
                        mover.collideHalfExtents,
                        rot,
                        mover.slide);
                    entity.get_mut<RigidMover>().kinematicReady = true;
                }
            }

            if (placement.havePrompt || !placement.onUse.empty()) {
                entity.set<Interactable>({
                    .prompt = placement.prompt,
                    .onUse = placement.onUse,
                    .maxDistance = 5.0f,
                });
            }
        }
        if (placement.kind == ThingKind::Actor) {
            CharacterMotor motor{};
            motor.radius = placement.motorRadius;
            motor.height = placement.motorHeight;
            motor.moveSpeed = placement.motorSpeed;
            motor.gravity = placement.motorGravity;
            motor.stepHeight = placement.motorStepHeight;
            motor.verticalSpeed = placement.motorVerticalSpeed;
            motor.hoverHeight = placement.motorHoverHeight;
            motor.hull = placement.motorHull;
            motor.moveMode = placement.motorMoveMode;
            NavigationAgent navAgent{};
            navAgent.flyer = motor.moveMode == CharacterMoveMode::Fly;
            navAgent.maxFall = placement.motorMaxFall;
            navAgent.waterCostMultiplier = placement.motorWaterAversion;
            entity.add<Actor>().set<CharacterMotor>(motor).set<NavigationAgent>(navAgent);
            std::vector<std::string> tags = placement.tags;
            if (tags.empty()) {
                tags.push_back("actor");
            }
            entity.set<CollisionTags>(CollisionTags{std::move(tags)});
            if (placement.haveSight) {
                ActorSight sight{};
                sight.enabled = placement.sightEnabled;
                sight.range = placement.sightRange;
                sight.fovDegrees = placement.sightFovDegrees;
                sight.eyeLift = placement.sightEyeLift;
                sight.seeTags = placement.sightSeeTags;
                sight.ignoreTags = placement.sightIgnoreTags;
                sight.filterProc = placement.sightFilterProc;
                entity.set<ActorSight>(std::move(sight));
            }
            if (ctx.world->has<PhysicsContext>()) {
                PhysicsWorld* physics = ctx.world->get_mut<PhysicsContext>().world;
                if (physics != nullptr) {
                    const Vector3 feet = placement.haveAt ? placement.at : Vector3{0.0f, 0.0f, 0.0f};
                    physics->createCharacter(
                        static_cast<std::uint64_t>(entity.id()),
                        feet.x,
                        feet.y,
                        feet.z,
                        motor);
                }
            }
        }
        if (thingHasTrigger(placement)) {
            applyTriggerVolume(entity, placement);
        }
        return;
    }

    if (placement.kind == ThingKind::Trigger) {
        entity.add<WorldSpace>().set<LocalTransformation>(makeLocalTransform(placement, ctx));
        applyTriggerVolume(entity, placement);
        return;
    }

    if (thingKindIsLight(placement.kind)) {
        spawnLight(entity, placement, ctx);
        return;
    }

    if (placement.kind == ThingKind::Skybox) {
        entity.set<SkyboxSettings>(
            skyboxSettingsFromThing(placement, ctx.assets));
        return;
    }

    if (placement.kind == ThingKind::SoundSource) {
        entity.add<WorldSpace>().set<LocalTransformation>(makeLocalTransform(placement, ctx));
        entity.set<AudioSource>({
            .audio = placement.audio,
            .clip = placement.clip,
            .volume = placement.volume,
            .minDistance = placement.minDistance,
            .maxDistance = placement.maxDistance,
            .looping = placement.looping,
            .spatial = placement.spatial,
            .autoplay = true,
            .playing = false,
            .voice = 0,
        });
        return;
    }

    if (placement.kind == ThingKind::Marker) {
        entity.add<WorldSpace>().set<LocalTransformation>(makeLocalTransform(placement, ctx));
        return;
    }

    if (placement.kind == ThingKind::Particle) {
        if (ctx.assets == nullptr || placement.particleSystem.empty()) {
            entity.destruct();
            return;
        }
        ParticleSystemInstance instance{};
        if (!initParticleSystemInstance(
                instance,
                *ctx.assets,
                placement.particleSystem,
                placement.particlePlay)) {
            TraceLog(
                LOG_WARNING,
                "THING: particle '%s' missing system '%s'",
                placement.id.c_str(),
                placement.particleSystem.c_str());
            entity.destruct();
            return;
        }
        LocalTransformation local = makeLocalTransform(placement, ctx);
        Matrix s = MatrixScale(local.scale.x, local.scale.y, local.scale.z);
        Matrix r = QuaternionToMatrix(local.rotation);
        Matrix t = MatrixTranslate(local.position.x, local.position.y, local.position.z);
        GlobalTransformation global{};
        global.matrix = MatrixMultiply(s, MatrixMultiply(r, t));
        entity.add<WorldSpace>()
            .set<LocalTransformation>(local)
            .set<GlobalTransformation>(global)
            .set<ParticleSystemInstance>(std::move(instance));
        return;
    }

    entity.destruct();
}

void spawnDoorBrush(
    SpawnContext& ctx,
    const Brush& brush,
    const std::unordered_set<std::string>& moverClaims,
    const ThingDocument* things) {
    if (brush.role != BrushRole::Door || brush.id.empty()) {
        return;
    }
    if (moverClaims.count(brush.id) != 0) {
        TraceLog(
            LOG_ERROR,
            "DOOR: skip brush '%s' — claimed by a mover",
            brush.id.c_str());
        return;
    }
    if (!claimId(ctx, brush.id)) {
        return;
    }

    Thing placement{};
    placement.kind = ThingKind::Mover;
    placement.id = brush.id;
    placement.brush = brush.id;
    placement.at = {
        0.5f * (brush.mins.x + brush.maxs.x),
        0.5f * (brush.mins.y + brush.maxs.y),
        0.5f * (brush.mins.z + brush.maxs.z),
    };
    placement.haveAt = true;

    flecs::entity entity = ctx.world->entity(placement.id.c_str());
    entity.add<MapOwned>();
    if (!applyBrushPresentation(entity, placement, ctx)) {
        entity.destruct();
        return;
    }

    RigidMover mover{};
    if (entity.has<LocalTransformation>()) {
        const LocalTransformation& local = entity.get<LocalTransformation>();
        mover.closedPos = local.position;
        mover.closedRot = local.rotation;
    }
    configureBrushDoorMover(mover, brush, brush.door, placement.at, things);
    entity.set<RigidMover>(mover);

    if (ctx.world->has<PhysicsContext>()) {
        PhysicsWorld* physics = ctx.world->get_mut<PhysicsContext>().world;
        if (physics != nullptr) {
            Vector3 pos{};
            Quaternion rot{};
            computeMoverPose(mover, 0.0f, pos, rot);
            physics->createKinematicBox(
                static_cast<std::uint64_t>(entity.id()),
                moverCollideWorldCenter(pos, rot, mover),
                mover.collideHalfExtents,
                rot,
                mover.slide);
            entity.get_mut<RigidMover>().kinematicReady = true;
        }
    }

    entity.set<Interactable>({
        .prompt = {},
        .onUse = {},
        .canUse = brush.door.canUse,
        .engineToggle = true,
        .maxDistance = 5.0f,
    });
}

void spawnDoorBrushes(
    SpawnContext& ctx,
    const std::vector<Brush>& brushes,
    const ThingDocument* things) {
    const std::unordered_set<std::string> moverClaims =
        things != nullptr ? collectMoverBrushIds(*things) : std::unordered_set<std::string>{};
    for (const Brush& brush : brushes) {
        spawnDoorBrush(ctx, brush, moverClaims, things);
    }
}

} // namespace

void spawnThings(
    flecs::world& world,
    AssetStore& assets,
    s7_scheme* scheme,
    const ThingDocument& doc,
    const std::vector<Brush>* brushes) {
    SpawnContext ctx{};
    ctx.world = &world;
    ctx.assets = &assets;
    ctx.scheme = scheme;
    ctx.brushes = brushes;
    for (const Thing& placement : doc.things) {
        spawnOne(ctx, placement);
    }
    if (brushes != nullptr) {
        spawnDoorBrushes(ctx, *brushes, &doc);
    }
}

PlayerStart spawnMapThings(
    s7_scheme* scheme,
    flecs::world& world,
    AssetStore& assets,
    std::string_view mapName,
    const std::vector<Brush>& brushes,
    const RadFile* rad,
    const std::vector<Texture2D>* lightmapAtlases,
    bool hasLightmaps) {
    PlayerStart defaults{};
    if (scheme == nullptr) {
        return defaults;
    }

    SpawnContext ctx{};
    ctx.world = &world;
    ctx.assets = &assets;
    ctx.scheme = scheme;
    ctx.brushes = &brushes;
    ctx.rad = rad;
    if (rad != nullptr) {
        for (const LightmapChart& chart : rad->charts) {
            if (!chart.faceId.empty()) {
                ctx.chartFaceIds.insert(chart.faceId);
            }
        }
    }
    ctx.lightmapAtlases = lightmapAtlases;
    ctx.hasLightmaps = hasLightmaps;

    std::optional<ThingDocument> doc;
    const std::string virtualPath = std::string(mapName) + "/things";
    if (assets.hasMapThings(virtualPath)) {
        doc = loadMapThings(scheme, assets, mapName);
        if (doc) {
            for (const Thing& placement : doc->things) {
                spawnOne(ctx, placement);
            }
        }
    } else {
        TraceLog(
            LOG_INFO,
            "THING: no things.s7 for map '%.*s'",
            static_cast<int>(mapName.size()),
            mapName.data());
    }

    spawnDoorBrushes(ctx, brushes, doc ? &*doc : nullptr);

    if (!ctx.playerStart.found) {
        TraceLog(LOG_WARNING, "THING: no player-start in things.s7; using default spawn");
        return defaults;
    }
    return ctx.playerStart;
}

}
