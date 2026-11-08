#include "test_assert.hpp"

#include "core/sim_clock.hpp"
#include "game/sim_interpolation.hpp"
#include "game/sim_pipeline.hpp"
#include "input/input_state.hpp"
#include "input/player_command.hpp"
#include "physics/components.hpp"
#include "render/animation_player.hpp"

#include <flecs.h>

#include <cmath>
#include <string>

namespace slopengine {

void runSimClockTests() {
    {
        SimClock clock{};
        CHECK_EQ(advanceSimClock(clock, kSimTickDt * 0.5f), 0);
        CHECK_EQ(advanceSimClock(clock, kSimTickDt * 0.5f), 1);
        CHECK(clock.accumulator < kSimTickDt);
    }
    {
        SimClock clock{};
        CHECK_EQ(advanceSimClock(clock, kSimTickDt * 2.5f), 2);
        CHECK(std::fabs(clock.accumulator - kSimTickDt * 0.5f) < 1.0e-4f);
    }
    {
        SimClock clock{};
        CHECK_EQ(advanceSimClock(clock, 10.0f), kMaxSimTicksPerFrame);
        CHECK(clock.accumulator < kSimTickDt);
        CHECK_EQ(advanceSimClock(clock, 0.0f), 0);
        CHECK_EQ(advanceSimClock(clock, -1.0f), 0);
    }
    {
        SimClock clock{};
        advanceSimClock(clock, kSimTickDt * 0.9f);
        resetSimAccumulator(clock);
        CHECK_EQ(advanceSimClock(clock, kSimTickDt * 0.9f), 0);
    }
    {
        SimClock clock{};
        int ticks = 0;
        for (int frame = 0; frame < 144; ++frame) {
            ticks += advanceSimClock(clock, 1.0f / 144.0f);
        }
        CHECK(ticks >= 59 && ticks <= 60);
    }

    {
        InputState input{};
        input.resize(3);
        InputCommand device{};
        device.moveForward = 1.0f;
        PlayerCommand command{};

        input.actionPressed[1] = 1;
        input.actionDown[1] = 1;
        input.latchPresses();
        input.actionPressed[1] = 0;
        input.latchPresses();
        buildPlayerCommand(command, input, device, 0.5f, -0.25f, true, 7);
        CHECK(command.pressed(1));
        CHECK(command.down(1));
        CHECK_FALSE(command.pressed(0));
        CHECK(command.tick == 7);
        CHECK(command.moveForward == 1.0f);
        CHECK(command.yaw == 0.5f && command.pitch == -0.25f);
        buildPlayerCommand(command, input, device, 0.5f, -0.25f, true, 8);
        CHECK_FALSE(command.pressed(1));

        input.actionPressed[2] = 1;
        input.latchPresses();
        buildPlayerCommand(command, input, device, 0.0f, 0.0f, false, 9);
        CHECK_FALSE(command.pressed(2));
        CHECK_FALSE(command.down(1));
        CHECK(command.moveForward == 0.0f);
        buildPlayerCommand(command, input, device, 0.0f, 0.0f, true, 10);
        CHECK_FALSE(command.pressed(2));
        CHECK_FALSE(command.pressed(7));
    }

    {
        SimClock clock{};
        CHECK(simBlendAlpha(clock) == 0.0f);
        clock.accumulator = kSimTickDt * 0.25f;
        CHECK(std::fabs(simBlendAlpha(clock) - 0.25f) < 1.0e-4f);
        clock.accumulator = kSimTickDt * 3.0f;
        CHECK(simBlendAlpha(clock) == 1.0f);
    }

    {
        SimPose pose{};
        pose.prev.position = {0.0f, 0.0f, 0.0f};
        pose.cur.position = {2.0f, 4.0f, -2.0f};
        const LocalTransformation half = blendSimPose(pose, 0.5f);
        CHECK(std::fabs(half.position.x - 1.0f) < 1.0e-5f);
        CHECK(std::fabs(half.position.y - 2.0f) < 1.0e-5f);
        CHECK(std::fabs(half.position.z + 1.0f) < 1.0e-5f);
        CHECK(sameSimPose(blendSimPose(pose, 1.0f), pose.cur, true));
        LocalTransformation turned = pose.cur;
        turned.rotation = {0.0f, 0.7071068f, 0.0f, 0.7071068f};
        CHECK_FALSE(sameSimPose(turned, pose.cur, true));
        CHECK(sameSimPose(turned, pose.cur, false));
    }

    {
        CHECK(std::fabs(blendAnimationTime(0.2f, 0.4f, 1.0f, false, 0.5f) - 0.3f) < 1.0e-5f);
        CHECK(std::fabs(blendAnimationTime(0.9f, 0.1f, 1.0f, true, 0.5f)) < 1.0e-5f);
        CHECK(std::fabs(blendAnimationTime(0.9f, 0.1f, 1.0f, true, 0.25f) - 0.95f) < 1.0e-5f);
        AnimationPlayer player{};
        player.time = 0.7f;
        player.prevTime = 0.6f;
        player.play("walk");
        CHECK(player.time == 0.0f && player.prevTime == 0.0f);
    }

    {
        flecs::world world;
        registerSimPipelines(world);
        std::string order;
        world.system("FrameEarly").kind(flecs::PreUpdate).run([&](flecs::iter&) { order += "A"; });
        world.system("SimEarly").kind(flecs::PreUpdate).run([&](flecs::iter&) { order += "B"; }).add<SimSystem>();
        world.system("SimLate").kind(flecs::OnUpdate).run([&](flecs::iter&) { order += "C"; }).add<SimSystem>();
        world.system("FrameLate").kind(flecs::OnUpdate).run([&](flecs::iter&) { order += "D"; });
        world.system("FrameDraw").kind(flecs::PostUpdate).run([&](flecs::iter&) { order += "E"; });

        const FramePipelines& pipelines = world.get<FramePipelines>();
        world.run_pipeline(pipelines.input, kSimTickDt);
        world.run_pipeline(pipelines.sim, kSimTickDt);
        world.run_pipeline(pipelines.sim, kSimTickDt);
        world.progress(kSimTickDt);
        CHECK_EQ(order, std::string("ABCBCDE"));
    }

    {
        flecs::world world;
        registerSimPipelines(world);
        registerSimInterpolation(world);
        world.system("MoveActors")
            .kind(flecs::OnUpdate)
            .each([](Actor, LocalTransformation& local) { local.position.x += 1.0f; })
            .add<SimSystem>();
        float seenInTick = -1.0f;
        world.system("ReadActors")
            .kind(flecs::OnUpdate)
            .each([&](Actor, const LocalTransformation& local) { seenInTick = local.position.x; })
            .add<SimSystem>();

        const flecs::entity actor = world.entity().add<Actor>().set<LocalTransformation>({}).set<GlobalTransformation>({});
        const FramePipelines& pipelines = world.get<FramePipelines>();
        world.run_pipeline(pipelines.sim, kSimTickDt);
        world.run_pipeline(pipelines.sim, kSimTickDt);
        CHECK(actor.has<SimPose>());
        CHECK(std::fabs(actor.get<LocalTransformation>().position.x - 2.0f) < 1.0e-5f);

        world.get_mut<SimClock>().accumulator = kSimTickDt * 0.5f;
        world.progress(kSimTickDt);
        CHECK(std::fabs(actor.get<LocalTransformation>().position.x - 1.5f) < 1.0e-5f);

        world.run_pipeline(pipelines.sim, kSimTickDt);
        CHECK(std::fabs(seenInTick - 3.0f) < 1.0e-5f);

        world.progress(kSimTickDt);
        actor.get_mut<LocalTransformation>().position.x = 10.0f;
        world.run_pipeline(pipelines.sim, kSimTickDt);
        CHECK(std::fabs(seenInTick - 11.0f) < 1.0e-5f);
    }

    {
        flecs::world world;
        registerSimPipelines(world);
        world.set<InputState>({});
        int both = 0;
        int cosmeticOnly = 0;
        world.system("Both").kind(flecs::OnUpdate).run([&both](flecs::iter&) { ++both; }).add<SimSystem>().add<CosmeticSystem>();
        world.system("CosmeticOnly").kind(flecs::OnUpdate).run([&cosmeticOnly](flecs::iter&) { ++cosmeticOnly; }).add<CosmeticSystem>();
        runSimulationTicks(world, kSimTickDt);
        CHECK_EQ(both, 1);
        CHECK_EQ(cosmeticOnly, 0);
        world.progress(kSimTickDt);
        CHECK_EQ(both, 1);
        CHECK_EQ(cosmeticOnly, 0);
        runCosmeticTicks(world, kSimTickDt);
        CHECK_EQ(both, 2);
        CHECK_EQ(cosmeticOnly, 1);
    }
}

}
