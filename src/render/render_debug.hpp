#pragma once

#include "assets/asset_store.hpp"
#include "map/bsp.hpp"
#include "map/graph.hpp"
#include "map/nav_graph.hpp"
#include "map/light_sample.hpp"
#include "render/animation_player.hpp"
#include "render/components.hpp"
#include "render/debug_line_pool.hpp"
#include "ui/ui_state.hpp"

#include <flecs.h>
#include <raylib.h>

#include <cstdint>
#include <string>

namespace slopengine {

void drawSkeletonOverlay(const Model& model, const AnimationPlayer* animationPlayer);
void drawGraphDebugOverlays(const GraphDocument& document);
void drawNavDebugOverlays(flecs::world& world, const DebugUiState& debugUi);
void drawNavPolyDebugOverlays(const MapNavigation& nav, const DebugUiState& debugUi);
void drawBspDebugOverlays(const BspTree& tree, const DebugUiState& debugUi, std::int32_t currentLeaf);
std::string drawSpriteDebugOverlays(
    const Lens& lens,
    AssetStore& assets,
    const DebugUiState& debugUi,
    flecs::query<SpriteInstance, GlobalTransformation>& spriteQuery);
void drawLightProbeDebugOverlays(
    const MapLighting* lighting,
    const DebugUiState& debugUi,
    const Lens& lens,
    AssetStore& assets,
    flecs::query<SpriteInstance, GlobalTransformation>& spriteQuery);

}
