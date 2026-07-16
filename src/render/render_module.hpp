#pragma once

#include "assets/asset_store.hpp"
#include "game/app_config.hpp"

#include <flecs.h>

struct s7_scheme;

namespace slopengine {

/** Registers render systems and components on @p world. */
void registerRenderModule(
    flecs::world& world,
    AssetStore& assets,
    const AppConfig& config,
    s7_scheme* scheme);

}
