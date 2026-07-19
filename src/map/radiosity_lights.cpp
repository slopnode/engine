#include "map/radiosity_lights.hpp"

#include "map/entities_script.hpp"
#include "map/placement.hpp"

#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace slopengine {

namespace {

struct CollectContext {
    AssetStore* assets = nullptr;
    s7_scheme* scheme = nullptr;
    std::vector<RadiosityLight>* out = nullptr;
    std::string idPrefix;
    bool inPrefab = false;
    Vector3 prefabAt{};
    Vector3 prefabAngles{};
    std::vector<std::string> nestStack;
};

Vector3 lightForward(const Placement& placement, const CollectContext& ctx) {
    Quaternion rotation{};
    if (placement.haveAngles) {
        rotation = QuaternionFromEuler(
            placement.angles.x,
            placement.angles.y,
            placement.angles.z);
    } else {
        rotation = QuaternionFromAxisAngle({0.0f, 1.0f, 0.0f}, placement.yaw);
        if (ctx.inPrefab) {
            const Quaternion instanceRotation = QuaternionFromEuler(
                ctx.prefabAngles.x,
                ctx.prefabAngles.y,
                ctx.prefabAngles.z);
            rotation = QuaternionMultiply(instanceRotation, rotation);
        }
    }
    return Vector3Normalize(Vector3RotateByQuaternion({0.0f, 0.0f, 1.0f}, rotation));
}

Placement transformPlacement(const CollectContext& ctx, Placement placement) {
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

void collectOne(CollectContext& ctx, Placement placement);

void collectPrefab(CollectContext& ctx, const Placement& placement) {
    if (ctx.assets == nullptr || ctx.scheme == nullptr) {
        return;
    }
    if (!ctx.assets->hasPrefabEntities(placement.prefabPath)) {
        return;
    }
    if (std::find(ctx.nestStack.begin(), ctx.nestStack.end(), placement.prefabPath) !=
        ctx.nestStack.end()) {
        TraceLog(LOG_WARNING, "sloprad: prefab cycle detected for '%s'", placement.prefabPath.c_str());
        return;
    }

    auto nested = loadPrefabPlacements(ctx.scheme, *ctx.assets, placement.prefabPath);
    if (!nested) {
        TraceLog(
            LOG_WARNING,
            "sloprad: failed to load prefab entities '%s'",
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
        collectOne(ctx, child);
    }

    ctx.nestStack.pop_back();
    ctx.idPrefix = savedPrefix;
    ctx.inPrefab = savedInPrefab;
    ctx.prefabAt = savedAt;
    ctx.prefabAngles = savedAngles;
}

void collectOne(CollectContext& ctx, Placement placement) {
    placement = transformPlacement(ctx, std::move(placement));

    if (placement.kind == PlacementKind::Prefab) {
        collectPrefab(ctx, placement);
        return;
    }

    if (placement.kind != PlacementKind::PointLight && placement.kind != PlacementKind::SpotLight) {
        return;
    }
    if (!placement.haveAt || ctx.out == nullptr) {
        return;
    }

    RadiosityLight light{};
    light.kind = placement.kind == PlacementKind::SpotLight ? RadiosityLightKind::Spot
                                                            : RadiosityLightKind::Point;
    light.position = placement.at;
    light.direction = lightForward(placement, ctx);
    light.color = placement.color;
    light.intensity = placement.intensity;
    light.range = placement.range;
    light.coneAngle = placement.coneAngle;
    ctx.out->push_back(light);
}

} // namespace

std::vector<RadiosityLight> collectRadiosityLights(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName) {
    std::vector<RadiosityLight> lights;
    auto doc = loadMapPlacements(scheme, assets, mapName);
    if (!doc) {
        return lights;
    }

    CollectContext ctx{};
    ctx.assets = &assets;
    ctx.scheme = scheme;
    ctx.out = &lights;
    for (const Placement& placement : doc->placements) {
        collectOne(ctx, placement);
    }
    return lights;
}

}
