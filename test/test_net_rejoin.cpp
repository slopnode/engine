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
#include "script/hook_registry.hpp"
#include "script/proc_role.hpp"
#include "script/script_context.hpp"

#include <s7.h>

#include <string>

namespace slopengine {

namespace {

struct Side {
    flecs::world world;
    std::unique_ptr<NetSession> session;
    std::string droppedReason;
};

void prepare(Side& side) {
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
}

NetSessionConfig config(Side& side, std::string listen, std::string connect, std::uint64_t hash) {
    NetSessionConfig out;
    out.listenAddress = std::move(listen);
    out.connectAddress = std::move(connect);
    out.hello.engineVersion = "test";
    out.hello.schemaHash = hash;
    out.hello.packages = {{"engine", "1.0.0"}, {"game", "1.0.0"}};
    out.loadMap = [](const std::string&, const std::string&) {};
    out.disconnected = [&side](const std::string& reason) { side.droppedReason = reason; };
    return out;
}

void startClient(Side& side, MemoryNetwork& network, std::uint64_t hash, std::optional<SecureKey> secret) {
    prepare(side);
    NetSessionConfig clientConfig = config(side, "", "server", hash);
    clientConfig.identitySecret = secret;
    side.session = std::make_unique<NetSession>(side.world, std::move(clientConfig), network.endpoint());
    side.session->registerSystems();
    CHECK(side.session->start());
}

void frames(MemoryNetwork& network, Side& server, std::initializer_list<Side*> clients, int count) {
    for (int i = 0; i < count; ++i) {
        server.session->serverPoll();
        server.world.get_mut<SimClock>().tick += 1;
        server.world.run_pipeline(server.world.get<FramePipelines>().sim, kSimTickDt);
        for (Side* client : clients) {
            if (client->session) {
                client->session->clientFrame(kSimTickDt);
            }
        }
        network.advance(kSimTickDt);
    }
}

std::string seen(s7_scheme* sc) {
    char* text = s7_object_to_c_string(sc, s7_eval_c_string(sc, "(reverse seen)"));
    std::string out = text;
    free(text);
    return out;
}

int healthOf(const flecs::world& world, const char* id, const NetField* health) {
    const NetPlayerRecord* record = findNetPlayer(world, id);
    return record == nullptr ? -1 : record->values[static_cast<std::size_t>(health->slot)].integer;
}

} // namespace

void runNetRejoinTests() {
    s7_scheme* sc = s7_init();
    std::vector<std::string> messages;
    netSchema().clear();
    netSchema().addPackageFields(
        sc,
        s7_eval_c_string(sc, "'((player (health (type int 0 999) (default 100) (send all) (travel #t))"
                             " (frags (type int 0 999) (default 0) (send all))))"),
        "game",
        PackageRole::Base,
        messages);
    netSchema().finalize();
    const std::uint64_t hash = netSchema().hash();
    const NetField* health = netSchema().find(NetScope{NetScopeKind::Player, {}}, "health");

    MemoryNetwork network;
    Side server;
    prepare(server);
    NetSessionConfig serverConfig = config(server, "server", "", hash);
    serverConfig.sessionKind = "deathmatch";
    serverConfig.maxPlayers = 4;
    serverConfig.playerCeiling = 4;
    serverConfig.firstClientTakesMapPlayer = false;
    serverConfig.rejoinHoldTicks = 600;
    server.session = std::make_unique<NetSession>(server.world, std::move(serverConfig), network.endpoint());
    server.session->registerSystems();
    server.world.set<ScriptContext>({sc});
    s7_eval_c_string(sc, "(define seen '())");
    s7_eval_c_string(sc, "(define (on-player-join id how) (set! seen (cons (list 'join id how) seen)))");
    s7_eval_c_string(sc, "(define (on-player-leave id reason) (set! seen (cons (list 'leave id reason) seen)))");
    captureHookOwners(sc);
    CHECK(server.session->start());
    server.session->serverMapLoaded();

    SecureKey aliceKey{};
    aliceKey[0] = 7;
    Side alice;
    startClient(alice, network, hash, aliceKey);
    frames(network, server, {&alice}, 10);
    CHECK(alice.session->clientConnected());
    CHECK(alice.session->clientPlayer() == "player-2");
    CHECK(seen(sc) == "((join \"player-2\" open))");
    CHECK(alice.world.has<NetSessionLimits>());
    CHECK(alice.world.get<NetSessionLimits>().kind == "deathmatch");
    CHECK(alice.world.get<NetSessionLimits>().maxPlayers == 4);
    CHECK(alice.world.get<NetSessionLimits>().joinable && alice.world.get<NetSessionLimits>().remote);
    CHECK_EQ(static_cast<int>(alice.world.get<NetRoster>().entries.size()), 1);

    server.world.get_mut<NetSessionLimits>().maxPlayers = 3;
    frames(network, server, {&alice}, 3);
    CHECK(alice.world.get<NetSessionLimits>().maxPlayers == 3);

    for (NetPlayerRecord& record : server.world.get_mut<NetStore>().players) {
        if (record.id == "player-2") {
            record.values[static_cast<std::size_t>(health->slot)].integer = 42;
        }
    }
    s7_eval_c_string(sc, "(set! seen '())");
    alice.session.reset();
    frames(network, server, {}, 5);
    CHECK_FALSE(server.session->serverHasClient());
    CHECK(seen(sc).rfind("((leave \"player-2\" ", 0) == 0);
    CHECK_EQ(static_cast<int>(server.session->serverHeldCount()), 1);

    server.world.get_mut<NetSessionLimits>().joinable = false;
    Side stranger;
    startClient(stranger, network, hash, std::nullopt);
    frames(network, server, {&stranger}, 10);
    CHECK(stranger.droppedReason == "not taking new players");

    s7_eval_c_string(sc, "(set! seen '())");
    Side back;
    startClient(back, network, hash, aliceKey);
    frames(network, server, {&back}, 10);
    CHECK(back.session->clientConnected());
    CHECK(back.session->clientPlayer() == "player-2");
    CHECK(seen(sc) == "((join \"player-2\" rejoin))");
    CHECK_EQ(static_cast<int>(server.session->serverHeldCount()), 0);
    const NetTravelRecord* record = findNetRejoin(server.world, "player-2");
    CHECK(record != nullptr && record->size() == 1 && record->front().first == "health");
    CHECK(record != nullptr && record->front().second.integer == 42);
    CHECK(healthOf(server.world, "player-2", health) == 100);
    CHECK_FALSE(back.world.get<NetSessionLimits>().joinable);

    s7_eval_c_string(sc, "(set! seen '())");
    Side twin;
    startClient(twin, network, hash, aliceKey);
    frames(network, server, {&back, &twin}, 10);
    CHECK(back.droppedReason == "joined again");
    CHECK(twin.session->clientConnected());
    CHECK(twin.session->clientPlayer() == "player-2");
    CHECK(seen(sc) == "((leave \"player-2\" \"joined again\") (join \"player-2\" rejoin))");
    CHECK_EQ(static_cast<int>(server.session->serverClientCount()), 1);

    server.world.get_mut<NetSessionLimits>().joinable = true;
    s7_eval_c_string(sc, "(set! seen '())");
    server.session->serverMapLoading();
    SecureKey bobKey{};
    bobKey[0] = 9;
    Side bob;
    startClient(bob, network, hash, bobKey);
    frames(network, server, {&twin, &bob}, 400);
    CHECK(bob.droppedReason.empty());
    CHECK(seen(sc) == "()");
    CHECK_EQ(static_cast<int>(server.session->serverClientCount()), 1);
    server.session->serverMapLoaded();
    frames(network, server, {&twin, &bob}, 10);
    CHECK(bob.session->clientConnected());
    CHECK(bob.session->clientPlayer() == "player-3");
    CHECK(seen(sc) == "((join \"player-3\" open))");
    CHECK_EQ(static_cast<int>(twin.world.get<NetRoster>().entries.size()), 2);

    SecureKey carolKey{};
    carolKey[0] = 11;
    NetSessionConfig quickConfig = config(server, "server-2", "", hash);
    quickConfig.rejoinHoldTicks = 0;
    quickConfig.firstClientTakesMapPlayer = false;
    Side quick;
    prepare(quick);
    quick.session = std::make_unique<NetSession>(quick.world, std::move(quickConfig), network.endpoint());
    quick.session->registerSystems();
    CHECK(quick.session->start());
    quick.session->serverMapLoaded();
    Side carol;
    prepare(carol);
    NetSessionConfig carolConfig = config(carol, "", "server-2", hash);
    carolConfig.identitySecret = carolKey;
    carol.session = std::make_unique<NetSession>(carol.world, std::move(carolConfig), network.endpoint());
    carol.session->registerSystems();
    CHECK(carol.session->start());
    frames(network, quick, {&carol}, 10);
    CHECK(carol.session->clientConnected());
    carol.session.reset();
    frames(network, quick, {}, 5);
    CHECK_EQ(static_cast<int>(quick.session->serverHeldCount()), 0);

    clearHookRegistry(sc);
    netSchema().clear();
    s7_quit(sc);
}

}
