#include "test_assert.hpp"

#include "camera/components.hpp"
#include "core/package.hpp"
#include "core/vfs.hpp"
#include "game/sim_interpolation.hpp"
#include "game/sim_pipeline.hpp"
#include "input/input_command.hpp"
#include "input/input_context.hpp"
#include "input/input_state.hpp"
#include "input/player_command.hpp"
#include "map/bsp.hpp"
#include "map/pvs.hpp"
#include "net/memory_transport.hpp"
#include "net/net_session.hpp"
#include "net/net_state.hpp"
#include "physics/components.hpp"
#include "render/components.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace slopengine {

namespace {

struct HardenSide {
    flecs::world world;
    std::unique_ptr<NetSession> session;
};

void prepareHardenSide(HardenSide& side) {
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
    side.world.entity("crate").add<Actor>().set<LocalTransformation>({});
}

NetSessionConfig hardenConfig(std::string listen, std::string connect, std::uint64_t hash) {
    NetSessionConfig out;
    out.listenAddress = std::move(listen);
    out.connectAddress = std::move(connect);
    out.hello.engineVersion = "test";
    out.hello.schemaHash = hash;
    out.hello.packages = {{"game", "1.0.0"}};
    return out;
}

bool sawDisconnect(NetTransport& transport) {
    std::vector<NetEvent> events;
    transport.poll(events);
    for (const NetEvent& event : events) {
        if (event.kind == NetEventKind::Disconnected) {
            return true;
        }
    }
    return false;
}

struct FakeServer {
    std::unique_ptr<SecureTransport> transport;
    NetConnectionId client = kNoNetConnection;
    bool hello = false;

