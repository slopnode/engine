#pragma once

#include <flecs.h>

namespace slopengine {

void registerNavModule(flecs::world& world);
void replanNavigationAgent(flecs::world& world, flecs::entity entity);
void resetNavFlowFieldCache(flecs::world& world);

}
