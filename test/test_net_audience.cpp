#include "test_assert.hpp"

#include "camera/components.hpp"
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
#include "script/net_script.hpp"
#include "script/script_context.hpp"

#include <s7.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace slopengine {

namespace {

struct AudienceSide {
    flecs::world world;
    std::unique_ptr<NetSession> session;
};

void prepareAudienceSide(AudienceSide& side) {
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

void splitMapAtX(flecs::world& world) {
    BspTree tree;
    tree.leaves.resize(2);
    BspNode node;
    node.plane.normal = {1.0f, 0.0f, 0.0f};
    node.plane.distance = 0.0f;
    node.front = bspEncodeLeaf(0);
    node.back = bspEncodeLeaf(1);
    tree.nodes.push_back(node);
    tree.root = 0;
    tree.boundsMins = {-100.0f, -100.0f, -100.0f};
    tree.boundsMaxs = {100.0f, 100.0f, 100.0f};
    world.set<MapBsp>({std::move(tree)});
    PvsFile pvs;
    pvs.leafCount = 2;
    pvs.columnCount = 2;
    pvs.wordsPerRow = 1;
    pvs.leafRow = {0, 1};
    pvs.leafColumn = {0, 1};
    pvs.rows = {1u, 2u};
    world.set<MapPvs>({std::move(pvs)});
}

NetSessionConfig audienceConfig(std::string listen, std::string connect, std::uint64_t hash) {
    NetSessionConfig out;
    out.listenAddress = std::move(listen);
    out.connectAddress = std::move(connect);
    out.hello.engineVersion = "test";
    out.hello.schemaHash = hash;
    out.hello.packages = {{"game", "1.0.0"}};
    return out;
}

NetEventMessage boomAt(float x) {
    NetEventMessage event;
    event.eventId = static_cast<std::uint32_t>(netSchema().findEvent("boom")->id);
    event.where = NetEventWhere::Point;
    event.point[0] = x;
    event.point[1] = 1.0f;
    return event;
}

} // namespace

void runNetAudienceTests() {
    s7_scheme* sc = s7_init();
    std::vector<std::string> messages;
    netSchema().clear();
    netSchema().addPackageEvents(sc, s7_eval_c_string(sc, "'((boom (to pvs)))"), "game", PackageRole::Base, messages);
    netSchema().finalize();
    CHECK(messages.empty());
    const std::uint64_t hash = netSchema().hash();

    MemoryNetwork network;
    AudienceSide server;
    prepareAudienceSide(server);
    splitMapAtX(server.world);
    LocalTransformation away{};
    away.position = {3.0f, 0.0f, 0.0f};
    server.world.entity("player-9").add<PlayerBody>().set<LocalTransformation>(away);
    server.session = std::make_unique<NetSession>(server.world, audienceConfig("server", "", hash), network.endpoint());
    server.session->registerSystems();
    CHECK(server.session->start());
    server.session->serverMapLoaded();

    AudienceSide client;
    prepareAudienceSide(client);
    client.world.entity(netAvatarName("player-9").c_str()).set<LocalTransformation>({});
    client.world.set<ScriptContext>({sc});
    bindNetApi(client.world, sc);
    s7_eval_c_string(sc, "(define booms 0)");
    s7_eval_c_string(sc, "(define (on-event-boom where) (set! booms (+ booms 1)))");
    client.session = std::make_unique<NetSession>(client.world, audienceConfig("", "server", hash), network.endpoint());
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
    const auto booms = [&]() { return s7_integer(s7_name_to_value(sc, "booms")); };

    frame(20);
    CHECK(client.session->clientConnected());
    server.world.lookup("Player").get_mut<Lens>().camera.position = {5.0f, 1.0f, 0.0f};
    frame(2);

    server.world.get_mut<NetEventQueue>().pending.push_back(boomAt(8.0f));
    frame(5);
    CHECK_EQ(booms(), 1);
    server.world.get_mut<NetEventQueue>().pending.push_back(boomAt(-8.0f));
    frame(5);
    CHECK_EQ(booms(), 1);
    server.world.lookup("Player").get_mut<Lens>().camera.position = {-5.0f, 1.0f, 0.0f};
    frame(2);
    server.world.get_mut<NetEventQueue>().pending.push_back(boomAt(-8.0f));
    server.world.get_mut<NetEventQueue>().pending.push_back(boomAt(8.0f));
    frame(5);
    CHECK_EQ(booms(), 2);

    const auto known = client.session->clientKnownEntities();
    CHECK(std::none_of(known.begin(), known.end(), [](const auto& entry) { return entry.first == netAvatarName("player-9"); }));

    client.session.reset();
    server.session.reset();
    netSchema().clear();
}

}