    void poll() {
        std::vector<NetEvent> events;
        transport->poll(events);
        for (const NetEvent& event : events) {
            if (event.kind == NetEventKind::Incoming) {
                transport->accept(event.connection);
                client = event.connection;
            } else if (event.kind == NetEventKind::Message) {
                hello = true;
            }
        }
    }
};

FakeServer makeFakeServer(MemoryNetwork& network) {
    SecureTransportOptions options;
    SecureKey secret{};
    generateSecureKey(secret);
    options.staticSecret = secret;
    FakeServer out{createSecureTransport(network.endpoint(), options)};
    out.transport->listen("evil");
    return out;
}

void runSlotTests(std::uint64_t hash) {
    MemoryNetwork network;
    HardenSide server;
    prepareHardenSide(server);
    NetSessionConfig config = hardenConfig("server", "", hash);
    config.maxPlayers = 2;
    config.maxPendingClients = 2;
    config.helloTimeoutTicks = 30;
    server.session = std::make_unique<NetSession>(server.world, std::move(config), network.endpoint());
    server.session->registerSystems();
    CHECK(server.session->start());
    server.session->serverMapLoaded();

    auto tick = [&](int count) {
        for (int i = 0; i < count; ++i) {
            server.session->serverPoll();
            server.world.get_mut<SimClock>().tick += 1;
            server.world.run_pipeline(server.world.get<FramePipelines>().sim, kSimTickDt);
            network.advance(kSimTickDt);
        }
    };

    std::vector<std::unique_ptr<NetTransport>> squatters;
    for (int i = 0; i < 3; ++i) {
        squatters.push_back(network.endpoint());
        squatters.back()->connect("server");
    }
    tick(5);
    CHECK_FALSE(sawDisconnect(*squatters[0]));
    CHECK_FALSE(sawDisconnect(*squatters[1]));
    CHECK(sawDisconnect(*squatters[2]));
    CHECK_EQ(server.session->serverPlayerCount(), 0);

    tick(40);
    CHECK(sawDisconnect(*squatters[0]));
    CHECK(sawDisconnect(*squatters[1]));

    HardenSide client;
    prepareHardenSide(client);
    client.session = std::make_unique<NetSession>(client.world, hardenConfig("", "server", hash), network.endpoint());
    client.session->registerSystems();
    CHECK(client.session->start());
    for (int i = 0; i < 10; ++i) {
        tick(1);
        client.session->clientFrame(kSimTickDt);
    }
    CHECK(client.session->clientConnected());
    CHECK_EQ(server.session->serverClientCount(), std::size_t{1});

    std::unique_ptr<SecureTransport> flood = createSecureTransport(network.endpoint());
    const NetConnectionId floodId = flood->connect("server");
    for (int i = 0; i < 10; ++i) {
        tick(1);
        std::vector<NetEvent> events;
        flood->poll(events);
    }
    NetHello hello;
    hello.engineVersion = "test";
    hello.schemaHash = hash;
    hello.packages = {{"game", "1.0.0"}};
    CHECK(flood->send(floodId, encodeNetHello(hello), NetSendMode::Reliable));
    tick(5);
    CHECK_EQ(server.session->serverClientCount(), std::size_t{2});
    for (std::uint64_t i = 1; i <= 3000; ++i) {
        NetCommandFrame frame;
        frame.tick = i;
        flood->send(floodId, encodeNetCommands({frame}, 0), NetSendMode::Reliable);
    }
    tick(5);
    CHECK_EQ(server.session->serverClientCount(), std::size_t{1});
}

void runEvilServerTests(std::uint64_t hash) {
    MemoryNetwork network;
    FakeServer evil = makeFakeServer(network);
    HardenSide client;
    prepareHardenSide(client);
    bool loaded = false;
    NetSessionConfig config = hardenConfig("", "evil", hash);
    config.loadMap = [&loaded](const std::string&, const std::string&) { loaded = true; };
    client.session = std::make_unique<NetSession>(client.world, std::move(config), network.endpoint());
    CHECK(client.session->start());
    auto frame = [&](int count) {
        for (int i = 0; i < count; ++i) {
            evil.poll();
            client.session->clientFrame(kSimTickDt);
            network.advance(kSimTickDt);
        }
    };
    frame(10);
    CHECK(evil.hello && client.session->clientConnected());

    evil.transport->send(evil.client, encodeNetMapChange({"testmap", "fresh", "player-2"}), NetSendMode::Reliable);
    frame(3);
    CHECK(loaded && client.session->clientConnected());
    evil.transport->send(
        evil.client,
        encodeNetIntroduce({{1, "Player", NetEntityKind::Thing}, {2, "crate", NetEntityKind::Thing}}),
        NetSendMode::Reliable);
    evil.transport->send(evil.client, encodeNetForget({1, 2}), NetSendMode::Reliable);
    frame(3);
    CHECK(client.session->clientConnected());
    CHECK_FALSE(client.world.lookup("Player").has(flecs::Disabled));
    CHECK(client.world.lookup("crate").has(flecs::Disabled));

    evil.transport->send(evil.client, encodeNetIntroduce({{3, "#1", NetEntityKind::Thing}}), NetSendMode::Reliable);
    frame(3);
    CHECK_FALSE(client.session->clientConnected());
    CHECK_FALSE(client.world.lookup("crate").has(flecs::Disabled));

    MemoryNetwork second;
    FakeServer traversal = makeFakeServer(second);
    HardenSide victim;
    prepareHardenSide(victim);
    bool traversed = false;
    NetSessionConfig victimConfig = hardenConfig("", "evil", hash);
    victimConfig.loadMap = [&traversed](const std::string&, const std::string&) { traversed = true; };
    victim.session = std::make_unique<NetSession>(victim.world, std::move(victimConfig), second.endpoint());
    CHECK(victim.session->start());
    for (int i = 0; i < 10; ++i) {
        traversal.poll();
        victim.session->clientFrame(kSimTickDt);
        second.advance(kSimTickDt);
    }
    traversal.transport->send(
        traversal.client, encodeNetMapChange({"../../outside/map", "fresh", "player-2"}), NetSendMode::Reliable);
    for (int i = 0; i < 3; ++i) {
        traversal.poll();
        victim.session->clientFrame(kSimTickDt);
        second.advance(kSimTickDt);
    }
    CHECK_FALSE(traversed);
    CHECK_FALSE(victim.session->clientConnected());
}

void runLeftStateTests(std::uint64_t hash) {
    MemoryNetwork network;
    HardenSide server;
    prepareHardenSide(server);
    server.world.set<MapPvs>({});
    server.world.lookup("Player").remove<Lens>();
    server.session = std::make_unique<NetSession>(server.world, hardenConfig("server", "", hash), network.endpoint());
    server.session->registerSystems();
    CHECK(server.session->start());
    server.session->serverMapLoaded();

    auto client = std::make_unique<HardenSide>();
    prepareHardenSide(*client);
    client->session = std::make_unique<NetSession>(client->world, hardenConfig("", "server", hash), network.endpoint());
    client->session->registerSystems();
    CHECK(client->session->start());
    auto frame = [&](int count) {
        for (int i = 0; i < count; ++i) {
            server.session->serverPoll();
            server.world.get_mut<SimClock>().tick += 1;
            server.world.run_pipeline(server.world.get<FramePipelines>().sim, kSimTickDt);
            if (client) {
                client->session->clientFrame(kSimTickDt);
            }
            network.advance(kSimTickDt);
        }
    };
    frame(10);
    CHECK(client->session->clientConnected());
    CHECK(client->world.lookup("crate").has(flecs::Disabled));

    server.world.lookup("Player").set<Lens>({});
    frame(5);
    CHECK_FALSE(client->world.lookup("crate").has(flecs::Disabled));
    server.world.lookup("Player").remove<Lens>();
    frame(90);
    CHECK_FALSE(client->world.lookup("crate").has(flecs::Disabled));

    const flecs::entity mapPlayer = server.world.lookup("Player");
    mapPlayer.get_mut<LocalTransformation>().position = {9.0f, 0.0f, 9.0f};
    mapPlayer.get_mut<PlayerCommand>().moveForward = 1.0f;
    NetPlayerRecord* record = nullptr;
    for (NetPlayerRecord& entry : server.world.get_mut<NetStore>().players) {
        if (entry.id == kLocalNetPlayerId) {
            record = &entry;
        }
    }
    CHECK(record != nullptr && !record->values.empty());
    if (record != nullptr && !record->values.empty()) {
        record->values[0].integer = 42;
    }
    client.reset();
    frame(5);
    CHECK_EQ(server.session->serverClientCount(), std::size_t{0});
    CHECK(mapPlayer.get<LocalTransformation>().position.x == 0.0f);
    CHECK(mapPlayer.get<PlayerCommand>().moveForward == 0.0f);
    CHECK(findNetPlayer(server.world, kLocalNetPlayerId)->values[0].integer == 0);
}

void runKnownServerTests() {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "slop-known-servers-test";
    std::filesystem::remove(path);
    SecureKey first{};
    SecureKey other{};
    generateSecureKey(first);
    generateSecureKey(other);
    CHECK(checkKnownServer(path, "a:1", first) == KnownServer::New);
    CHECK(checkKnownServer(path, "a:1", first) == KnownServer::Same);
    CHECK(checkKnownServer(path, "a:1", other) == KnownServer::Changed);
    CHECK(checkKnownServer(path, "b:1", other) == KnownServer::New);
    CHECK(checkKnownServer(path, "b:1", other) == KnownServer::Same);
    std::filesystem::remove(path);

    const std::filesystem::path keyPath = std::filesystem::temp_directory_path() / "slop-key-mode-test" / "server.key";
    std::filesystem::remove_all(keyPath.parent_path());
    SecureKey created{};
    SecureKey loaded{};
    CHECK(loadOrCreateServerKey(keyPath, created));
    CHECK(loadOrCreateServerKey(keyPath, loaded) && loaded == created);
#ifndef _WIN32
    const std::filesystem::perms mode = std::filesystem::status(keyPath).permissions();
    CHECK((mode & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) ==
          std::filesystem::perms::none);
#endif
    std::filesystem::remove_all(keyPath.parent_path());

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "slop-vfs-escape-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "game" / "textures" / "walls");
    std::ofstream(root / "game" / "textures" / "walls" / "brick.png") << "x";
    std::ofstream(root / "outside.png") << "x";
    VirtualFileSystem vfs;
    vfs.setBasePackage(Package(root / "game"));
    CHECK(vfs.exists(AssetKind::Texture, "walls/brick"));
    CHECK(vfs.exists(AssetKind::Texture, "walls/../walls/brick"));
    CHECK_FALSE(vfs.exists(AssetKind::Texture, "../../outside"));
    CHECK_FALSE(vfs.exists(AssetKind::Texture, "walls/../../../outside"));
    std::filesystem::remove_all(root);
}

} // namespace

void runNetHardeningTests() {
    netSchema().clear();
    netSchema().finalize();
    const std::uint64_t hash = netSchema().hash();
    runSlotTests(hash);
    runEvilServerTests(hash);
    runKnownServerTests();

    s7_scheme* sc = s7_init();
    std::vector<std::string> messages;
    netSchema().clear();
    netSchema().addPackageFields(
        sc, s7_eval_c_string(sc, "'((player (score (type int 0 99))))"), "game", PackageRole::Base, messages);
    netSchema().finalize();
    runLeftStateTests(netSchema().hash());
    netSchema().clear();
    s7_quit(sc);
}

}
