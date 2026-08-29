#pragma once

#include "assets/asset_store.hpp"
#include "render/post_process.hpp"

#include <flecs.h>
#include <raylib.h>

namespace slopengine {

void registerLoadingOverlayModule(flecs::world& world);

/** Blends snapshot into the just-rendered postState.scene texture by mixT and
 *  presents it to the backbuffer. Falls back to a plain scene present if the
 *  crossfade shader failed to load. */
void presentLoadingCrossfade(
    PostProcessState& postState,
    AssetStore& assets,
    Texture2D snapshot,
    float mixT);

}
