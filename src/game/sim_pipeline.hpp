#pragma once

#include <flecs.h>

namespace slopengine {

struct FramePipelines {
    flecs::entity input;
    flecs::entity sim;
    flecs::entity present;
};

void registerSimPipelines(flecs::world& world);
void runFramePipelines(flecs::world& world);
void resetSimClock(flecs::world& world);

}
