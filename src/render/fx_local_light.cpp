#include "render/fx_local_light.hpp"

#include "map/bsp.hpp"
#include "map/bsp_ray.hpp"
#include "map/light_sample.hpp"
#include "render/components.hpp"
#include "render/dynamic_light_shadows.hpp"
#include "render/render_frustum.hpp"

#include <flecs.h>
#include <raymath.h>

#include <algorithm>
#include <cmath>

namespace slopengine {

namespace {

constexpr float kLightLosNudge = 0.04f;

Vector3 translationFromMatrix(const Matrix& matrix) {
    return {matrix.m12, matrix.m13, matrix.m14};
}

Vector3 colorToLinear(Color color) {
    return {
        static_cast<float>(color.r) / 255.0f,
        static_cast<float>(color.g) / 255.0f,
        static_cast<float>(color.b) / 255.0f,
    };
}

Color linearToColor(Vector3 linearRgb, unsigned char alpha = 255) {
    return Color{
        static_cast<unsigned char>(std::clamp(static_cast<int>(linearRgb.x * 255.0f), 0, 255)),
        static_cast<unsigned char>(std::clamp(static_cast<int>(linearRgb.y * 255.0f), 0, 255)),
        static_cast<unsigned char>(std::clamp(static_cast<int>(linearRgb.z * 255.0f), 0, 255)),
        alpha,
    };
}

float linearLuminance(Vector3 linearRgb) {
    return 0.2126f * linearRgb.x + 0.7152f * linearRgb.y + 0.0722f * linearRgb.z;
}

Vector3 composeReceiverLighting(Color bakeTint, Vector3 overlay) {
    Vector3 bake = colorToLinear(bakeTint);
    const float overlayPeak = std::max({overlay.x, overlay.y, overlay.z, 0.0f});
    const float bakePeak = std::max({bake.x, bake.y, bake.z, 0.0f});
    if (overlayPeak > bakePeak && overlayPeak > 1e-4f) {
        const float bakeLum = linearLuminance(bake);
        bake = {bakeLum, bakeLum, bakeLum};
    }
    Vector3 lighting{
        bake.x + overlay.x,
        bake.y + overlay.y,
        bake.z + overlay.z,
    };
    const float peak = std::max({lighting.x, lighting.y, lighting.z, 1.0f});
    lighting.x /= peak;
    lighting.y /= peak;
    lighting.z /= peak;
    return lighting;
}

float overlayStrength(Vector3 overlay) {
    return std::max({overlay.x, overlay.y, overlay.z, 0.0f});
}

const QuadBvh* occlusionBvhFromLighting(const MapLighting* lighting) {
    if (lighting == nullptr || !lighting->available || lighting->surfaceBvh.empty()) {
        return nullptr;
    }
    return &lighting->surfaceBvh;
}

FxLightGridCell cellCoords(Vector3 point, float cellSize) {
    const float inv = 1.0f / std::max(cellSize, 1e-4f);
    return {
        static_cast<int>(std::floor(point.x * inv)),
        static_cast<int>(std::floor(point.y * inv)),
        static_cast<int>(std::floor(point.z * inv)),
    };
}

void insertLightIntoGrid(FxLightFrameState& state, int lightIndex, const FxLightEval& light) {
    const float range = std::max(light.range, 0.0f);
    const float cellSize = std::max(state.cellSize, 1e-4f);
    const FxLightGridCell minCell = cellCoords(
        {light.position.x - range, light.position.y - range, light.position.z - range},
        cellSize);
    const FxLightGridCell maxCell = cellCoords(
        {light.position.x + range, light.position.y + range, light.position.z + range},
        cellSize);
    for (int z = minCell.z; z <= maxCell.z; ++z) {
        for (int y = minCell.y; y <= maxCell.y; ++y) {
            for (int x = minCell.x; x <= maxCell.x; ++x) {
                state.grid[{x, y, z}].push_back(lightIndex);
            }
        }
    }
}

RankedDynamicLight toRanked(const FxLightEval& light) {
    RankedDynamicLight ranked{};
    ranked.position = light.position;
    ranked.linearRgb = light.linearRgb;
    ranked.light.kind = DynamicLightKind::Point;
    ranked.light.intensity = light.intensity;
    ranked.light.range = light.range;
    return ranked;
}

} // namespace

Vector3 fxLocalLightLinearRgb(const FxLocalLight& light) {
    DynamicLight tmp{};
    tmp.color = light.color;
    return dynamicLightLinearRgb(tmp);
}

flecs::entity spawnFxLocalLight(
    flecs::world& world,
    const char* name,
    Vector3 position,
    const FxLocalLight& light) {
    flecs::entity entity = name != nullptr && name[0] != '\0' ? world.entity(name) : world.entity();
    LocalTransformation local{};
    local.position = position;
    local.scale = {1.0f, 1.0f, 1.0f};
    local.rotation = QuaternionIdentity();
    Matrix s = MatrixScale(local.scale.x, local.scale.y, local.scale.z);
    Matrix r = QuaternionToMatrix(local.rotation);
    Matrix t = MatrixTranslate(local.position.x, local.position.y, local.position.z);
    GlobalTransformation global{};
    global.matrix = MatrixMultiply(t, MatrixMultiply(r, s));
    entity.add<WorldSpace>()
        .set<LocalTransformation>(local)
        .set<GlobalTransformation>(global)
        .set<FxLocalLight>(light);
    return entity;
}

void clearFxLightFrameState(FxLightFrameState& state) {
    state.lights.clear();
    state.grid.clear();
    state.cellSize = kFxLightGridCellSize;
}

void buildFxLightFrameState(
    flecs::world& world,
    const Frustum* frustum,
    bool unlit,
    FxLightFrameState& out) {
    clearFxLightFrameState(out);
    if (unlit) {
        return;
    }

    world.each([&](flecs::entity entity, const FxLocalLight& light, const GlobalTransformation& global) {
        (void)entity;
        if (light.intensity <= 0.0f) {
            return;
        }
        FxLightEval eval{};
        eval.position = translationFromMatrix(global.matrix);
        eval.linearRgb = fxLocalLightLinearRgb(light);
        eval.intensity = light.intensity;
        eval.range = std::max(light.range, 0.0f);
        if (frustum != nullptr &&
            !sphereInFrustum(*frustum, eval.position, eval.range)) {
            return;
        }
        const int index = static_cast<int>(out.lights.size());
        out.lights.push_back(eval);
        insertLightIntoGrid(out, index, eval);
    });
}

bool lightSegmentOccluded(const QuadBvh* bvh, Vector3 lightPos, Vector3 point) {
    if (bvh == nullptr || bvh->empty()) {
        return false;
    }
    Vector3 delta = Vector3Subtract(point, lightPos);
    const float distSq = Vector3DotProduct(delta, delta);
    if (distSq < 1e-8f) {
        return false;
    }
    const float dist = std::sqrt(distSq);
    const Vector3 dir = Vector3Scale(delta, 1.0f / dist);
    const float nudge = std::min(kLightLosNudge, dist * 0.45f);
    const Vector3 from = Vector3Add(lightPos, Vector3Scale(dir, nudge));
    const Vector3 to = Vector3Subtract(point, Vector3Scale(dir, nudge));
    return bspSegmentOccluded(*bvh, from, to);
}

Vector3 evaluateOverlayLightsAtPoint(
    const std::vector<RankedDynamicLight>* dynLights,
    const FxLightFrameState* fxLights,
    Vector3 point,
    Vector3 normal,
    const QuadBvh* occlusionBvh) {
    Vector3 total{};
    if (dynLights != nullptr && !dynLights->empty()) {
        for (const RankedDynamicLight& light : *dynLights) {
            if (lightSegmentOccluded(occlusionBvh, light.position, point)) {
                continue;
            }
            const Vector3 dyn = evaluateDynamicLightAtPoint(light, point, normal);
            total.x += dyn.x;
            total.y += dyn.y;
            total.z += dyn.z;
        }
    }
    if (fxLights != nullptr && !fxLights->lights.empty()) {
        const Vector3 fx =
            evaluateFxLightsAtPoint(*fxLights, point, normal, occlusionBvh);
        total.x += fx.x;
        total.y += fx.y;
        total.z += fx.z;
    }
    return total;
}

Color addLinearRgbToColor(Color base, Vector3 linearRgb) {
    return Color{
        static_cast<unsigned char>(std::clamp(
            static_cast<int>(base.r) + static_cast<int>(linearRgb.x * 255.0f),
            0,
            255)),
        static_cast<unsigned char>(std::clamp(
            static_cast<int>(base.g) + static_cast<int>(linearRgb.y * 255.0f),
            0,
            255)),
        static_cast<unsigned char>(std::clamp(
            static_cast<int>(base.b) + static_cast<int>(linearRgb.z * 255.0f),
            0,
            255)),
        base.a,
    };
}

void storeFxLightFrameState(flecs::world& world, FxLightFrameState state) {
    if (world.has<FxLightFrameState>()) {
        world.set<FxLightFrameState>(std::move(state));
    }
}

Color sampleReceiverTintAtOrigin(
    flecs::world& world,
    Vector3 origin,
    bool unlit,
    bool includeFxLights) {
    if (unlit) {
        return WHITE;
    }

    Color tint = WHITE;
    if (world.has<MapLighting>()) {
        const MapLighting& lighting = world.get<MapLighting>();
        tint = lighting.ambient;
        if (lighting.available) {
            if (auto sample =
                    sampleMapLight(lighting, origin, {0.0f, -1.0f, 0.0f}, 2.0f)) {
                tint = *sample;
            }
        }
    }

    const std::vector<RankedDynamicLight>* dynLights =
        world.has<DynamicLightFrameState>() ? &world.get<DynamicLightFrameState>().lights
                                            : nullptr;
    const FxLightFrameState* fxLights = includeFxLights && world.has<FxLightFrameState>()
        ? &world.get<FxLightFrameState>()
        : nullptr;
    const MapLighting* lighting =
        world.has<MapLighting>() ? &world.get<MapLighting>() : nullptr;
    const Vector3 overlay = evaluateOverlayLightsAtPoint(
        dynLights,
        fxLights,
        origin,
        {0.0f, 1.0f, 0.0f},
        occlusionBvhFromLighting(lighting));
    return linearToColor(composeReceiverLighting(tint, overlay));
}

Color sampleReceiverTintColor(flecs::world& world, Vector3 origin, bool unlit) {
    return sampleReceiverTintAtOrigin(world, origin, unlit, true);
}

namespace {

void appendModelTintSamplePoints(
    const Model& model,
    const Matrix& globalMatrix,
    Vector3* outSamples,
    int maxSamples,
    int& inoutCount) {
    if (outSamples == nullptr || inoutCount >= maxSamples) {
        return;
    }
    const BoundingBox localBounds = GetModelBoundingBox(model);
    const BoundingBox worldBounds = transformAabb(localBounds, globalMatrix);
    const Vector3 center{
        (worldBounds.min.x + worldBounds.max.x) * 0.5f,
        (worldBounds.min.y + worldBounds.max.y) * 0.5f,
        (worldBounds.min.z + worldBounds.max.z) * 0.5f,
    };
    const float extentY = std::max(0.0f, worldBounds.max.y - worldBounds.min.y);
    const float insetY = std::min(0.15f, extentY * 0.25f);
    const Vector3 points[3] = {
        center,
        {center.x, worldBounds.min.y + insetY, center.z},
        {center.x, worldBounds.max.y - insetY, center.z},
    };
    for (const Vector3& point : points) {
        if (inoutCount >= maxSamples) {
            break;
        }
        outSamples[inoutCount++] = point;
    }
}

void collectModelTintSamplePoints(
    const Model& model,
    const Matrix& globalMatrix,
    const Matrix* secondaryMatrix,
    Vector3 outSamples[6],
    int& outCount) {
    outCount = 0;
    appendModelTintSamplePoints(model, globalMatrix, outSamples, 6, outCount);
    if (secondaryMatrix != nullptr) {
        appendModelTintSamplePoints(model, *secondaryMatrix, outSamples, 6, outCount);
    }
}

Color sampleBakeTintAtOrigin(flecs::world& world, Vector3 origin, bool unlit) {
    if (unlit) {
        return WHITE;
    }
    Color tint = WHITE;
    if (world.has<MapLighting>()) {
        const MapLighting& lighting = world.get<MapLighting>();
        tint = lighting.ambient;
        if (lighting.available) {
            if (auto sample =
                    sampleMapLight(lighting, origin, {0.0f, -1.0f, 0.0f}, 2.0f)) {
                tint = *sample;
            }
        }
    }
    return tint;
}

} // namespace

Color sampleReceiverTintColorForModel(
    flecs::world& world,
    const Model& model,
    const Matrix& globalMatrix,
    bool unlit,
    const Matrix* secondaryMatrix) {
    if (unlit) {
        return WHITE;
    }

    Vector3 samples[6]{};
    int sampleCount = 0;
    collectModelTintSamplePoints(model, globalMatrix, secondaryMatrix, samples, sampleCount);
    if (sampleCount <= 0) {
        return WHITE;
    }

    const std::vector<RankedDynamicLight>* dynLights =
        world.has<DynamicLightFrameState>() ? &world.get<DynamicLightFrameState>().lights
                                            : nullptr;
    const MapLighting* lighting =
        world.has<MapLighting>() ? &world.get<MapLighting>() : nullptr;
    const QuadBvh* occlusionBvh = occlusionBvhFromLighting(lighting);

    Color best = sampleReceiverTintAtOrigin(world, samples[0], false, false);
    float bestStrength = overlayStrength(evaluateOverlayLightsAtPoint(
        dynLights,
        nullptr,
        samples[0],
        {0.0f, 1.0f, 0.0f},
        occlusionBvh));
    float bestLum = linearLuminance(colorToLinear(best));
    for (int i = 1; i < sampleCount; ++i) {
        const float strength = overlayStrength(evaluateOverlayLightsAtPoint(
            dynLights,
            nullptr,
            samples[i],
            {0.0f, 1.0f, 0.0f},
            occlusionBvh));
        const Color candidate = sampleReceiverTintAtOrigin(world, samples[i], false, false);
        const float lum = linearLuminance(colorToLinear(candidate));
        if (strength > bestStrength + 1e-6f ||
            (std::fabs(strength - bestStrength) <= 1e-6f && lum > bestLum)) {
            best = candidate;
            bestStrength = strength;
            bestLum = lum;
        }
    }
    return best;
}

Color sampleBakeTintColorForModel(
    flecs::world& world,
    const Model& model,
    const Matrix& globalMatrix,
    bool unlit,
    const Matrix* secondaryMatrix) {
    if (unlit) {
        return WHITE;
    }

    Vector3 samples[6]{};
    int sampleCount = 0;
    collectModelTintSamplePoints(model, globalMatrix, secondaryMatrix, samples, sampleCount);
    if (sampleCount <= 0) {
        return WHITE;
    }

    Color best = sampleBakeTintAtOrigin(world, samples[0], false);
    float bestLum = linearLuminance(colorToLinear(best));
    for (int i = 1; i < sampleCount; ++i) {
        const Color candidate = sampleBakeTintAtOrigin(world, samples[i], false);
        const float lum = linearLuminance(colorToLinear(candidate));
        if (lum > bestLum) {
            best = candidate;
            bestLum = lum;
        }
    }
    return best;
}

Vector3 evaluateFxLightsAtPoint(
    const FxLightFrameState& state,
    Vector3 point,
    Vector3 normal,
    const QuadBvh* occlusionBvh,
    int maxLights) {
    if (state.lights.empty() || maxLights <= 0) {
        return {};
    }

    const auto cellIt = state.grid.find(cellCoords(point, state.cellSize));
    if (cellIt == state.grid.end() || cellIt->second.empty()) {
        return {};
    }

    struct Candidate {
        int index = 0;
        float distSq = 0.0f;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(cellIt->second.size());
    for (int index : cellIt->second) {
        if (index < 0 || index >= static_cast<int>(state.lights.size())) {
            continue;
        }
        const FxLightEval& light = state.lights[static_cast<std::size_t>(index)];
        const Vector3 delta = Vector3Subtract(light.position, point);
        const float distSq = Vector3DotProduct(delta, delta);
        const float range = std::max(light.range, 1e-4f);
        if (distSq > range * range) {
            continue;
        }
        if (lightSegmentOccluded(occlusionBvh, light.position, point)) {
            continue;
        }
        candidates.push_back({index, distSq});
    }

    if (candidates.empty()) {
        return {};
    }

    const int keep = std::min(maxLights, static_cast<int>(candidates.size()));
    if (static_cast<int>(candidates.size()) > keep) {
        std::partial_sort(
            candidates.begin(),
            candidates.begin() + keep,
            candidates.end(),
            [](const Candidate& a, const Candidate& b) {
                return a.distSq < b.distSq;
            });
        candidates.resize(static_cast<std::size_t>(keep));
    }

    Vector3 total{};
    for (const Candidate& candidate : candidates) {
        const RankedDynamicLight ranked =
            toRanked(state.lights[static_cast<std::size_t>(candidate.index)]);
        const Vector3 contrib = evaluateDynamicLightAtPoint(ranked, point, normal);
        total.x += contrib.x;
        total.y += contrib.y;
        total.z += contrib.z;
    }
    return total;
}

}
