#pragma once

#include "core/sim_clock.hpp"
#include "net/net_protocol.hpp"
#include "net/secure_transport.hpp"

#include <flecs.h>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
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
    bool serverHasClient() const { return client_.has_value() && client_->ready; }
    const SecureKey& publicKey() const { return transport_->publicKey(); }

    void registerSystems();
    void serverPoll();
    void serverMapLoaded();
    void clientFrame(float frameDt);

private:
    struct ServerClient {
        NetConnectionId connection = kNoNetConnection;
        bool ready = false;
        std::uint64_t lastCommandTick = 0;
        std::deque<NetCommandFrame> pending;
        NetCommandFrame latest;
    };

    void serverReceive(const NetEvent& event);
    void serverHello(ServerClient& client, const std::vector<std::byte>& data);
    void serverApplyCommands();
    void serverAfterTick();
    void serverSendMap(ServerClient& client);
    void serverSendEvents(const std::vector<NetEventMessage>& events);
    void serverDrop(const std::string& reason);
    std::vector<NetIntroduction> serverAssignIds();
    std::vector<std::uint32_t> serverForgetDead();

    void clientReceive(const NetEvent& event);
    void clientSnapshot(const NetSnapshot& snapshot);
    void clientSendCommands();
    flecs::entity clientEntity(std::uint32_t netId);

    flecs::world& world_;
    NetSessionConfig config_;
    std::unique_ptr<SecureTransport> transport_;
    NetRole role_ = NetRole::None;

    std::optional<ServerClient> client_;
    std::uint32_t nextNetId_ = 1;
    std::map<std::uint32_t, flecs::entity_t> serverEntities_;
    std::uint64_t serverTick_ = 0;

    NetConnectionId server_ = kNoNetConnection;
    bool connected_ = false;
    std::map<std::uint32_t, std::string> names_;
    std::map<std::uint32_t, flecs::entity_t> bound_;
    std::uint64_t lastSnapshotTick_ = 0;
    bool loggedSnapshots_ = false;
    SimClock commandClock_{};
    std::uint64_t commandTick_ = 0;
    std::deque<NetCommandFrame> recentCommands_;
};

bool loadOrCreateServerKey(const std::filesystem::path& path, SecureKey& secret);
bool parseSecureKeyHex(std::string_view text, SecureKey& out);

}
