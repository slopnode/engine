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
#include "net/net_protocol.hpp"
#include "net/net_session.hpp"
#include "net/net_state.hpp"
#include "physics/components.hpp"
#include "render/components.hpp"

#include <s7.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

namespace {

struct FieldSide {
    flecs::world world;
    std::unique_ptr<NetSession> session;
};

void prepareFieldSide(FieldSide& side) {
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

NetSessionConfig fieldConfig(std::string listen, std::string connect, std::uint64_t hash) {
    NetSessionConfig out;
    out.listenAddress = std::move(listen);
    out.connectAddress = std::move(connect);
    out.hello.engineVersion = "test";
    out.hello.schemaHash = hash;
    out.hello.packages = {{"game", "1.0.0"}};
    return out;
}

NetValue integer(std::int64_t n) {
    NetValue out;
    out.kind = NetTypeKind::Int;
    out.integer = n;
    return out;
}

std::size_t slotOf(std::string_view name) {
    return static_cast<std::size_t>(netSchema().find(NetScope{NetScopeKind::Session, {}}, name)->slot);
}

const NetValue& sessionValue(FieldSide& side, std::string_view name) {
    return side.world.get<NetStore>().session[slotOf(name)];
}

}

void runNetSessionFieldTests() {
    s7_scheme* sc = s7_init();
    std::vector<std::string> messages;
    netSchema().clear();
    netSchema().addPackageFields(
        sc,
        s7_eval_c_string(sc,
                         "'((session (round (type int 0 99) (default 1) (send all))"
                         "           (secret (type int 0 99) (default 2) (send none))"
                         "           (leader (type (string 16)) (default \"\") (send all))))"),
        "game", PackageRole::Base, messages);
    netSchema().finalize();
    CHECK(messages.empty());
    const std::uint64_t hash = netSchema().hash();
    const std::vector<const NetField*> fields = netSchema().fieldsIn(NetScope{NetScopeKind::Session, {}});
    CHECK_EQ(fields.size(), 3u);

    MemoryNetwork network;
    FieldSide server;
    prepareFieldSide(server);
    server.session = std::make_unique<NetSession>(server.world, fieldConfig("server", "", hash), network.endpoint());
    server.session->registerSystems();
    CHECK(server.session->start());
    server.session->serverMapLoaded();
    server.world.get_mut<NetStore>().session[slotOf("round")] = integer(3);

    FieldSide early;
    prepareFieldSide(early);
    early.session = std::make_unique<NetSession>(early.world, fieldConfig("", "server", hash), network.endpoint());
    early.session->registerSystems();
    CHECK(early.session->start());

    std::vector<FieldSide*> clients{&early};
    auto frame = [&](int count) {
        for (int i = 0; i < count; ++i) {
            server.session->serverPoll();
            server.world.get_mut<SimClock>().tick += 1;
            server.world.run_pipeline(server.world.get<FramePipelines>().sim, kSimTickDt);
            for (FieldSide* client : clients) {
                client->session->clientFrame(kSimTickDt);
            }
            network.advance(kSimTickDt);
        }
    };

    frame(20);
    CHECK(early.session->clientConnected());
    CHECK_EQ(sessionValue(early, "round").integer, 3);
    CHECK_EQ(sessionValue(early, "secret").integer, 2);

    NetValue leader;
    leader.kind = NetTypeKind::String;
    leader.text = "player-2";
    server.world.get_mut<NetStore>().session[slotOf("round")] = integer(4);
    server.world.get_mut<NetStore>().session[slotOf("secret")] = integer(9);
    server.world.get_mut<NetStore>().session[slotOf("leader")] = leader;
    frame(5);
    CHECK_EQ(sessionValue(early, "round").integer, 4);
    CHECK_EQ(sessionValue(early, "secret").integer, 2);
    CHECK(sessionValue(early, "leader").text == "player-2");

    FieldSide late;
    prepareFieldSide(late);
    late.session = std::make_unique<NetSession>(late.world, fieldConfig("", "server", hash), network.endpoint());
    late.session->registerSystems();
    CHECK(late.session->start());
    clients.push_back(&late);
    frame(20);
    CHECK(late.session->clientConnected());
    CHECK_EQ(sessionValue(late, "round").integer, 4);
    CHECK_EQ(sessionValue(late, "secret").integer, 2);
    CHECK(sessionValue(late, "leader").text == "player-2");

    std::vector<std::pair<int, NetValue>> decoded;
    const int round = static_cast<int>(slotOf("round"));
    const std::vector<std::byte> good = encodeNetSession({{round, integer(7)}, {static_cast<int>(slotOf("leader")), leader}}, fields);
    CHECK(decodeNetSession(good, fields, decoded) && decoded.size() == 2 && decoded[0].second.integer == 7 &&
          decoded[1].second.text == "player-2");
    std::vector<std::byte> trailing = good;
    trailing.push_back(std::byte{1});
    CHECK_FALSE(decodeNetSession(trailing, fields, decoded));
    CHECK_FALSE(decodeNetSession(encodeNetSession({{round, integer(7)}}, fields), {}, decoded));
    CHECK_FALSE(decodeNetSession(encodeNetSession({{round, integer(100)}}, fields), fields, decoded));

    late.session.reset();
    early.session.reset();
    server.session.reset();
    netSchema().clear();
    s7_quit(sc);
}

}
