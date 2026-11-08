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
#include "net/net_session.hpp"
#include "net/net_state.hpp"
#include "physics/character_wish.hpp"
#include "physics/components.hpp"
#include "physics/physics_module.hpp"
#include "physics/physics_world.hpp"
#include "render/components.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

namespace slopengine {

namespace {

struct PredictSide {
    flecs::world world;
    PhysicsWorld physics;
    std::unique_ptr<NetSession> session;
};

void preparePredictSide(PredictSide& side) {
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
    motor.moveSpeed = 6.0f;
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

NetSessionConfig predictConfig(std::string listen, std::string connect, std::uint64_t hash) {
    NetSessionConfig out;
    out.listenAddress = std::move(listen);
    out.connectAddress = std::move(connect);
    out.hello.engineVersion = "test";
    out.hello.schemaHash = hash;
    out.hello.packages = {{"game", "1.0.0"}};
    return out;
}

float feetZ(PredictSide& side) {
    const flecs::entity player = side.world.lookup("Player");
    return static_cast<float>(side.physics.characterPosition(static_cast<std::uint64_t>(player.id())).GetZ());
}

} // namespace

void runNetPredictionTests() {
    netSchema().clear();
    netSchema().finalize();
    const std::uint64_t hash = netSchema().hash();

    MemoryNetwork network;
    PredictSide server;
    preparePredictSide(server);
    bool launch = false;
    server.world.system("StepBodies")
        .kind(flecs::OnUpdate)
        .run([&server, &launch](flecs::iter& it) {
            it.world().each([&](flecs::entity entity, PlayerBody, const PlayerCommand& command, CharacterMotor& motor) {
                const std::uint64_t id = static_cast<std::uint64_t>(entity.id());
                if (launch) {
                    launch = !server.physics.launchCharacter(id, 7.7f);
                }
                applyMoveWish(motor, command.moveForward, command.moveStrafe, command.moveUp, command.yaw);
                CharacterStep step{};
                step.id = id;
                step.motor = &motor;
                server.physics.stepCharacterAlone(step);
            });
        }).add<SimSystem>();
    server.session = std::make_unique<NetSession>(server.world, predictConfig("server", "", hash), network.endpoint());
    server.session->registerSystems();
    CHECK(server.session->start());
    server.session->serverMapLoaded();

    PredictSide client;
    preparePredictSide(client);
    client.session = std::make_unique<NetSession>(client.world, predictConfig("", "server", hash), network.endpoint());
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

    network.setConditions(MemoryLinkConditions{50.0f, 0.0f, 0.0f});
    frame(60);
    CHECK(client.session->clientConnected());
    const float startZ = feetZ(client);
    CHECK(std::fabs(startZ - feetZ(server)) < 1.0e-3f);

    client.world.get_mut<InputCommand>().moveForward = 1.0f;
    frame(3);
    CHECK(feetZ(client) > startZ + 0.2f);
    CHECK(std::fabs(feetZ(server) - startZ) < 1.0e-3f);

    frame(57);
    const float lead = feetZ(client) - feetZ(server);
    CHECK(lead > 0.25f && lead < 1.2f);

    client.world.get_mut<InputCommand>().moveForward = 0.0f;
    frame(30);
    CHECK(std::fabs(feetZ(client) - feetZ(server)) < 1.0e-3f);
    for (int i = 0; i < 4; ++i) {
        client.session->clientFrame(kSimTickDt);
        network.advance(kSimTickDt);
    }
    frame(10);
    const std::size_t backlogged = client.session->clientCommandsInFlight();
    frame(150);
    CHECK(backlogged >= client.session->clientCommandsInFlight() + 2);
    CHECK(std::fabs(feetZ(client) - feetZ(server)) < 1.0e-3f);
    CHECK(feetZ(server) > startZ + 5.0f);

    network.setConditions(MemoryLinkConditions{40.0f, 15.0f, 0.05f});
    client.world.get_mut<InputCommand>().moveStrafe = 1.0f;
    frame(90);
    client.world.get_mut<InputCommand>().moveStrafe = 0.0f;
    network.setConditions(MemoryLinkConditions{40.0f, 0.0f, 0.0f});
    frame(40);
    const flecs::entity clientPlayer = client.world.lookup("Player");
    const flecs::entity serverPlayer = server.world.lookup("Player");
    const JPH::RVec3 clientFeet = client.physics.characterPosition(static_cast<std::uint64_t>(clientPlayer.id()));
    const JPH::RVec3 serverFeet = server.physics.characterPosition(static_cast<std::uint64_t>(serverPlayer.id()));
    CHECK(std::fabs(static_cast<float>(clientFeet.GetX() - serverFeet.GetX())) < 1.0e-3f);
    CHECK(std::fabs(static_cast<float>(serverFeet.GetX())) > 3.0f);

    const float floorY = static_cast<float>(clientFeet.GetY());
    launch = true;
    bool falling = false;
    bool rose = false;
    int bounces = 0;
    float lastY = floorY;
    float peak = floorY;
    float lastEye = clientPlayer.get<SimEyePose>().cur.y;
    float worstEyeStep = 0.0f;
    for (int i = 0; i < 200; ++i) {
        frame(1);
        const float eyeY = clientPlayer.get<SimEyePose>().cur.y;
        worstEyeStep = std::max(worstEyeStep, std::fabs(eyeY - lastEye));
        lastEye = eyeY;
        const float y = static_cast<float>(client.physics.characterPosition(static_cast<std::uint64_t>(clientPlayer.id())).GetY());
        peak = std::max(peak, y);
        if (y < lastY - 1.0e-4f) {
            falling = true;
        }
        if (falling && y > lastY + 1.0e-4f) {
            ++bounces;
        }
        rose = rose || y > floorY + 1.0f;
        lastY = y;
    }
    CHECK(rose && peak > floorY + 1.2f);
    CHECK_EQ(bounces, 0);
    CHECK(worstEyeStep < 0.4f);
    CHECK(std::fabs(lastY - floorY) < 1.0e-3f);
    const SimEyePose& eye = client.world.lookup("Player").get<SimEyePose>();
    CHECK(std::fabs(eye.cur.z - feetZ(client)) < 1.0e-3f && eye.cur.y > 1.5f && eye.cur.y < 1.8f);
}

}
