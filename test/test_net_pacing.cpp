#include "test_assert.hpp"

#include "camera/components.hpp"
#include "game/sim_interpolation.hpp"
#include "game/sim_pipeline.hpp"
#include "input/input_command.hpp"
#include "input/input_context.hpp"
#include "input/input_state.hpp"
#include "input/player_command.hpp"
#include "map/bsp.hpp"
#include "net/memory_transport.hpp"
#include "net/net_session.hpp"
#include "net/net_state.hpp"
#include "physics/components.hpp"
#include "render/animation_player.hpp"
#include "render/components.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace slopengine {

namespace {

class CountingTransport final : public NetTransport {
public:
    CountingTransport(std::unique_ptr<NetTransport> inner, std::vector<std::size_t>& unreliable)
        : inner_(std::move(inner)), unreliable_(unreliable) {}

    bool listen(std::string_view address) override { return inner_->listen(address); }
    NetConnectionId connect(std::string_view address) override { return inner_->connect(address); }
    bool accept(NetConnectionId connection) override { return inner_->accept(connection); }
    bool send(NetConnectionId connection, std::span<const std::byte> data, NetSendMode mode) override {
        if (mode == NetSendMode::Unreliable) {
            unreliable_.push_back(data.size());
        }
        return inner_->send(connection, data, mode);
    }
    void close(NetConnectionId connection, std::string_view reason) override { inner_->close(connection, reason); }
    void poll(std::vector<NetEvent>& out) override { inner_->poll(out); }
    NetConnectionStats stats(NetConnectionId connection) const override { return inner_->stats(connection); }

private:
    std::unique_ptr<NetTransport> inner_;
    std::vector<std::size_t>& unreliable_;
};

struct PacingSide {
    flecs::world world;
    std::unique_ptr<NetSession> session;
};

void preparePacingSide(PacingSide& side, int crates, bool animated) {
    registerSimPipelines(side.world);
    registerSimInterpolation(side.world);
    InputState input{};
    input.resize(8);
    side.world.set<InputState>(std::move(input));
    side.world.set<InputCommand>({});
    side.world.set<InputContextStack>({});
    side.world.component<LocalPlayer>();
    side.world.component<PlayerCommand>();
    registerNetState(side.world);
    side.world.set<CurrentMap>({"testmap"});
    side.world.entity("Player")
        .add<LocalPlayer>()
        .add<PlayerBody>()
        .set<LocalTransformation>({})
        .set<FirstPersonController>({})
        .set<Lens>({})
        .set<CharacterMotor>({});
    for (int i = 0; i < crates; ++i) {
        LocalTransformation local{};
        local.position = {0.0f, 0.0f, static_cast<float>(i)};
        flecs::entity crate = side.world.entity(("crate-" + std::to_string(i)).c_str())
                                  .add<Actor>()
                                  .set<LocalTransformation>(local)
                                  .set<GlobalTransformation>({});
        if (animated) {
            AnimationPlayer animation{};
            animation.animBankPath = "bank";
            crate.set<AnimationPlayer>(animation);
        }
    }
}

NetSessionConfig pacingConfig(std::string listen, std::string connect, std::uint64_t hash) {
    NetSessionConfig out;
    out.listenAddress = std::move(listen);
    out.connectAddress = std::move(connect);
    out.hello.engineVersion = "test";
    out.hello.schemaHash = hash;
    out.hello.packages = {{"game", "1.0.0"}};
    return out;
}

struct PacingRun {
    MemoryNetwork network;
    PacingSide server;
    PacingSide client;
    std::vector<std::size_t> sent;
    std::function<void(flecs::world&)> step;

    PacingRun(int crates, bool animated, std::uint64_t hash) {
        preparePacingSide(server, crates, animated);
        preparePacingSide(client, crates, animated);
        server.world.system("StepCrates")
            .kind(flecs::OnUpdate)
            .run([this](flecs::iter& it) {
                if (step) {
                    flecs::world world = it.world();
                    step(world);
                }
            }).add<SimSystem>();
        server.session = std::make_unique<NetSession>(
            server.world, pacingConfig("server", "", hash), std::make_unique<CountingTransport>(network.endpoint(), sent));
        server.session->registerSystems();
        CHECK(server.session->start());
        server.session->serverMapLoaded();
        client.session = std::make_unique<NetSession>(client.world, pacingConfig("", "server", hash), network.endpoint());
        client.session->registerSystems();
        CHECK(client.session->start());
    }

    void frames(int count) {
        for (int i = 0; i < count; ++i) {
            server.session->serverPoll();
            server.world.get_mut<SimClock>().tick += 1;
            server.world.run_pipeline(server.world.get<FramePipelines>().sim, kSimTickDt);
            client.session->clientFrame(kSimTickDt);
            network.advance(kSimTickDt);
        }
    }

    float clientX(const std::string& name) const {
        const flecs::entity entity = client.world.lookup(name.c_str());
        return entity.is_valid() ? entity.get<LocalTransformation>().position.x : -1.0f;
    }

    float serverX(const std::string& name) const {
        return server.world.lookup(name.c_str()).get<LocalTransformation>().position.x;
    }
};

void moveCrate(flecs::world& world, int index, float dx) {
    world.lookup(("crate-" + std::to_string(index)).c_str()).get_mut<LocalTransformation>().position.x += dx;
}

constexpr std::size_t kSealOverhead = 64;

} // namespace

