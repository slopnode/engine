#include "test_assert.hpp"

#include "camera/components.hpp"
#include "game/sim_interpolation.hpp"
#include "game/sim_pipeline.hpp"
#include "input/input_command.hpp"
#include "input/input_context.hpp"
#include "input/input_state.hpp"
#include "input/player_command.hpp"
#include "map/brush.hpp"
#include "map/bsp.hpp"
#include "net/memory_transport.hpp"
#include "net/net_protocol.hpp"
#include "net/net_rewind.hpp"
#include "net/net_session.hpp"
#include "net/net_state.hpp"
#include "physics/components.hpp"
#include "physics/physics_module.hpp"
#include "physics/physics_world.hpp"
#include "physics/ragdoll_rig.hpp"
#include "render/components.hpp"
#include "script/net_script.hpp"
#include "script/player_target.hpp"
#include "script/script_scope.hpp"

#include <s7.h>

#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace slopengine {

namespace {

bool near(float a, float b) {
    return std::fabs(a - b) < 1.0e-3f;
}

float feetX(PhysicsWorld& physics, flecs::entity entity) {
    return static_cast<float>(physics.characterPosition(static_cast<std::uint64_t>(entity.id())).GetX());
}

void moveTo(PhysicsWorld& physics, flecs::entity entity, float x) {
    entity.get_mut<LocalTransformation>().position.x = x;
    physics.placeCharacter(static_cast<std::uint64_t>(entity.id()), x, 0.0f, 0.0f);
}

void runRewindProtocolTests() {
    NetCommandFrame frame;
    frame.tick = 7;
    frame.viewTick = 93.25;
    NetCommandFrame ahead;
    ahead.tick = 8;
    ahead.viewTick = 101.5;
    NetCommandFrame none;
    none.tick = 9;
    std::vector<NetCommandFrame> out;
    std::uint64_t acked = 0;
    CHECK(decodeNetCommands(encodeNetCommands({frame, ahead, none}, 100), out, acked));
    CHECK(acked == 100 && out.size() == 3);
    CHECK(out.size() == 3 && std::fabs(out[0].viewTick - 93.25) < 1.0e-9);
    CHECK(out.size() == 3 && std::fabs(out[1].viewTick - 101.5) < 1.0e-9);
    CHECK(out.size() == 3 && out[2].viewTick == 0.0);
    out.clear();
    CHECK(decodeNetCommands(encodeNetCommands({frame}, 0), out, acked));
    CHECK(out.size() == 1 && std::fabs(out[0].viewTick - 93.25) < 1.0e-9);
}

void runRewindWorldTests() {
    flecs::world world;
    PhysicsWorld physics;
    world.set<PhysicsContext>({&physics});
    world.component<NetRewind>();
    world.ensure<NetRewind>().maxTicks = 30;
    const CharacterMotor motor{};
    const auto body = [&](const char* name) {
        flecs::entity entity = world.entity(name).add<PlayerBody>().set<LocalTransformation>({});
        physics.createCharacter(static_cast<std::uint64_t>(entity.id()), 0.0f, 0.0f, 0.0f, motor);
        return entity;
    };
    const flecs::entity shooter = body("player-2");
    const flecs::entity target = body("player-3");
    const flecs::entity walker =
        world.entity("walker").add<Actor>().set<LocalTransformation>({}).set<GlobalTransformation>({});
    const flecs::entity corpse =
        world.entity("corpse").add<Actor>().set<LocalTransformation>({}).set<GlobalTransformation>({});
    const flecs::entity child = world.entity("held").add<Actor>().set<LocalTransformation>({}).child_of(walker);
    const flecs::entity unnamed = world.entity().add<Actor>().set<LocalTransformation>({});
    for (std::uint64_t tick = 1; tick <= 40; ++tick) {
        const float x = static_cast<float>(tick);
        moveTo(physics, shooter, x);
        moveTo(physics, target, x);
        walker.get_mut<LocalTransformation>().position.x = x;
        corpse.get_mut<LocalTransformation>().position.x = x;
        child.get_mut<LocalTransformation>().position.x = x;
        unnamed.get_mut<LocalTransformation>().position.x = x;
        recordNetRewind(world, tick);
    }
    CHECK(world.get<NetRewind>().frames.size() == 31);
    corpse.add<ActorCorpse>();

    beginNetRewind(world, shooter);
    CHECK(near(target.get<LocalTransformation>().position.x, 40.0f));
    CHECK(world.get<NetRewind>().rewoundTo < 0.0);
    endNetRewind(world);

    shooter.set<NetView>({35.5});
    beginNetRewind(world, shooter);
    CHECK(std::fabs(world.get<NetRewind>().rewoundTo - 35.5) < 1.0e-9);
    CHECK(near(target.get<LocalTransformation>().position.x, 35.5f));
    CHECK(near(feetX(physics, target), 35.5f));
    CHECK(near(walker.get<LocalTransformation>().position.x, 35.5f));
    CHECK(near(walker.get<GlobalTransformation>().matrix.m12, 35.5f));
    CHECK(near(shooter.get<LocalTransformation>().position.x, 40.0f));
    CHECK(near(feetX(physics, shooter), 40.0f));
    CHECK(near(corpse.get<LocalTransformation>().position.x, 40.0f));
    CHECK(near(child.get<LocalTransformation>().position.x, 40.0f));
    CHECK(near(unnamed.get<LocalTransformation>().position.x, 40.0f));
    beginNetRewind(world, target);
    endNetRewind(world);
    CHECK(near(target.get<LocalTransformation>().position.x, 35.5f));
    endNetRewind(world);
    CHECK(near(target.get<LocalTransformation>().position.x, 40.0f));
    CHECK(near(feetX(physics, target), 40.0f));
    CHECK(near(walker.get<GlobalTransformation>().matrix.m12, 40.0f));
    CHECK(world.get<NetRewind>().rewoundTo < 0.0);
    endNetRewind(world);
    CHECK(world.get<NetRewind>().depth == 0);

    shooter.set<NetView>({2.0});
    beginNetRewind(world, shooter);
    CHECK(near(target.get<LocalTransformation>().position.x, 10.0f));
    target.set<RagdollRig>({});
    target.get_mut<LocalTransformation>().position.x = -3.0f;
    endNetRewind(world);
    CHECK(near(target.get<LocalTransformation>().position.x, -3.0f));
    CHECK(near(walker.get<LocalTransformation>().position.x, 40.0f));
    target.remove<RagdollRig>();
    target.get_mut<LocalTransformation>().position.x = 40.0f;

    moveTo(physics, target, 46.0f);
    recordNetRewind(world, 41);
    shooter.set<NetView>({40.5});
    beginNetRewind(world, shooter);
    CHECK(near(target.get<LocalTransformation>().position.x, 40.0f));
    endNetRewind(world);
    shooter.set<NetView>({40.75});
    beginNetRewind(world, shooter);
    CHECK(near(target.get<LocalTransformation>().position.x, 40.0f));
    endNetRewind(world);

    world.get_mut<NetRewind>().maxTicks = 0;
    recordNetRewind(world, 42);
    CHECK(world.get<NetRewind>().frames.empty());
    world.get_mut<NetRewind>().maxTicks = 30;
    recordNetRewind(world, 43);
    recordNetRewind(world, 43);
    CHECK(world.get<NetRewind>().frames.size() == 1);

    s7_scheme* sc = s7_init();
    bindNetApi(world, sc);
    for (std::uint64_t tick = 44; tick <= 50; ++tick) {
        moveTo(physics, target, static_cast<float>(tick));
        recordNetRewind(world, tick);
    }
    shooter.set<NetView>({46.0});
    s7_eval_c_string(sc, "(define seen #f)");
    s7_eval_c_string(sc, "(define after #f)");
    {
        ScriptScopeGuard scope(ScriptScope::World);
        ScriptPlayerGuard player("player-2");
        CHECK(s7_real(s7_eval_c_string(sc, "(with-lag-compensation (lambda () (net-rewound-tick)))")) == 46.0);
        CHECK(near(target.get<LocalTransformation>().position.x, 50.0f));
        s7_eval_c_string(
            sc, "(catch #t (lambda () (with-lag-compensation (lambda () (set! seen (net-rewound-tick)) (error 'boom))))"
                " (lambda args (set! after (net-rewound-tick))))");
        CHECK(s7_real(s7_name_to_value(sc, "seen")) == 46.0);
        CHECK(s7_name_to_value(sc, "after") == s7_f(sc));
        CHECK(near(target.get<LocalTransformation>().position.x, 50.0f));
        CHECK(world.get<NetRewind>().depth == 0);
    }
    {
        ScriptScopeGuard scope(ScriptScope::Present);
        ScriptPlayerGuard player("player-2");
        s7_eval_c_string(sc, "(set! seen 'untouched)");
        s7_eval_c_string(sc, "(catch #t (lambda () (with-lag-compensation (lambda () (set! seen 'ran)))) (lambda args #f))");
        CHECK(s7_name_to_value(sc, "seen") == s7_make_symbol(sc, "untouched"));
        CHECK(world.get<NetRewind>().depth == 0);
    }
}

struct RewindSide {
    flecs::world world;
    PhysicsWorld physics;
    std::unique_ptr<NetSession> session;
};

void prepareRewindSide(RewindSide& side) {
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
    side.physics.addStaticBrushes({makeBrushBox("floor", {-20.0f, -0.25f, -20.0f}, {20.0f, 0.0f, 20.0f}, "mat/a", {})});
    side.world.set<PhysicsContext>({&side.physics});
    CharacterMotor motor{};
    motor.eyeHeight = 1.6f;
    const flecs::entity player = side.world.entity("Player")
                                     .add<LocalPlayer>()
                                     .add<PlayerBody>()
                                     .set<LocalTransformation>({})
                                     .set<FirstPersonController>({})
                                     .set<Lens>({})
                                     .set<CharacterMotor>(motor);
    side.physics.setPlayerId(static_cast<std::uint64_t>(player.id()));
    side.physics.createPlayerCharacter(0.0f, 0.05f, 0.0f, motor);
}

NetSessionConfig rewindConfig(std::string listen, std::string connect, std::uint64_t hash) {
    NetSessionConfig out;
    out.listenAddress = std::move(listen);
    out.connectAddress = std::move(connect);
    out.hello.engineVersion = "test";
    out.hello.schemaHash = hash;
    out.hello.packages = {{"game", "1.0.0"}};
    out.maxRewindTicks = 40;
    return out;
}

void runRewindSessionTests() {
    netSchema().clear();
    netSchema().finalize();
    const std::uint64_t hash = netSchema().hash();
    MemoryNetwork network;
    RewindSide server;
    prepareRewindSide(server);
    server.session = std::make_unique<NetSession>(server.world, rewindConfig("server", "", hash), network.endpoint());
    server.session->registerSystems();
    CHECK(server.session->start());
    server.session->serverMapLoaded();
    RewindSide client;
    prepareRewindSide(client);
    const flecs::entity walker =
        server.world.entity("walker").add<Actor>().set<LocalTransformation>({}).set<GlobalTransformation>({});
    const flecs::entity shown =
        client.world.entity("walker").add<Actor>().set<LocalTransformation>({}).set<GlobalTransformation>({});
    std::map<std::uint64_t, float> shownAt;
    NetSessionConfig clientConfig = rewindConfig("", "server", hash);
    clientConfig.predict = [&](const PlayerCommand& command) {
        shownAt[command.tick] = shown.get<LocalTransformation>().position.x;
    };
    client.session = std::make_unique<NetSession>(client.world, std::move(clientConfig), network.endpoint());
    client.session->registerSystems();
    CHECK(client.session->start());
    std::vector<float> seen;
    std::vector<float> aimed;
    std::vector<float> live;
    std::vector<std::uint64_t> commands;
    server.world.system("Aim")
        .kind(flecs::OnUpdate)
        .run([&](flecs::iter& it) {
            flecs::world world = it.world();
            const flecs::entity shooter = world.lookup("Player");
            if (!shooter.has<NetView>() || !shooter.has<PlayerCommand>() || shooter.get<PlayerCommand>().commands == 0) {
                return;
            }
            live.push_back(walker.get<LocalTransformation>().position.x);
            commands.push_back(shooter.get<NetView>().command);
            beginNetRewind(world, shooter);
            aimed.push_back(walker.get<LocalTransformation>().position.x);
            endNetRewind(world);
        }).add<SimSystem>();
    network.setConditions(MemoryLinkConditions{50.0f, 0.0f, 0.0f});
    for (int i = 0; i < 180; ++i) {
        server.session->serverPoll();
        server.world.get_mut<SimClock>().tick += 1;
        walker.get_mut<LocalTransformation>().position.x = 0.05f * static_cast<float>(i);
        server.world.run_pipeline(server.world.get<FramePipelines>().sim, kSimTickDt);
        client.session->clientFrame(kSimTickDt);
        if (client.session->clientConnected()) {
            seen.push_back(shown.get<LocalTransformation>().position.x);
        }
        network.advance(kSimTickDt);
    }
    CHECK(client.session->clientConnected());
    CHECK(aimed.size() > 60);
    const auto matched = [&](float x) {
        for (float y : seen) {
            if (std::fabs(x - y) < 0.002f) {
                return true;
            }
        }
        return false;
    };
    std::size_t aimedMatches = 0;
    float behind = 0.0f;
    float worst = 0.0f;
    for (std::size_t i = aimed.size() / 2; i < aimed.size(); ++i) {
        aimedMatches += matched(aimed[i]) ? 1 : 0;
        behind = std::max(behind, std::fabs(live[i] - aimed[i]));
        const auto at = shownAt.find(commands[i]);
        CHECK(at != shownAt.end());
        if (at != shownAt.end()) {
            worst = std::max(worst, std::fabs(at->second - aimed[i]));
        }
    }
    CHECK(aimedMatches == aimed.size() - aimed.size() / 2);
    CHECK(worst < 0.002f);
    CHECK(behind > 0.5f);
    CHECK(near(walker.get<LocalTransformation>().position.x, 0.05f * 179.0f));
    const flecs::entity body = server.world.lookup("Player");
    CHECK(body.has<NetView>());
    const NetRewind& rewind = server.world.get<NetRewind>();
    CHECK(rewind.maxTicks == 40);
    CHECK(rewind.frames.size() == 41);
    const double age = static_cast<double>(rewind.frames.back().tick) - body.get<NetView>().tick;
    CHECK(age >= 10.0 && age <= 20.0);
    server.session->serverMapLoaded();
    CHECK(server.world.get<NetRewind>().frames.empty());
    client.session.reset();
    server.session.reset();
    netSchema().clear();
}

}

void runNetRewindTests() {
    runRewindProtocolTests();
    runRewindWorldTests();
    runRewindSessionTests();
}

}
