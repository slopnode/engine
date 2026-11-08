#include "test_assert.hpp"

#include "camera/components.hpp"
#include "game/sim_interpolation.hpp"
#include "game/sim_pipeline.hpp"
#include "input/action_registry.hpp"
#include "input/input_command.hpp"
#include "input/input_context.hpp"
#include "input/input_state.hpp"
#include "input/player_command.hpp"
#include "map/brush.hpp"
#include "map/bsp.hpp"
#include "net/memory_transport.hpp"
#include "net/net_session.hpp"
#include "net/net_state.hpp"
#include "physics/components.hpp"
#include "physics/physics_module.hpp"
#include "physics/physics_world.hpp"
#include "render/components.hpp"
#include "script/net_script.hpp"
#include "script/script_context.hpp"
#include "script/script_scope.hpp"

#include <s7.h>

#include <memory>
#include <string>
#include <vector>

namespace slopengine {

namespace {

struct EventSide {
    flecs::world world;
    PhysicsWorld physics;
    std::unique_ptr<NetSession> session;
};

void prepareEventSide(EventSide& side) {
    registerSimPipelines(side.world);
    registerSimInterpolation(side.world);
    InputState input{};
    input.resize(actionRegistry().size());
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

NetSessionConfig eventConfig(std::string listen, std::string connect, std::uint64_t hash) {
    NetSessionConfig out;
    out.listenAddress = std::move(listen);
    out.connectAddress = std::move(connect);
    out.hello.engineVersion = "test";
    out.hello.schemaHash = hash;
    out.hello.packages = {{"game", "1.0.0"}};
    return out;
}

NetEventMessage shotEvent(const char* cause) {
    NetEventMessage event;
    event.eventId = static_cast<std::uint32_t>(netSchema().findEvent("shot")->id);
    event.where = NetEventWhere::Player;
    event.player = kLocalNetPlayerId;
    NetValue n;
    n.kind = NetTypeKind::Int;
    n.integer = 1;
    event.args.push_back(n);
    event.cause = cause;
    return event;
}

} // namespace

void runNetPredictedEventTests() {
    actionRegistry().clear();
    actionRegistry().registerCoreActions();
    actionRegistry().registerPackageAction("fire", "Fire", 0);
    const int fire = actionRegistry().indexOf("fire");
    s7_scheme* sc = s7_init();
    std::vector<std::string> messages;
    netSchema().clear();
    netSchema().addPackageEvents(
        sc,
        s7_eval_c_string(sc, "'((shot (args (n (int 0 99))) (to owner) (predict #t)) (plain (to owner)))"),
        "game",
        PackageRole::Base,
        messages);
    netSchema().finalize();
    CHECK(messages.empty());
    CHECK(netSchema().findEvent("shot")->predict);
    CHECK_FALSE(netSchema().findEvent("plain")->predict);
    const std::uint64_t hash = netSchema().hash();

    MemoryNetwork network;
    EventSide server;
    prepareEventSide(server);
    int serverShots = 0;
    int shotsPerPress = 1;
    server.world.system("Shoot")
        .kind(flecs::OnUpdate)
        .run([&server, &serverShots, &shotsPerPress, fire](flecs::iter& it) {
            it.world().each([&](flecs::entity, PlayerBody, const PlayerCommand& command) {
                for (int i = 0; command.pressed(fire) && i < shotsPerPress; ++i) {
                    ++serverShots;
                    server.world.get_mut<NetEventQueue>().pending.push_back(shotEvent(kLocalNetPlayerId));
                }
            });
        }).add<SimSystem>();
    server.session = std::make_unique<NetSession>(server.world, eventConfig("server", "", hash), network.endpoint());
    server.session->registerSystems();
    CHECK(server.session->start());
    server.session->serverMapLoaded();

    EventSide client;
    prepareEventSide(client);
    client.world.set<ScriptContext>({sc});
    bindNetApi(client.world, sc);
    s7_eval_c_string(sc, "(define felt 0)");
    s7_eval_c_string(sc, "(define ticks '())");
    s7_eval_c_string(sc, "(define refused #f)");
    s7_eval_c_string(sc, "(define (on-event-shot where n) (set! felt (+ felt n)))");
    NetSessionConfig clientConfig = eventConfig("", "server", hash);
    clientConfig.predict = [&](const PlayerCommand& command) {
        ScriptScopeGuard scope(ScriptScope::Predict);
        s7_eval_c_string(sc, "(set! ticks (cons (net-command-tick) ticks))");
        if (command.pressed(fire)) {
            s7_eval_c_string(sc, "(net-event! 'shot \"Player\" 1)");
            s7_eval_c_string(sc, "(catch #t (lambda () (net-event! 'plain \"Player\")) (lambda args (set! refused #t)))");
        }
    };
    client.session = std::make_unique<NetSession>(client.world, std::move(clientConfig), network.endpoint());
    client.session->registerSystems();
    CHECK(client.session->start());

    auto frame = [&](int count) {
        for (int i = 0; i < count; ++i) {
            server.session->serverPoll();
            server.world.get_mut<SimClock>().tick += 1;
            server.world.run_pipeline(server.world.get<FramePipelines>().sim, kSimTickDt);
            client.session->clientFrame(kSimTickDt);
            network.advance(kSimTickDt);
        }
    };
    const auto felt = [&]() { return s7_integer(s7_name_to_value(sc, "felt")); };

    network.setConditions(MemoryLinkConditions{50.0f, 0.0f, 0.0f});
    frame(60);
    CHECK(client.session->clientConnected());
    CHECK(s7_is_integer(s7_car(s7_name_to_value(sc, "ticks"))));
    {
        ScriptScopeGuard scope(ScriptScope::Present);
        CHECK(s7_is_boolean(s7_eval_c_string(sc, "(net-command-tick)")));
    }

    client.world.get_mut<InputState>().actionPressedPending[static_cast<std::size_t>(fire)] = 1;
    frame(2);
    CHECK_EQ(felt(), 1);
    CHECK(s7_boolean(sc, s7_name_to_value(sc, "refused")));
    frame(30);
    CHECK_EQ(serverShots, 1);
    CHECK_EQ(felt(), 1);
    CHECK(client.world.get<NetPrediction>().ackedTick > 0);

    server.world.get_mut<NetEventQueue>().pending.push_back(shotEvent(kLocalNetPlayerId));
    frame(10);
    CHECK_EQ(felt(), 2);

    server.world.get_mut<NetEventQueue>().pending.push_back(shotEvent(""));
    frame(10);
    CHECK_EQ(felt(), 3);
    shotsPerPress = 2;
    client.world.get_mut<InputState>().actionPressedPending[static_cast<std::size_t>(fire)] = 1;
    frame(30);
    CHECK_EQ(serverShots, 3);
    CHECK_EQ(felt(), 5);
    frame(30);
    CHECK(client.world.get<NetPrediction>().raised.empty());

    client.session.reset();
    server.session.reset();
    netSchema().clear();
}

}
