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
#include "physics/motored_body.hpp"
#include "physics/ragdoll_rig.hpp"
#include "render/components.hpp"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace slopengine {

namespace {

struct SpawnSide {
    flecs::world world;
    std::unique_ptr<NetSession> session;
    std::vector<std::string> spawned;
    std::vector<std::pair<NetEffect, bool>> effects;
    std::vector<std::size_t> corpses;
    std::string dropped;
};

void prepareSpawnSide(SpawnSide& side) {
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
    side.world.entity("walker").add<Actor>().set<LocalTransformation>({}).set<GlobalTransformation>({});
}

NetSessionConfig spawnConfig(SpawnSide& side, std::string listen, std::string connect, std::uint64_t hash) {
    NetSessionConfig out;
    out.listenAddress = std::move(listen);
    out.connectAddress = std::move(connect);
    out.hello.engineVersion = "test";
    out.hello.schemaHash = hash;
    out.hello.packages = {{"game", "1.0.0"}};
    out.snapshotIntervalTicks = 1;
    out.interpolationDelayTicks = 0;
    out.disconnected = [&side](const std::string& reason) { side.dropped = reason; };
    out.spawnPresentation = [&side](const std::string& name, const NetSpawnRecipe& recipe) {
        float position[3]{};
        float rotation[4]{};
        unpackNetPose(recipe.pose, position, rotation);
        LocalTransformation local{};
        local.position = {position[0], position[1], position[2]};
        side.spawned.push_back(name + ":" + recipe.path);
        flecs::entity entity = side.world.entity(name.c_str()).add<NetSpawned>().set<LocalTransformation>(local);
        if (recipe.kind == NetSpawnKind::Actor) {
            entity.add<Actor>();
        }
        return entity;
    };
    out.applyEffect = [&side](flecs::entity entity, const NetEffect& effect, bool makePiece) {
        side.effects.emplace_back(effect, makePiece);
        if (effect.kind == NetEffectKind::Ragdoll) {
            entity.set<RagdollRig>({});
        }
    };
    out.corpsePose = [](flecs::entity) { return std::vector<NetPose>(3); };
    out.applyCorpse = [&side](flecs::entity, const std::vector<NetPose>& bones) { side.corpses.push_back(bones.size()); };
    return out;
}

} // namespace

