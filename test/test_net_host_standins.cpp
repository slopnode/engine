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
#include "physics/ragdoll_rig.hpp"
#include "render/components.hpp"
#include "script/net_script.hpp"
#include "script/script_context.hpp"
#include "script/script_scope.hpp"
#include "physics/physics_module.hpp"

#include <s7.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace slopengine {

namespace {

struct HostSide {
    flecs::world world;
    PhysicsWorld physics;
    std::unique_ptr<NetSession> session;
};

void prepareHostSide(HostSide& side) {
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

NetSessionConfig hostConfig(std::string listen, std::string connect, std::uint64_t hash) {
    NetSessionConfig out;
    out.listenAddress = std::move(listen);
    out.connectAddress = std::move(connect);
    out.hello.engineVersion = "test";
    out.hello.schemaHash = hash;
    out.hello.packages = {{"game", "1.0.0"}};
    return out;
}

flecs::world* g_hostWorld = nullptr;

s7_pointer makeStandIn(s7_scheme* sc, s7_pointer args) {
    const char* name = s7_string(s7_car(args));
    g_hostWorld->entity(name).add<Actor>().set<LocalTransformation>({}).set<GlobalTransformation>({}).set<CharacterMotor>({});
    return s7_t(sc);
}

s7_pointer dropStandIn(s7_scheme* sc, s7_pointer args) {
    const flecs::entity entity = g_hostWorld->lookup(s7_string(s7_car(args)));
    if (entity.is_valid()) {
        entity.destruct();
    }
    return s7_t(sc);
}

} // namespace

void runNetHostStandInTests() {
    actionRegistry().clear();
    actionRegistry().registerCoreActions();
    s7_scheme* sc = s7_init();
    netSchema().clear();
    std::vector<std::string> messages;
    netSchema().addPackageFields(
        sc, s7_eval_c_string(sc, "'((player (health (type int 0 999) (default 100) (send all))))"), "game",
        PackageRole::Base, messages);
    netSchema().finalize();
    const std::uint64_t hash = netSchema().hash();

    MemoryNetwork network;
    HostSide server;
    prepareHostSide(server);
    g_hostWorld = &server.world;
    server.world.set<ScriptContext>({sc});
    s7_define_function(sc, "make-stand-in", makeStandIn, 1, 0, false, "");
    s7_define_function(sc, "drop-stand-in", dropStandIn, 1, 0, false, "");
    s7_eval_c_string(sc, "(define appeared '())");
    s7_eval_c_string(sc, "(define (on-player-appear id) (set! appeared (cons id appeared)) (make-stand-in id))");
    s7_eval_c_string(sc, "(define (on-player-vanish id) (drop-stand-in id) (set! appeared (cons (string-append \"gone \" id) appeared)))");
    NetSessionConfig serverConfig = hostConfig("server", "", hash);
    serverConfig.hostPlayerVisible = true;
    serverConfig.firstClientTakesMapPlayer = false;
    server.session = std::make_unique<NetSession>(server.world, std::move(serverConfig), network.endpoint());
    server.session->registerSystems();
    CHECK(server.session->start());
    server.session->serverMapLoaded();

    auto client = std::make_unique<HostSide>();
    prepareHostSide(*client);
    client->session = std::make_unique<NetSession>(client->world, hostConfig("", "server", hash), network.endpoint());
    client->session->registerSystems();
    CHECK(client->session->start());

    auto frame = [&](int count) {
        for (int i = 0; i < count; ++i) {
            server.session->serverPoll();
            server.world.get_mut<SimClock>().tick += 1;
            server.world.run_pipeline(server.world.get<FramePipelines>().sim, kSimTickDt);
            server.world.progress(kSimTickDt);
            if (client) {
                client->session->clientFrame(kSimTickDt);
            }
            network.advance(kSimTickDt);
        }
    };
    bindNetApi(client->world, sc);
    frame(30);
    CHECK(client->session->clientConnected());
    {
        ScriptScopeGuard scope(ScriptScope::Present);
        CHECK(s7_boolean(sc, s7_eval_c_string(sc, "(equal? (net-player-name) \"player-2\")")));
    }
    const flecs::entity body = server.world.lookup("player-2");
    CHECK(body.is_valid());
    const flecs::entity standIn = server.world.lookup("player:player-2");
    CHECK(standIn.is_valid() && standIn.has<NetStandIn>() && standIn.has<NetHostStandIn>());
    CHECK(standIn.is_valid() && !standIn.has<CharacterMotor>());
    CHECK(findNetPlayer(server.world, "player:player-2") != nullptr);
    CHECK(server.world.lookup("player:Player") == flecs::entity{});
    CHECK_FALSE(standIn.has<NetId>());

    body.get_mut<LocalTransformation>().position = {4.0f, 0.0f, -2.0f};
    for (NetPlayerRecord& record : server.world.get_mut<NetStore>().players) {
        if (record.id == "player-2") {
            record.values[0].integer = 42;
        }
    }
    frame(4);
    CHECK(std::fabs(standIn.get<LocalTransformation>().position.x - 4.0f) < 1.0e-3f);
    CHECK(findNetPlayer(server.world, "player:player-2")->values[0].integer == 42);
    CHECK(s7_list_length(sc, s7_name_to_value(sc, "appeared")) == 1);

    standIn.set<RagdollRig>({});
    body.get_mut<LocalTransformation>().position = {9.0f, 0.0f, -2.0f};
    frame(4);
    CHECK(std::fabs(standIn.get<LocalTransformation>().position.x - 4.0f) < 1.0e-3f);
    standIn.remove<RagdollRig>();
    frame(4);
    CHECK(std::fabs(standIn.get<LocalTransformation>().position.x - 9.0f) < 1.0e-3f);

    server.world.entity("walker").add<Actor>().set<LocalTransformation>({}).set<GlobalTransformation>({});
    client->world.entity("walker").add<Actor>().set<LocalTransformation>({}).set<GlobalTransformation>({});
    frame(10);
    server.session->serverMapLoaded();
    frame(30);
    bool walkerKnown = false;
    for (const auto& [name, player] : client->session->clientKnownEntities()) {
        walkerKnown = walkerKnown || name == "walker";
    }
    CHECK(walkerKnown);
    CHECK(server.world.lookup("player:player-2").is_valid());
    CHECK(server.world.lookup("player:player-2").has<NetHostStandIn>());

    client.reset();
    frame(30);
    CHECK(s7_boolean(sc, s7_eval_c_string(sc, "(and (member \"gone player:player-2\" appeared) #t)")));
    CHECK(findNetPlayer(server.world, "player:player-2") == nullptr);

    server.session.reset();
    netSchema().clear();
}

}
