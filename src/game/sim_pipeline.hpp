#pragma once

#include <flecs.h>

#include <functional>

namespace slopengine {

struct FramePipelines {
    flecs::entity input;
    flecs::entity sim;
    flecs::entity present;
    flecs::entity cosmetic;
};

void registerSimPipelines(flecs::world& world);
void runFramePipelines(flecs::world& world, float frameDt);
void runSimulationTicks(flecs::world& world, float frameDt);
void runCosmeticTicks(flecs::world& world, float frameDt);
void runFrameWithoutTicks(flecs::world& world, float frameDt, const std::function<void(float)>& between);
void resetSimClock(flecs::world& world);

}
