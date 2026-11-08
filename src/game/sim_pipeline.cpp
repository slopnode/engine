#include "game/sim_pipeline.hpp"

#include "core/log.hpp"
#include "core/sim_clock.hpp"
#include "game/game_state.hpp"
#include "input/input_state.hpp"

#include <algorithm>
#include <string>

namespace slopengine {

namespace {

flecs::pipeline_builder<> systemPipeline(flecs::world& world) {
    auto builder = world.pipeline();
    builder.with(flecs::System)
        .with(flecs::Phase).cascade(flecs::DependsOn)
        .without(flecs::Disabled).up(flecs::DependsOn)
        .without(flecs::Disabled).up(flecs::ChildOf);
    return builder;
}

enum class PipelineKind {
    Input,
    Sim,
    Present,
};

bool inPipeline(flecs::entity system, PipelineKind kind) {
    const bool sim = system.has<SimSystem>();
    switch (kind) {
    case PipelineKind::Input:
        return !sim && !system.has<CosmeticSystem>() && system.has(flecs::DependsOn, flecs::PreUpdate);
    case PipelineKind::Sim:
        return sim;
    case PipelineKind::Present:
        return !sim && !system.has<CosmeticSystem>() && !system.has(flecs::DependsOn, flecs::PreUpdate);
    }
    return false;
}

void logPipelineMembers(flecs::world& world, const char* label, PipelineKind kind) {
    std::string names;
    world.query_builder().with(flecs::System).build().each([&](flecs::entity system) {
        if (inPipeline(system, kind) && system.name()) {
            names += names.empty() ? "" : ", ";
            names += system.name().c_str();
        }
    });
    Log::debug(LogCategory::General, "PIPELINE: %s: %s", label, names.c_str());
}

} // namespace

void registerSimPipelines(flecs::world& world) {
    world.component<SimSystem>();
    world.component<SimClock>();
    world.set<SimClock>({});

    FramePipelines pipelines{};
    world.component<CosmeticSystem>();
    world.component<CosmeticClock>();
    world.set<CosmeticClock>({});
    pipelines.input = systemPipeline(world)
                          .without<SimSystem>()
                          .without<CosmeticSystem>()
                          .with(flecs::DependsOn, flecs::PreUpdate)
                          .build()
                          .set_name("InputPipeline");
    pipelines.sim = systemPipeline(world).with<SimSystem>().build().set_name("SimPipeline");
    pipelines.present = systemPipeline(world)
                            .without<SimSystem>()
                            .without<CosmeticSystem>()
                            .without(flecs::DependsOn, flecs::PreUpdate)
                            .build()
                            .set_name("PresentPipeline");
    pipelines.cosmetic = systemPipeline(world).with<CosmeticSystem>().build().set_name("CosmeticPipeline");
    world.set_pipeline(pipelines.present);
    world.component<FramePipelines>();
    world.set<FramePipelines>(pipelines);
}

void runFramePipelines(flecs::world& world, float frameDt) {
    const FramePipelines pipelines = world.get<FramePipelines>();
    static bool logged = false;
    if (!logged) {
        logPipelineMembers(world, "per frame, before ticks", PipelineKind::Input);
        logPipelineMembers(world, "per tick", PipelineKind::Sim);
        logPipelineMembers(world, "per frame, after ticks", PipelineKind::Present);
        logged = true;
    }

    world.run_pipeline(pipelines.input, frameDt);
    runSimulationTicks(world, frameDt);
    world.progress(frameDt);
}

void runSimulationTicks(flecs::world& world, float frameDt) {
    const flecs::entity sim = world.get<FramePipelines>().sim;
    if (isSimulationPaused(world)) {
        world.get_mut<InputState>().clearPendingPresses();
        return;
    }
    const int ticks = advanceSimClock(world.get_mut<SimClock>(), frameDt);
    for (int i = 0; i < ticks; ++i) {
        world.get_mut<SimClock>().tick += 1;
        world.run_pipeline(sim, kSimTickDt);
    }
}

void runCosmeticTicks(flecs::world& world, float frameDt) {
    if (!world.has<CosmeticClock>() || !world.has<FramePipelines>() || isSimulationPaused(world)) {
        return;
    }
    CosmeticClock& clock = world.get_mut<CosmeticClock>();
    clock.accumulator = std::min(clock.accumulator + std::max(0.0f, frameDt), kSimTickDt * kMaxSimTicksPerFrame);
    const flecs::entity cosmetic = world.get<FramePipelines>().cosmetic;
    while (clock.accumulator >= kSimTickDt) {
        clock.accumulator -= kSimTickDt;
        world.run_pipeline(cosmetic, kSimTickDt);
    }
}

void runFrameWithoutTicks(flecs::world& world, float frameDt, const std::function<void(float)>& between) {
    world.run_pipeline(world.get<FramePipelines>().input, frameDt);
    between(frameDt);
    world.progress(frameDt);
}

void resetSimClock(flecs::world& world) {
    resetSimAccumulator(world.get_mut<SimClock>());
    if (world.has<InputState>()) {
        world.get_mut<InputState>().clearPendingPresses();
    }
}

}
