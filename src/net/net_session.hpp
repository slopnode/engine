#pragma once

#include "core/sim_clock.hpp"
#include "net/net_protocol.hpp"
#include "net/secure_transport.hpp"

#include <flecs.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace slopengine {

enum class NetRole {
    None,
    Server,
    Client,
};

struct NetSessionConfig {
    std::string listenAddress;
    std::string connectAddress;
    std::optional<SecureKey> pinnedServerKey;
    std::filesystem::path serverKeyPath;
    NetHello hello;
    std::size_t maxClients = 8;
    bool hostPlayerVisible = false;
    bool firstClientTakesMapPlayer = true;
    std::function<void(const std::string& mapId, const std::string& reason)> loadMap;
    std::function<void(const std::string& reason)> disconnected;
};

struct NetId {
    std::uint32_t id = 0;
};

class NetSession {
public:
    NetSession(flecs::world& world, NetSessionConfig config, std::unique_ptr<NetTransport> transport);
    ~NetSession();

    NetSession(const NetSession&) = delete;
    NetSession& operator=(const NetSession&) = delete;

    bool start();
    NetRole role() const { return role_; }
    bool clientConnected() const { return connected_; }
    const std::string& clientPlayer() const { return yourPlayer_; }
    std::size_t serverClientCount() const;
    bool serverHasClient() const { return serverClientCount() > 0; }
    const SecureKey& publicKey() const { return transport_->publicKey(); }

    void registerSystems();
    void serverPoll();
    void serverMapLoaded();
    void clientFrame(float frameDt);

private:
    struct ServerClient {
        NetConnectionId connection = kNoNetConnection;
        bool ready = false;
        std::string player;
        bool ownBody = false;
        std::uint64_t lastCommandTick = 0;
        std::deque<NetCommandFrame> pending;
        NetCommandFrame latest;
        std::set<std::uint32_t> known;
    };

    void serverReceive(const NetEvent& event);
    void serverHello(ServerClient& client, const std::vector<std::byte>& data);
    void serverApplyCommands();
    void serverAfterTick();
    void serverSendMap(ServerClient& client);
    void serverSendEvents(ServerClient& client, const std::vector<NetEventMessage>& events);
    void serverUpdateKnown(ServerClient& client);
    void serverSendSnapshot(ServerClient& client);
    void serverDrop(NetConnectionId connection, const std::string& reason);
    void serverRemove(NetConnectionId connection, const std::string& reason);
    bool serverVisibleTo(const ServerClient& client, flecs::entity entity) const;
    void serverAssignIds();

    void clientReceive(const NetEvent& event);
    void clientSnapshot(const NetSnapshot& snapshot);
    struct PredictedMove {
        std::uint64_t tick = 0;
        float moveForward = 0.0f;
        float moveStrafe = 0.0f;
        float moveUp = 0.0f;
        float yaw = 0.0f;
    };

    void clientSendCommands();
    bool clientPredictMove(const PredictedMove& move);
    void clientReconcile(const NetSnapshot& snapshot);
    void clientPresentPrediction(bool advance);
    flecs::entity clientEntity(std::uint32_t netId);
    void applyStandInFields(const NetEntityState& state);
    void clearStandInRecords();

    void callHook(const char* name, const std::string& argument);

    flecs::world& world_;
    NetSessionConfig config_;
    std::unique_ptr<SecureTransport> transport_;
    NetRole role_ = NetRole::None;

    std::map<NetConnectionId, ServerClient> clients_;
    std::uint32_t nextNetId_ = 1;
    int nextPlayerNumber_ = 2;
    std::map<std::uint32_t, flecs::entity_t> serverEntities_;
    std::uint64_t serverTick_ = 0;

    NetConnectionId server_ = kNoNetConnection;
    bool connected_ = false;
    std::string yourPlayer_;
    std::string expectedMap_;
    std::map<std::uint32_t, NetIntroduction> names_;
    std::map<std::uint32_t, flecs::entity_t> bound_;
    std::set<std::uint32_t> appeared_;
    std::uint64_t lastSnapshotTick_ = 0;
    bool loggedSnapshots_ = false;
    SimClock commandClock_{};
    std::uint64_t commandTick_ = 0;
    std::deque<NetCommandFrame> recentCommands_;
    std::deque<PredictedMove> predicted_;
    bool predicting_ = false;
};

bool loadOrCreateServerKey(const std::filesystem::path& path, SecureKey& secret);
bool parseSecureKeyHex(std::string_view text, SecureKey& out);
std::string netAvatarName(std::string_view player);

}
