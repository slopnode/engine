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
#include "render/components.hpp"

#include <memory>
#include <string>

namespace slopengine {

namespace {

struct InterestSide {
    flecs::world world;
    std::unique_ptr<NetSession> session;
};

void prepareInterestSide(InterestSide& side) {
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
    for (int i = 0; i < 3; ++i) {
        LocalTransformation local{};
        local.position = {static_cast<float>(10 * (i + 1)), 0.0f, 0.0f};
        const flecs::entity actor =
            side.world.entity(("actor-" + std::to_string(i + 1)).c_str()).add<Actor>().set<LocalTransformation>(local);
        side.world.entity("held").child_of(actor).set<LocalTransformation>({});
    }
}

NetSessionConfig interestConfig(std::string listen, std::string connect, std::uint64_t hash) {
    NetSessionConfig out;
    out.listenAddress = std::move(listen);
    out.connectAddress = std::move(connect);
    out.hello.engineVersion = "test";
    out.hello.schemaHash = hash;
    out.hello.packages = {{"game", "1.0.0"}};
    return out;
}

bool hidden(const flecs::entity entity) {
    return entity.has(flecs::Disabled);
}

} // namespace

void runNetInterestTests() {
    netSchema().clear();
    netSchema().finalize();
    const std::uint64_t hash = netSchema().hash();

    MemoryNetwork network;
    InterestSide server;
    prepareInterestSide(server);
    NetSessionConfig serverConfig = interestConfig("server", "", hash);
    serverConfig.maxKnownEntities = 2;
    server.session = std::make_unique<NetSession>(server.world, std::move(serverConfig), network.endpoint());
    server.session->registerSystems();
    CHECK(server.session->start());
    CHECK(server.world.has<NetSessionLimits>() && server.world.get<NetSessionLimits>().maxPlayers == 8);
    server.session->serverMapLoaded();

    InterestSide client;
    prepareInterestSide(client);
    client.session = std::make_unique<NetSession>(client.world, interestConfig("", "server", hash), network.endpoint());
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

    frame(10);
    CHECK(client.session->clientConnected());
    const flecs::entity near = client.world.lookup("actor-1");
    const flecs::entity middle = client.world.lookup("actor-2");
    const flecs::entity far = client.world.lookup("actor-3");
    CHECK(!hidden(near) && !hidden(middle));
    CHECK(hidden(far) && hidden(client.world.lookup("actor-3::held")));

    server.world.lookup("actor-3").get_mut<LocalTransformation>().position = {5.0f, 0.0f, 0.0f};
    frame(10);
    CHECK(!hidden(far) && !hidden(client.world.lookup("actor-3::held")));
    CHECK(far.has<SimPose>() && far.get<SimPose>().cur.position.x == 5.0f);
    CHECK(hidden(middle) && !hidden(near));

    server.world.lookup("actor-3").get_mut<LocalTransformation>().position = {30.0f, 0.0f, 0.0f};
    frame(10);
    CHECK(!hidden(middle) && !hidden(near) && hidden(far));
}

}
