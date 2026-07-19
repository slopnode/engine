#include "map/entity_script.hpp"

#include "assets/skeleton_loader.hpp"
#include "interact/components.hpp"
#include "map/entities_script.hpp"
#include "map/light_components.hpp"
#include "render/components.hpp"
#include "render/sprite_animator.hpp"

#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace slopengine {

namespace {

struct SpawnContext {
    flecs::world* world = nullptr;
    AssetStore* assets = nullptr;
    s7_scheme* scheme = nullptr;
    PlayerStart playerStart{};
    std::unordered_set<std::string> usedIds;
    std::string idPrefix;
    bool inPrefab = false;
    Vector3 prefabAt{};
    Vector3 prefabAngles{};
    std::vector<std::string> nestStack;
};

bool claimId(SpawnContext& ctx, const std::string& id) {
    if (id.empty()) {
        return false;
    }
    if (!ctx.usedIds.insert(id).second) {
        TraceLog(LOG_WARNING, "ENTITY: duplicate id '%s'", id.c_str());
        return false;
    }
    return true;
}

Placement transformPlacement(const SpawnContext& ctx, Placement placement) {
    if (!ctx.inPrefab) {
        return placement;
    }
    if (!ctx.idPrefix.empty()) {
        placement.id = ctx.idPrefix + "/" + placement.id;
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

LocalTransformation makeLocalTransform(const Placement& placement, const SpawnContext& ctx) {
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

bool applyPresentation(flecs::entity entity, const Placement& placement, SpawnContext& ctx) {
    const bool hasSprite = !placement.sprite.empty();
    const bool hasGeo = !placement.geo.empty();
    if (hasSprite == hasGeo) {
        TraceLog(
            LOG_WARNING,
            "ENTITY: '%s' requires exactly one of sprite or geo",
            placement.id.c_str());
        return false;
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
                "ENTITY: missing sprite '%s' for '%s'",
                placement.sprite.c_str(),
                placement.id.c_str());
            return false;
        }

        entity.set<SpriteInstance>({
            .sprite = placement.sprite,
            .frame = placement.frame.empty() ? "A" : placement.frame,
            .facingYaw = facingYaw,
        });

        if (placement.haveAnim) {
            SpriteAnimator animator{};
            animator.animPath = placement.sprite;
            animator.play(placement.animClip, placement.animLoop);
            entity.set<SpriteAnimator>(animator);
        }
        return true;
    }

    if (ctx.assets == nullptr || !ctx.assets->hasGeo(placement.geo)) {
        TraceLog(
            LOG_WARNING,
            "ENTITY: missing geo '%s' for '%s'",
            placement.geo.c_str(),
            placement.id.c_str());
        return false;
    }

    const Model source = ctx.assets->getGeoModel(placement.geo);
    Model model = cloneGeoModelInstance(source);
    if (model.meshCount <= 0) {
        TraceLog(
            LOG_WARNING,
            "ENTITY: failed to load geo '%s' for '%s'",
            placement.geo.c_str(),
            placement.id.c_str());
        return false;
    }

    entity.set<Model3D>({model, WHITE});
    return true;
}

void spawnLight(flecs::entity entity, const Placement& placement, SpawnContext& ctx) {
    entity.add<WorldSpace>().set<LocalTransformation>(makeLocalTransform(placement, ctx));
    switch (placement.kind) {
    case PlacementKind::PointLight:
        entity.set<PointLight>({
            .color = placement.color,
            .intensity = placement.intensity,
            .range = placement.range,
        });
        break;
    case PlacementKind::SpotLight:
        entity.set<SpotLight>({
            .color = placement.color,
            .intensity = placement.intensity,
            .range = placement.range,
            .coneAngle = placement.coneAngle,
        });
        break;
    case PlacementKind::AreaLight:
        entity.set<AreaLight>({
            .color = placement.color,
            .intensity = placement.intensity,
            .size = placement.size,
        });
        break;
    case PlacementKind::Sun:
        entity.set<SunLight>({
            .color = placement.color,
            .intensity = placement.intensity,
        });
        break;
    default:
        break;
    }
}

void spawnOne(SpawnContext& ctx, Placement placement);

void spawnPrefab(SpawnContext& ctx, const Placement& placement) {
    if (ctx.assets == nullptr || ctx.scheme == nullptr) {
        return;
    }
    if (!ctx.assets->hasPrefabEntities(placement.prefabPath)) {
        return;
    }
    if (std::find(ctx.nestStack.begin(), ctx.nestStack.end(), placement.prefabPath) !=
        ctx.nestStack.end()) {
        TraceLog(LOG_WARNING, "ENTITY: prefab cycle detected for '%s'", placement.prefabPath.c_str());
        return;
    }

    auto nested = loadPrefabPlacements(ctx.scheme, *ctx.assets, placement.prefabPath);
    if (!nested) {
        TraceLog(
            LOG_WARNING,
            "ENTITY: failed to load prefab entities '%s'",
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

    for (const Placement& child : nested->placements) {
        spawnOne(ctx, child);
    }

    ctx.nestStack.pop_back();
    ctx.idPrefix = savedPrefix;
    ctx.inPrefab = savedInPrefab;
    ctx.prefabAt = savedAt;
    ctx.prefabAngles = savedAngles;
}

void spawnOne(SpawnContext& ctx, Placement placement) {
    placement = transformPlacement(ctx, std::move(placement));

    if (placement.kind == PlacementKind::PlayerStart) {
        if (!claimId(ctx, placement.id)) {
            return;
        }
        if (ctx.playerStart.found) {
            TraceLog(LOG_WARNING, "ENTITY: ignoring extra player-start '%s'", placement.id.c_str());
            return;
        }
        ctx.playerStart.position = placement.haveAt ? placement.at : ctx.playerStart.position;
        ctx.playerStart.yaw = placement.yaw;
        ctx.playerStart.found = true;
        return;
    }

    if (placement.kind == PlacementKind::Prefab) {
        if (!claimId(ctx, placement.id)) {
            return;
        }
        spawnPrefab(ctx, placement);
        return;
    }

    if (!claimId(ctx, placement.id)) {
        return;
    }

    flecs::entity entity = ctx.world->entity(placement.id.c_str());

    if (placementKindNeedsPresentation(placement.kind)) {
        if (!applyPresentation(entity, placement, ctx)) {
            entity.destruct();
            return;
        }
        if (placement.kind == PlacementKind::Usable) {
            entity.set<Interactable>({
                .prompt = placement.prompt,
                .eventName = placement.onUse,
                .maxDistance = 5.0f,
            });
        }
        return;
    }

    if (placementKindIsLight(placement.kind)) {
        spawnLight(entity, placement, ctx);
        return;
    }

    entity.destruct();
}

} // namespace

void spawnPlacements(
    flecs::world& world,
    AssetStore& assets,
    s7_scheme* scheme,
    const PlacementDocument& doc) {
    SpawnContext ctx{};
    ctx.world = &world;
    ctx.assets = &assets;
    ctx.scheme = scheme;
    for (const Placement& placement : doc.placements) {
        spawnOne(ctx, placement);
    }
}

PlayerStart loadMapEntities(
    s7_scheme* scheme,
    flecs::world& world,
    AssetStore& assets,
    std::string_view mapName) {
    PlayerStart defaults{};
    if (scheme == nullptr) {
        return defaults;
    }

    const std::string virtualPath = std::string(mapName) + "/entities";
    if (!assets.hasMapEntities(virtualPath)) {
        TraceLog(
            LOG_INFO,
            "ENTITY: no entities.s7 for map '%.*s'",
            static_cast<int>(mapName.size()),
            mapName.data());
        return defaults;
    }

    auto doc = loadMapPlacements(scheme, assets, mapName);
    if (!doc) {
        return defaults;
    }

    SpawnContext ctx{};
    ctx.world = &world;
    ctx.assets = &assets;
    ctx.scheme = scheme;
    for (const Placement& placement : doc->placements) {
        spawnOne(ctx, placement);
    }

    if (!ctx.playerStart.found) {
        TraceLog(LOG_WARNING, "ENTITY: no player-start in entities.s7; using default spawn");
        return defaults;
    }
    return ctx.playerStart;
}

}
