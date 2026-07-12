#pragma once

#include <flecs.h>

namespace slopengine {

class AssetStore;

/** Registers render systems and components on @p world. */
void registerRenderModule(flecs::world& world, AssetStore& assets);

}
