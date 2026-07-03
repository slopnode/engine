#pragma once

#include <flecs.h>

namespace daggerlike {

class AssetStore;

void registerRenderModule(flecs::world& world, AssetStore& assets);

}