void runNetSpawnTests() {
    netSchema().clear();
    netSchema().finalize();
    const std::uint64_t hash = netSchema().hash();

    MemoryNetwork network;
    SpawnSide server;
    prepareSpawnSide(server);
    server.session = std::make_unique<NetSession>(server.world, spawnConfig(server, "server", "", hash), network.endpoint());
    server.session->registerSystems();
    CHECK(server.session->start());
    server.session->serverMapLoaded();

    SpawnSide client;
    prepareSpawnSide(client);
    client.session = std::make_unique<NetSession>(client.world, spawnConfig(client, "", "server", hash), network.endpoint());
    client.session->registerSystems();
    CHECK(client.session->start());

    auto frames = [&](int count) {
        for (int i = 0; i < count; ++i) {
            server.session->serverPoll();
            server.world.get_mut<SimClock>().tick += 1;
            server.world.run_pipeline(server.world.get<FramePipelines>().sim, kSimTickDt);
            client.session->clientFrame(kSimTickDt);
            network.advance(kSimTickDt);
        }
    };
    frames(10);
    CHECK(client.session->clientConnected());

    LocalTransformation at{};
    at.position = {3.0f, 1.0f, 2.0f};
    const flecs::entity rocket = server.world.entity("rocket-1")
                                     .set<LocalTransformation>(at)
                                     .set<MotoredBody>({})
                                     .set<NetSpawnRecipe>({NetSpawnKind::Motored, false, "weapons/rocket", {}, {}});
    server.world.entity("zombie-1")
        .add<Actor>()
        .set<LocalTransformation>(at)
        .set<NetSpawnRecipe>({NetSpawnKind::Actor, false, "zombie/zombie", {"zombie"}, {}});
    frames(5);
    CHECK(client.spawned.size() == 2);
    const flecs::entity clientRocket = client.world.lookup("rocket-1");
    CHECK(clientRocket.is_valid() && clientRocket.has<NetSpawned>());
    CHECK(clientRocket.is_valid() && std::fabs(clientRocket.get<LocalTransformation>().position.x - 3.0f) < 0.01f);
    rocket.get_mut<LocalTransformation>().position.x = 6.0f;
    frames(5);
    CHECK(std::fabs(clientRocket.get<LocalTransformation>().position.x - 6.0f) < 0.01f);
    CHECK(client.world.lookup("zombie-1").is_valid() && client.world.lookup("zombie-1").has<Actor>());

    const flecs::entity serverWalker = server.world.lookup("walker");
    serverWalker.set<NetSevered>({{"arm.L"}, {"vote_up"}});
    NetEffect sever;
    sever.kind = NetEffectKind::Sever;
    sever.bone = "arm.L";
    sever.push[0] = 1.0f;
    sever.speed = 3.0f;
    queueNetEffect(server.world, serverWalker, sever);
    frames(3);
    CHECK(client.effects.size() == 1 && client.effects[0].second && client.effects[0].first.bone == "arm.L");
    CHECK(client.effects.size() == 1 && client.effects[0].first.push[0] == 1.0f);
    serverWalker.get_mut<LocalTransformation>().position.x = 2.0f;
    frames(3);
    const flecs::entity clientWalker = client.world.lookup("walker");
    CHECK(clientWalker.has<SimPose>());
    NetEffect ragdoll;
    ragdoll.kind = NetEffectKind::Ragdoll;
    queueNetEffect(server.world, serverWalker, ragdoll);
    serverWalker.get_mut<LocalTransformation>().position.x = 4.0f;
    frames(3);
    CHECK(clientWalker.has<RagdollRig>() && !clientWalker.has<SimPose>());
    clientWalker.get_mut<LocalTransformation>().position.x = -7.0f;
    serverWalker.get_mut<LocalTransformation>().position.x = 5.0f;
    frames(3);
    client.world.progress(kSimTickDt);
    CHECK(clientWalker.get<LocalTransformation>().position.x == -7.0f);

    server.world.component<RagdollRig>();
    serverWalker.set<RagdollRig>({});
    rocket.destruct();
    frames(5);
    CHECK_FALSE(client.world.lookup("rocket-1").is_valid());

    SpawnSide late;
    prepareSpawnSide(late);
    late.session = std::make_unique<NetSession>(late.world, spawnConfig(late, "", "server", hash), network.endpoint());
    late.session->registerSystems();
    CHECK(late.session->start());
    for (int i = 0; i < 12; ++i) {
        server.session->serverPoll();
        server.world.get_mut<SimClock>().tick += 1;
        server.world.run_pipeline(server.world.get<FramePipelines>().sim, kSimTickDt);
        client.session->clientFrame(kSimTickDt);
        late.session->clientFrame(kSimTickDt);
        network.advance(kSimTickDt);
    }
    CHECK(late.session->clientConnected());
    bool cutOnJoin = false;
    for (const auto& [effect, makePiece] : late.effects) {
        cutOnJoin = cutOnJoin || (effect.kind == NetEffectKind::Sever && effect.bone == "arm.L" && !makePiece);
    }
    CHECK(cutOnJoin);
    bool hiddenOnJoin = false;
    for (const auto& [effect, makePiece] : late.effects) {
        hiddenOnJoin = hiddenOnJoin || (effect.kind == NetEffectKind::Hide && effect.bone == "vote_up");
    }
    CHECK(hiddenOnJoin);
    bool ragdolledOnJoin = false;
    for (const auto& [effect, makePiece] : late.effects) {
        ragdolledOnJoin = ragdolledOnJoin || effect.kind == NetEffectKind::Ragdoll;
    }
    CHECK(late.corpses.size() == 1 && late.corpses[0] == 3);
    CHECK_FALSE(ragdolledOnJoin);
    CHECK(late.world.lookup("zombie-1").is_valid());

    server.world.entity("evil-1")
        .add<Actor>()
        .set<LocalTransformation>(at)
        .set<NetSpawnRecipe>({NetSpawnKind::Actor, false, "../../etc/passwd", {}, {}});
    frames(5);
    CHECK(client.dropped.find("bad spawn") != std::string::npos);
    netSchema().clear();
}

}
