#pragma once

#include "assets/asset_store.hpp"

#include <flecs.h>

struct s7_scheme;

namespace slopengine {

void registerScriptBoot(flecs::world& world, AssetStore& assets, s7_scheme* scheme);

}
