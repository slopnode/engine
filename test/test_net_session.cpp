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
#include "script/player_target.hpp"
#include "script/script_context.hpp"
#include "script/script_scope.hpp"

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
        .add<PlayerBody>()
        .set<LocalTransformation>({})
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
    NetSessionConfig serverConfig = config(server, "server", "", hash);
    serverConfig.maxClients = 2;
    server.session = std::make_unique<NetSession>(server.world, std::move(serverConfig), network.endpoint());
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
    CHECK(client.session->clientPlayer() == "Player");

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

    s7_eval_c_string(sc, "(define appeared '())");
    s7_eval_c_string(sc, "(define (on-player-appear id) (set! appeared (cons id appeared)))");
    s7_eval_c_string(sc, "(define (on-player-vanish id) (set! appeared (cons (string-append \"gone \" id) appeared)))");

    CharacterMotor tall{};
    tall.eyeHeight = 1.65f;
    tall.height = 1.2f;
    server.world.lookup("Player").set<CharacterMotor>(tall);

    Side late;
    prepare(late);
    late.session = std::make_unique<NetSession>(late.world, config(late, "", "server", hash), network.endpoint());
    late.session->registerSystems();
    CHECK(late.session->start());
    frames(network, server, {&client, &late}, 10);
    CHECK(late.session->clientConnected());
    CHECK(late.session->clientPlayer() == "player-2");
    CHECK_EQ(static_cast<int>(server.session->serverClientCount()), 2);
    const flecs::entity secondBody = server.world.lookup("player-2");
    CHECK(secondBody.is_valid() && secondBody.has<PlayerBody>());
    CHECK(secondBody.has<CharacterMotor>() && secondBody.get<CharacterMotor>().eyeHeight == 1.65f);
    CHECK(secondBody.get<FirstPersonController>().eyeHeight == 1.65f);
    CHECK(findNetPlayer(server.world, "player-2") != nullptr);
    CHECK(s7_boolean(sc, s7_eval_c_string(sc, "(and (member \"player:player-2\" appeared) #t)")));

    bindScriptPlayerApi(server.world, sc);
    {
        ScriptScopeGuard worldScope(ScriptScope::World);
        CHECK(s7_t(sc) == s7_eval_c_string(sc,
            "(let ((r (list (script-player) (with-script-player \"player-2\" (lambda () (script-player))) (script-player))))"
            " (equal? r '(\"Player\" \"player-2\" \"Player\")))"));
        s7_eval_c_string(sc,
            "(define (seen-as) (dynamic-wind (lambda () #f)"
            " (lambda () (with-script-player \"player-2\" (lambda () (list (script-player) (with-script-player \"Player\" script-player) (script-player)))))"
            " (lambda () #f)))");
        const s7_pointer seen = s7_call(sc, s7_name_to_value(sc, "seen-as"), s7_nil(sc));
        CHECK(s7_t(sc) == s7_call(sc, s7_name_to_value(sc, "equal?"),
        s7_list(sc, 2, seen, s7_eval_c_string(sc, "'(\"player-2\" \"Player\" \"player-2\")"))));
        s7_eval_c_string(sc, "(define (boom-as) (catch #t (lambda () (with-script-player \"player-2\" (lambda () (error 'boom)))) (lambda args 'caught)))");
        s7_call(sc, s7_name_to_value(sc, "boom-as"), s7_nil(sc));
        CHECK(std::string(s7_string(s7_eval_c_string(sc, "(script-player)"))) == "Player");
        CHECK(s7_t(sc) == s7_eval_c_string(sc, "(let ((r (with-script-player \"nobody\" (lambda () 1)))) (not r))"));
        {
            ScriptPlayerGuard guard("player-2");
            CHECK(scriptPlayer(server.world) == secondBody);
            CHECK_FALSE(scriptPlayerIsLocal());
        }
    }
    CHECK(scriptPlayer(server.world) == server.world.lookup("Player"));

    client.world.entity("player:player-2").set<LocalTransformation>({}).set<GlobalTransformation>({});
    secondBody.get_mut<LocalTransformation>().position = {5.0f, 0.0f, 6.0f};
    late.world.get_mut<InputCommand>().moveStrafe = 1.0f;
    client.world.get_mut<InputCommand>().moveForward = 0.0f;
    client.world.get_mut<InputState>().actionDown[static_cast<std::size_t>(Action::MoveForward)] = 0;
    for (NetPlayerRecord& record : server.world.get_mut<NetStore>().players) {
        if (record.id == "player-2") {
            record.values[static_cast<std::size_t>(health->slot)].integer = 33;
        }
    }
    frames(network, server, {&client, &late}, 10);
    CHECK(secondBody.has<PlayerCommand>() && secondBody.get<PlayerCommand>().moveStrafe == 1.0f);
    CHECK(server.world.lookup("Player").get<PlayerCommand>().moveStrafe == 0.0f);
    const flecs::entity avatar = client.world.lookup("player:player-2");
    CHECK(avatar.has<SimPose>() && avatar.get<SimPose>().cur.position.z == 6.0f);
    CHECK(late.world.get<NetStore>().players.front().values[static_cast<std::size_t>(health->slot)].integer == 33);
    CHECK(client.world.get<NetStore>().players.front().values[static_cast<std::size_t>(health->slot)].integer == 77);
    CHECK_FALSE(late.world.lookup("player:Player").is_valid() && late.world.lookup("player:Player").has<SimPose>());

    Side extra;
    prepare(extra);
    extra.session = std::make_unique<NetSession>(extra.world, config(extra, "", "server", hash), network.endpoint());
    extra.session->registerSystems();
    CHECK(extra.session->start());
    frames(network, server, {&client, &late, &extra}, 10);
    CHECK(extra.droppedReason == "server full");

    late.session.reset();
    frames(network, server, {&client}, 5);
    CHECK_EQ(static_cast<int>(server.session->serverClientCount()), 1);
    CHECK_FALSE(server.world.lookup("player-2").is_valid());
    CHECK(findNetPlayer(server.world, "player-2") == nullptr);
    CHECK(s7_boolean(sc, s7_eval_c_string(sc, "(and (member \"gone player:player-2\" appeared) #t)")));
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
