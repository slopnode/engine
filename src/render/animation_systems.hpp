#pragma once

#include <flecs.h>

namespace slopengine {

void registerSpinSystem(flecs::world& world);
void registerSchemeTickSystem(flecs::world& world);
void registerAnimationSystems(flecs::world& world);
void registerAnimationClipFlipTestSystem(flecs::world& world);
void registerTransformSystems(flecs::world& world);

}
