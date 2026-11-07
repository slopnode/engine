#include "test_assert.hpp"

#include "camera/components.hpp"
#include "game/sim_interpolation.hpp"
#include "game/sim_pipeline.hpp"
#include "input/actions.hpp"
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
#include "script/net_script.hpp"
#include "script/script_context.hpp"

#include <s7.h>

#include <cmath>
#include <string>

namespace slopengine {

namespace {

struct Side {
    flecs::world world;
    std::unique_ptr<NetSession> session;
    std::string loadedMap;
    std::string droppedReason;
    int pressTicks = 0;
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
        .set<FirstPersonController>({})
        .set<Lens>({})
        .set<CharacterMotor>({});
    AnimationPlayer animation{};
    animation.animBankPath = "bank";
    side.world.entity("walker")
        .add<Actor>()
        .set<LocalTransformation>({})
        .set<GlobalTransformation>({})
        .set<AnimationPlayer>(animation);
}

NetSessionConfig config(Side& side, std::string listen, std::string connect, std::uint64_t schemaHash) {
    NetSessionConfig out;
    out.listenAddress = std::move(listen);
    out.connectAddress = std::move(connect);
    out.hello.engineVersion = "test";
    out.hello.schemaHash = schemaHash;
    out.hello.packages = {{"engine", "1.0.0"}, {"game", "1.0.0"}};
    out.loadMap = [&side](const std::string& mapId, const std::string&) { side.loadedMap = mapId; };
    out.disconnected = [&side](const std::string& reason) { side.droppedReason = reason; };
    return out;
}

void serverTick(Side& server) {
    server.session->serverPoll();
    server.world.get_mut<SimClock>().tick += 1;
    server.world.run_pipeline(server.world.get<FramePipelines>().sim, kSimTickDt);
}

void frames(MemoryNetwork& network, Side& server, std::initializer_list<Side*> clients, int count) {
    for (int i = 0; i < count; ++i) {
        serverTick(server);
        for (Side* client : clients) {
            client->session->clientFrame(kSimTickDt);
        }
        network.advance(kSimTickDt);
    }
}

} // namespace

void runNetSessionTests() {
    s7_scheme* sc = s7_init();
    std::vector<std::string> messages;
    netSchema().clear();
    netSchema().addPackageFields(
        sc,
        s7_eval_c_string(sc, "'((player (health (type int 0 999) (default 100) (send all)) (secret (type int 0 9) (send none))))"),
        "game",
        PackageRole::Base,
        messages);
    netSchema().addPackageEvents(
        sc,
        s7_eval_c_string(sc, "'((bang (args (n (int 0 99))) (to (near 10))) (mine (to owner) (reliable #t)))"),
        "game",
        PackageRole::Base,
        messages);
    netSchema().finalize();
    const std::uint64_t hash = netSchema().hash();

    MemoryNetwork network;
    Side server;
    prepare(server);
    server.world.system("MoveWalker")
        .kind(flecs::OnUpdate)
        .run([](flecs::iter& it) {
            flecs::entity walker = it.world().lookup("walker");
            if (!walker.is_valid()) {
                return;
            }
            walker.get_mut<LocalTransformation>().position.x += 0.1f;
            AnimationPlayer& animation = walker.get_mut<AnimationPlayer>();
            animation.clipName = "Walk";
            animation.playing = true;
            animation.time += kSimTickDt;
        }).add<SimSystem>();
    server.session = std::make_unique<NetSession>(server.world, config(server, "server", "", hash), network.endpoint());
    server.session->registerSystems();
    server.world.system("CountPresses")
        .kind(flecs::PreUpdate)
        .run([&server](flecs::iter& it) {
            const PlayerCommand* command = localPlayerCommand(it.world());
            if (command != nullptr && command->pressed(Action::Interact)) {
                ++server.pressTicks;
            }
        }).add<SimSystem>();
    CHECK(server.session->start());
    server.session->serverMapLoaded();

    Side client;
    prepare(client);
    client.session = std::make_unique<NetSession>(client.world, config(client, "", "server", hash), network.endpoint());
    client.session->registerSystems();
    CHECK(client.session->start());
    frames(network, server, {&client}, 10);
    CHECK(client.session->clientConnected());
    CHECK(server.session->serverHasClient());
    CHECK(client.loadedMap == "testmap");

    const flecs::entity clientWalker = client.world.lookup("walker");
    const flecs::entity serverWalker = server.world.lookup("walker");
    CHECK(clientWalker.has<SimPose>());
    const float serverX = serverWalker.get<LocalTransformation>().position.x;
    CHECK(clientWalker.has<SimPose>() && std::fabs(clientWalker.get<SimPose>().cur.position.x - serverX) < 1.0e-4f);
    CHECK(clientWalker.get<AnimationPlayer>().clipName == "Walk" && clientWalker.get<AnimationPlayer>().playing);

    server.world.lookup("Player").get_mut<Lens>().camera.position = {1.0f, 2.0f, 3.0f};
    std::vector<NetValue>& serverValues = server.world.get_mut<NetStore>().players.front().values;
    const NetField* health = netSchema().find(NetScope{NetScopeKind::Player, {}}, "health");
    const NetField* secret = netSchema().find(NetScope{NetScopeKind::Player, {}}, "secret");
    serverValues[static_cast<std::size_t>(health->slot)].integer = 77;
    serverValues[static_cast<std::size_t>(secret->slot)].integer = 9;

    InputState& input = client.world.get_mut<InputState>();
    input.actionDown[static_cast<std::size_t>(Action::MoveForward)] = 1;
    client.world.get_mut<InputCommand>().moveForward = 1.0f;
    client.world.lookup("Player").get_mut<FirstPersonController>().yaw = 1.25f;
    input.actionPressed[static_cast<std::size_t>(Action::Interact)] = 1;
    input.latchPresses();
    input.actionPressed[static_cast<std::size_t>(Action::Interact)] = 0;
    frames(network, server, {&client}, 10);

    const flecs::entity serverPlayer = server.world.lookup("Player");
    CHECK(serverPlayer.has<PlayerCommand>() && serverPlayer.get<PlayerCommand>().moveForward == 1.0f);
    CHECK(serverPlayer.get<PlayerCommand>().down(Action::MoveForward));
    CHECK(std::fabs(serverPlayer.get<FirstPersonController>().yaw - 1.25f) < 1.0e-6f);
    CHECK_EQ(server.pressTicks, 1);
    const std::vector<NetValue>& clientValues = client.world.get<NetStore>().players.front().values;
    CHECK(clientValues[static_cast<std::size_t>(health->slot)].integer == 77);
    CHECK(clientValues[static_cast<std::size_t>(secret->slot)].integer == 0);
    const flecs::entity clientPlayer = client.world.lookup("Player");
    CHECK(clientPlayer.has<SimEyePose>() && clientPlayer.get<SimEyePose>().cur.y == 2.0f);

    client.world.set<ScriptContext>({sc});
    s7_eval_c_string(sc, "(define heard '())");
    s7_eval_c_string(sc, "(define (on-event-bang where n) (set! heard (cons n heard)))");
    s7_eval_c_string(sc, "(define (on-event-mine where) (set! heard (cons 'mine heard)))");
    const auto queue = [&](const char* name, NetEventWhere where, float x, std::vector<NetValue> args) {
        NetEventMessage event;
        event.eventId = static_cast<std::uint32_t>(netSchema().findEvent(name)->id);
        event.where = where;
        event.point[0] = x;
        event.player = kLocalNetPlayerId;
        event.args = std::move(args);
        server.world.get_mut<NetEventQueue>().pending.push_back(std::move(event));
    };
    NetValue near;
    near.kind = NetTypeKind::Int;
    near.integer = 7;
    NetValue far = near;
    far.integer = 8;
    queue("bang", NetEventWhere::Point, 2.0f, {near});
    queue("bang", NetEventWhere::Point, 500.0f, {far});
    queue("mine", NetEventWhere::Player, 0.0f, {});
    frames(network, server, {&client}, 3);
    const s7_pointer heard = s7_name_to_value(sc, "heard");
    CHECK(s7_list_length(sc, heard) == 2);
    CHECK(s7_boolean(sc, s7_eval_c_string(sc, "(and (memv 7 heard) (memq 'mine heard) (not (memv 8 heard)) #t)")));

    serverWalker.destruct();
    frames(network, server, {&client}, 3);
    const float frozen = clientWalker.get<SimPose>().cur.position.x;
    frames(network, server, {&client}, 3);
    CHECK(clientWalker.get<SimPose>().cur.position.x == frozen);

    Side late;
    prepare(late);
    late.session = std::make_unique<NetSession>(late.world, config(late, "", "server", hash), network.endpoint());
    late.session->registerSystems();
    CHECK(late.session->start());
    frames(network, server, {&client, &late}, 10);
    CHECK(late.droppedReason == "server full");
    CHECK(client.session->clientConnected());

    Side wrong;
    prepare(wrong);
    client.session.reset();
    frames(network, server, {}, 5);
    CHECK_FALSE(server.session->serverHasClient());
    wrong.session = std::make_unique<NetSession>(wrong.world, config(wrong, "", "server", hash + 1), network.endpoint());
    wrong.session->registerSystems();
    CHECK(wrong.session->start());
    frames(network, server, {&wrong}, 10);
    CHECK(wrong.droppedReason == "declared state differs");
    CHECK_FALSE(server.session->serverHasClient());

    Side pinned;
    prepare(pinned);
    NetSessionConfig pinnedConfig = config(pinned, "", "server", hash);
    SecureKey otherKey{};
    otherKey[0] = 1;
    pinnedConfig.pinnedServerKey = otherKey;
    pinned.session = std::make_unique<NetSession>(pinned.world, std::move(pinnedConfig), network.endpoint());
    CHECK(pinned.session->start());
    frames(network, server, {&pinned}, 10);
    CHECK(pinned.droppedReason == "server key mismatch");

    netSchema().clear();
    s7_quit(sc);
}

}
