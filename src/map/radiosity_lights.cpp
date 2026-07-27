#include "map/radiosity_lights.hpp"

#include "map/things_script.hpp"
#include "map/thing.hpp"

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
    RadiosityThingLights* out = nullptr;
    std::string idPrefix;
    bool inPrefab = false;
    Vector3 prefabAt{};
    Vector3 prefabAngles{};
    std::vector<std::string> nestStack;
    bool haveSun = false;
};

Vector3 lightForward(const Thing& placement, const CollectContext& ctx) {
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

Thing transformThing(const CollectContext& ctx, Thing placement) {
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

void collectOne(CollectContext& ctx, Thing placement);

void collectPrefab(CollectContext& ctx, const Thing& placement) {
    if (ctx.assets == nullptr || ctx.scheme == nullptr) {
        return;
    }
    if (!ctx.assets->hasPrefabThings(placement.prefabPath)) {
        return;
    }
    if (std::find(ctx.nestStack.begin(), ctx.nestStack.end(), placement.prefabPath) !=
        ctx.nestStack.end()) {
        TraceLog(LOG_WARNING, "sloprad: prefab cycle detected for '%s'", placement.prefabPath.c_str());
        return;
    }

    auto nested = loadPrefabThings(ctx.scheme, *ctx.assets, placement.prefabPath);
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

    for (const Thing& child : nested->things) {
        collectOne(ctx, child);
    }

    ctx.nestStack.pop_back();
    ctx.idPrefix = savedPrefix;
    ctx.inPrefab = savedInPrefab;
    ctx.prefabAt = savedAt;
    ctx.prefabAngles = savedAngles;
}

void collectOne(CollectContext& ctx, Thing placement) {
    placement = transformThing(ctx, std::move(placement));

    if (placement.kind == ThingKind::Prefab) {
        collectPrefab(ctx, placement);
        return;
    }

    if (ctx.out == nullptr) {
        return;
    }

    if (placement.kind == ThingKind::AmbientLight) {
        if (ctx.out->hasAmbient) {
            TraceLog(
                LOG_WARNING,
                "sloprad: ignoring extra ambient-light '%s'",
                placement.id.c_str());
            return;
        }
        ctx.out->hasAmbient = true;
        ctx.out->ambient = {
            placement.color.x * placement.intensity,
            placement.color.y * placement.intensity,
            placement.color.z * placement.intensity,
        };
        return;
    }

    if (placement.kind == ThingKind::Sun) {
        if (ctx.haveSun) {
            TraceLog(LOG_WARNING, "sloprad: ignoring extra sun '%s'", placement.id.c_str());
            return;
        }
        RadiosityLight light{};
        light.kind = RadiosityLightKind::Sun;
        light.direction = lightForward(placement, ctx);
        light.color = placement.color;
        light.intensity = placement.intensity;
        if (placement.haveAt) {
            light.position = placement.at;
        }
        ctx.out->lights.push_back(light);
        ctx.haveSun = true;
        return;
    }

    if (placement.kind != ThingKind::PointLight && placement.kind != ThingKind::SpotLight) {
        return;
    }
    if (!placement.haveAt) {
        return;
    }

    RadiosityLight light{};
    light.kind = placement.kind == ThingKind::SpotLight ? RadiosityLightKind::Spot
                                                            : RadiosityLightKind::Point;
    light.position = placement.at;
    light.direction = lightForward(placement, ctx);
    light.color = placement.color;
    light.intensity = placement.intensity;
    light.range = placement.range;
    light.coneAngle = placement.coneAngle;
    ctx.out->lights.push_back(light);
}

} // namespace

RadiosityThingLights collectRadiosityLights(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName) {
    RadiosityThingLights result{};
    auto doc = loadMapThings(scheme, assets, mapName);
    if (!doc) {
        return result;
    }

    CollectContext ctx{};
    ctx.assets = &assets;
    ctx.scheme = scheme;
    ctx.out = &result;
    for (const Thing& placement : doc->things) {
        collectOne(ctx, placement);
    }
    return result;
}

}