void runNetPacingTests() {
    netSchema().clear();
    netSchema().finalize();
    const std::uint64_t hash = netSchema().hash();
    const NetSessionConfig defaults;

    {
        PacingRun run(300, false, hash);
        run.step = [](flecs::world& world) {
            for (int i = 0; i < 300; ++i) {
                moveCrate(world, i, 0.01f * static_cast<float>(i % 7 + 1));
            }
        };
        run.frames(240);
        CHECK(run.client.session->clientConnected());
        const std::size_t largest = *std::max_element(run.sent.begin(), run.sent.end());
        CHECK(largest <= defaults.snapshotBudgetBytes + kSealOverhead);
        float worstLag = 0.0f;
        bool allMoved = true;
        for (int i = 0; i < 300; ++i) {
            const std::string name = "crate-" + std::to_string(i);
            const float client = run.clientX(name);
            allMoved = allMoved && client > 0.0f;
            worstLag = std::max(worstLag, run.serverX(name) - client);
        }
        CHECK(allMoved);
        CHECK(worstLag < 3.0f);
        CHECK(run.sent.size() >= 110 && run.sent.size() <= 130);
    }

    {
        PacingRun run(300, false, hash);
        run.frames(180);
        CHECK(run.client.session->clientConnected());
        bool everyCrateSeen = true;
        for (int i = 0; i < 300; ++i) {
            everyCrateSeen = everyCrateSeen && run.client.world.lookup(("crate-" + std::to_string(i)).c_str()).has<SimPose>();
        }
        CHECK(everyCrateSeen);
        const std::size_t quiet = *std::max_element(run.sent.end() - 20, run.sent.end());
        CHECK(quiet < 160);
    }

    {
        PacingRun run(20, false, hash);
        run.frames(30);
        CHECK(run.client.session->clientConnected());
        run.network.setConditions({20.0f, 0.0f, 0.4f});
        int moved = 0;
        run.step = [&moved](flecs::world& world) {
            for (int i = 0; i < 20; ++i) {
                if (moved < 40 + i) {
                    moveCrate(world, i, 0.0371f);
                }
            }
            ++moved;
        };
        run.frames(400);
        int settledCount = 0;
        for (int i = 0; i < 20; ++i) {
            const std::string name = "crate-" + std::to_string(i);
            const float settled = std::round(run.serverX(name) * kNetPositionScale) / kNetPositionScale;
            settledCount += std::fabs(run.clientX(name) - settled) < 1.0e-5f ? 1 : 0;
        }
        CHECK_EQ(settledCount, 20);
    }

    {
        PacingRun run(1, false, hash);
        bool go = false;
        run.step = [&go](flecs::world& world) {
            if (go) {
                moveCrate(world, 0, 0.05f);
            }
        };
        run.frames(120);
        CHECK(run.clientX("crate-0") == 0.0f);
        go = true;
        run.frames(4);
        CHECK(run.clientX("crate-0") == 0.0f);
        run.frames(30);
        CHECK(run.clientX("crate-0") > 0.5f);
    }

    {
        PacingRun run(1, false, hash);
        run.network.setConditions({40.0f, 15.0f, 0.0f});
        run.step = [](flecs::world& world) { moveCrate(world, 0, 0.05f); };
        run.frames(90);
        float last = run.clientX("crate-0");
        int stalls = 0;
        float worstStep = 0.0f;
        for (int i = 0; i < 120; ++i) {
            run.frames(1);
            const float now = run.clientX("crate-0");
            if (now - last <= 0.0f) {
                ++stalls;
            }
            worstStep = std::max(worstStep, std::fabs(now - last - 0.05f));
            last = now;
        }
        CHECK_EQ(stalls, 0);
        CHECK(worstStep < 0.03f);
        const SimPose& pose = run.client.world.lookup("crate-0").get<SimPose>();
        CHECK(std::fabs(pose.cur.position.x - pose.prev.position.x - 0.05f) < 0.01f);
    }

    {
        PacingRun run(50, true, hash);
        run.step = [](flecs::world& world) {
            for (int i = 0; i < 50; ++i) {
                AnimationPlayer& animation =
                    world.lookup(("crate-" + std::to_string(i)).c_str()).get_mut<AnimationPlayer>();
                animation.clipName = "Idle";
                animation.playing = true;
                animation.loop = true;
                animation.time += kSimTickDt;
            }
        };
        run.frames(120);
        const std::size_t quiet = *std::max_element(run.sent.end() - 20, run.sent.end());
        CHECK(quiet < 160);
        const flecs::entity crate = run.client.world.lookup("crate-7");
        const float before = crate.get<AnimationPlayer>().time;
        run.frames(30);
        const float after = crate.get<AnimationPlayer>().time;
        CHECK(crate.get<AnimationPlayer>().clipName == "Idle" && crate.get<AnimationPlayer>().playing);
        CHECK(std::fabs(after - before - 30.0f * kSimTickDt) < 0.01f);
        const float serverTime = run.server.world.lookup("crate-7").get<AnimationPlayer>().time;
        CHECK(std::fabs(serverTime - after - static_cast<float>(defaults.interpolationDelayTicks) * kSimTickDt) < 0.1f);
    }

    netSchema().clear();
}

}
