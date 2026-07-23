#pragma once

#include "render/dynamic_light.hpp"
#include "render/render_context.hpp"
#include "render/components.hpp"

#include <flecs.h>
#include <raylib.h>

#include <string>
#include <vector>

namespace slopengine {

void renderWorldModel(
    flecs::entity entity,
    Model3D& model,
    GlobalTransformation& globalTransform,
    const Lens& lens);

std::vector<RankedDynamicLight> gatherDynamicLights(
    flecs::world& world,
    const Lens& lens,
    const Lens& presentLens,
    bool unlit);

void storeDynamicLightFrameState(
    flecs::world& world,
    const std::vector<RankedDynamicLight>& rankedLights);

void drawWorldModels(
    flecs::world& world,
    RenderContext& context,
    const Lens& lens,
    const std::vector<RankedDynamicLight>& rankedLights,
    bool unlit);

std::string drawWorldSprites(
    flecs::world& world,
    RenderContext& context,
    const Lens& lens,
    bool unlit);

void drawWorldDebugOverlays(flecs::world& world);

}
