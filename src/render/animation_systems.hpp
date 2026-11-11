#pragma once

#include <flecs.h>

namespace slopengine {

struct AnimationPlayer;

void registerSpinSystem(flecs::world& world);
void registerSchemeTickSystem(flecs::world& world);
void registerAnimationSystems(flecs::world& world);
void registerTransformSystems(flecs::world& world);
void sampleTickPose(flecs::entity entity);
float animationClipDuration(flecs::world world, const AnimationPlayer& player);

}
