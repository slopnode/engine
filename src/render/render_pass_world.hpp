#pragma once

#include "render/dynamic_light.hpp"
#include "render/dynamic_light_shadows.hpp"
#include "render/render_context.hpp"
#include "render/render_frustum.hpp"
#include "render/components.hpp"

#include <flecs.h>
#include <raylib.h>

#include <string>
#include <unordered_set>
#include <vector>

namespace slopengine {

void renderWorldModel(
    flecs::entity entity,
    Model3D& model,
    GlobalTransformation& globalTransform,
    const Lens& lens,
    bool unlit = false,
    const std::unordered_set<int>* skipMeshIndices = nullptr,
    const std::unordered_set<int>* onlyMeshIndices = nullptr);

std::vector<RankedDynamicLight> gatherDynamicLights(
    flecs::world& world,
    const Lens& lens,
    const Lens& presentLens,
    const Frustum& frustum,
    bool unlit,
    bool enableDynamicLights = true,
    int maxShadowed = kMaxShadowedDynamicLights);

void storeDynamicLightFrameState(
    flecs::world& world,
    const std::vector<RankedDynamicLight>& rankedLights);

void uploadMapDynamicLights(
    flecs::world& world,
    const std::vector<RankedDynamicLight>& rankedLights,
    bool unlit,
    const DynamicLightShadowState* shadowState = nullptr);

void drawWorldModels(
    flecs::world& world,
    RenderContext& context,
    const Lens& lens,
    const Frustum& frustum,
    bool unlit);

std::string drawWorldTransparentPass(
    flecs::world& world,
    RenderContext& context,
    const Lens& lens,
    const Frustum& frustum,
    bool unlit);

std::string drawWorldSprites(
    flecs::world& world,
    RenderContext& context,
    const Lens& lens,
    const Frustum& frustum,
    bool unlit);

void drawWorldDebugOverlays(flecs::world& world);

}
