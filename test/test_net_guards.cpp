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
#include "net/command_move.hpp"
#include "net/memory_transport.hpp"
#include "net/net_protocol.hpp"
#include "net/net_session.hpp"
#include "net/net_state.hpp"
#include "net/secure_transport.hpp"
#include "physics/components.hpp"
#include "physics/physics_module.hpp"
#include "physics/physics_world.hpp"
#include "render/components.hpp"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace slopengine {

namespace {

float feetY(PhysicsWorld& physics, flecs::entity body) {
    return static_cast<float>(physics.characterPosition(static_cast<std::uint64_t>(body.id())).GetY());
}

float feetX(PhysicsWorld& physics, flecs::entity body) {
    return static_cast<float>(physics.characterPosition(static_cast<std::uint64_t>(body.id())).GetX());
}

void runCommandMoveTests() {
    flecs::world world;
    PhysicsWorld physics;
    physics.addStaticBrushes({makeBrushBox("floor", {-20.0f, -0.25f, -20.0f}, {20.0f, 0.0f, 20.0f}, "mat/a", {})});
    const CharacterMotor motor{};
    const auto body = [&](const char* name, float x, float y, bool allowMove) {
        FirstPersonController controller{};
        controller.allowMove = allowMove;
        flecs::entity entity = world.entity(name).set<CharacterMotor>(motor).set<FirstPersonController>(controller);
        physics.createCharacter(static_cast<std::uint64_t>(entity.id()), x, y, 0.0f, motor);
        return entity;
    };
    const flecs::entity frozen = body("frozen", 0.0f, 3.0f, false);
    const flecs::entity normal = body("normal", 5.0f, 0.05f, true);
    const flecs::entity eager = body("eager", 10.0f, 0.05f, true);
    for (int i = 0; i < 20; ++i) {
        CHECK(stepCommandMove(physics, frozen, CommandMove{1.0f, 1.0f, 0.0f, 0.0f, nullptr, nullptr}));
        stepCommandMove(physics, normal, CommandMove{0.0f, 1.0f, 0.0f, 0.0f, nullptr, nullptr});
        stepCommandMove(physics, eager, CommandMove{0.0f, 5.0f, 0.0f, 0.0f, nullptr, nullptr});
    }
    CHECK(feetY(physics, frozen) < 2.9f);
    CHECK(std::fabs(feetX(physics, frozen)) < 1.0e-3f);
    CHECK(frozen.get<CharacterMotor>().wishX == 0.0f && frozen.get<CharacterMotor>().wishZ == 0.0f);
    CHECK(std::fabs(feetX(physics, normal) - 5.0f) > 0.1f);
    const CharacterMotor& pushed = eager.get<CharacterMotor>();
    CHECK(std::fabs(pushed.wishX * pushed.wishX + pushed.wishZ * pushed.wishZ - 1.0f) < 1.0e-3f);
    CHECK_FALSE(stepCommandMove(physics, world.entity("bodiless").set<CharacterMotor>(motor), CommandMove{}));
}

void prepareGuardSide(flecs::world& world) {
    registerSimPipelines(world);
    registerSimInterpolation(world);
    InputState input{};
    input.resize(8);
    world.set<InputState>(std::move(input));
    world.set<InputCommand>({});
    world.set<InputContextStack>({});
    world.component<LocalPlayer>();
    world.component<PlayerCommand>();
    registerNetState(world);
    world.set<CurrentMap>({"testmap"});
    world.entity("Player")
        .add<LocalPlayer>()
        .add<PlayerBody>()
        .set<LocalTransformation>({})
        .set<FirstPersonController>({})
        .set<Lens>({})
        .set<CharacterMotor>({});
}

NetSessionConfig guardConfig(std::string connect, std::string& dropped) {
    NetSessionConfig out;
    out.connectAddress = std::move(connect);
    out.hello.engineVersion = "test";
    out.hello.schemaHash = netSchema().hash();
    out.disconnected = [&dropped](const std::string& reason) { dropped = reason; };
    return out;
}

std::string clientDropReason(std::vector<std::byte> message) {
    MemoryNetwork network;
    std::unique_ptr<SecureTransport> server = createSecureTransport(network.endpoint());
    CHECK(server->listen("server"));
    flecs::world world;
    prepareGuardSide(world);
    std::string dropped;
    NetSession client(world, guardConfig("server", dropped), network.endpoint());
    client.registerSystems();
    CHECK(client.start());
    bool sent = false;
    for (int i = 0; i < 40 && dropped.empty(); ++i) {
        std::vector<NetEvent> events;
        server->poll(events);
        for (const NetEvent& event : events) {
            if (event.kind == NetEventKind::Incoming) {
                server->accept(event.connection);
            }
            if (event.kind == NetEventKind::Message && !sent) {
                server->send(event.connection, message, NetSendMode::Reliable);
                sent = true;
            }
        }
        client.clientFrame(kSimTickDt);
        network.advance(kSimTickDt);
    }
    CHECK(sent);
    return dropped;
}

void runBadMessageTests() {
    netSchema().clear();
    netSchema().finalize();
    std::vector<std::byte> forget = encodeNetForget({1, 2});
    forget.push_back(std::byte{7});
    CHECK(clientDropReason(forget).find("bad forget") != std::string::npos);
    std::vector<std::byte> events = encodeNetEvents({}, netSchema().events());
    events.push_back(std::byte{7});
    CHECK(clientDropReason(events).find("bad events") != std::string::npos);
    CHECK(clientDropReason(encodeNetForget({1, 2})).empty());
    netSchema().clear();
}

struct CatchUpSide {
    flecs::world world;
    PhysicsWorld physics;
    std::unique_ptr<NetSession> session;
};

void prepareCatchUpSide(CatchUpSide& side) {
    prepareGuardSide(side.world);
    side.physics.addStaticBrushes({makeBrushBox("floor", {-50.0f, -0.25f, -50.0f}, {50.0f, 0.0f, 50.0f}, "mat/a", {})});
    side.world.set<PhysicsContext>({&side.physics});
    const flecs::entity player = side.world.lookup("Player");
    side.physics.setPlayerId(static_cast<std::uint64_t>(player.id()));
    side.physics.createPlayerCharacter(0.0f, 0.05f, 0.0f, player.get<CharacterMotor>());
}

NetSessionConfig catchUpConfig(std::string listen, std::string connect) {
    NetSessionConfig out;
    out.listenAddress = std::move(listen);
    out.connectAddress = std::move(connect);
    out.hello.engineVersion = "test";
    out.hello.schemaHash = netSchema().hash();
    return out;
}

void runCatchUpTests() {
    netSchema().clear();
    netSchema().finalize();
    MemoryNetwork network;
    CatchUpSide server;
    prepareCatchUpSide(server);
    server.session = std::make_unique<NetSession>(server.world, catchUpConfig("server", ""), network.endpoint());
    server.session->registerSystems();
    CHECK(server.session->start());
    server.session->serverMapLoaded();
    CatchUpSide client;
    prepareCatchUpSide(client);
    client.session = std::make_unique<NetSession>(client.world, catchUpConfig("", "server"), network.endpoint());
    client.session->registerSystems();
    CHECK(client.session->start());
    const auto serverTick = [&]() {
        server.session->serverPoll();
        server.world.get_mut<SimClock>().tick += 1;
        server.world.run_pipeline(server.world.get<FramePipelines>().sim, kSimTickDt);
    };
    for (int i = 0; i < 30; ++i) {
        serverTick();
        client.session->clientFrame(kSimTickDt);
        network.advance(kSimTickDt);
    }
    CHECK(client.session->clientConnected());
    const flecs::entity body = server.world.lookup("Player");
    const auto bodyZ = [&]() {
        return static_cast<float>(server.physics.characterPosition(static_cast<std::uint64_t>(body.id())).GetZ());
    };
    client.world.get_mut<InputCommand>().moveForward = 1.0f;
    for (int i = 0; i < 20; ++i) {
        serverTick();
        client.session->clientFrame(kSimTickDt);
        network.advance(kSimTickDt);
    }
    for (int i = 0; i < 10; ++i) {
        client.session->clientFrame(kSimTickDt);
        network.advance(kSimTickDt);
    }
    const float paused = bodyZ();
    serverTick();
    CHECK(std::fabs(bodyZ() - paused) > 0.05f);
    client.session.reset();
    server.session.reset();
    netSchema().clear();
}

void runStartGuardTests() {
    MemoryNetwork network;
    flecs::world bare;
    std::string dropped;
    NetSession session(bare, guardConfig("server", dropped), network.endpoint());
    CHECK_FALSE(session.start());
}

}

void runNetGuardTests() {
    runCommandMoveTests();
    runBadMessageTests();
    runCatchUpTests();
    runStartGuardTests();
}

}
