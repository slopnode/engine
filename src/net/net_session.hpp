#pragma once

#include "core/sim_clock.hpp"
#include "net/net_protocol.hpp"
#include "net/secure_transport.hpp"

#include <flecs.h>
#include <raylib.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
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
    std::filesystem::path knownServersPath;
    NetHello hello;
    std::string sessionKind;
    int maxPlayers = 8;
    int playerCeiling = 8;
    std::size_t maxPendingClients = 8;
    std::uint64_t helloTimeoutTicks = 300;
    double messagesPerSecond = 120.0;
    double messageBurst = 2400.0;
    std::size_t maxKnownEntities = 512;
    float nearRadius = 16.0f;
    std::uint64_t snapshotIntervalTicks = 2;
    std::size_t snapshotBudgetBytes = 1200;
    std::uint64_t interpolationDelayTicks = 6;
    float statsSeconds = 0.0f;
    bool hostPlayerVisible = false;
    bool firstClientTakesMapPlayer = true;
    std::function<void(const std::string& mapId, const std::string& reason)> loadMap;
    std::function<void(const std::string& reason)> disconnected;
    std::function<float(flecs::entity entity)> clipDuration;
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
    int serverPlayerCount() const;
    std::size_t serverPendingCount() const;
    int serverPlayerLimit() const;
    bool serverHasClient() const { return serverClientCount() > 0; }
    const SecureKey& publicKey() const { return transport_->publicKey(); }

    void registerSystems();
    void serverPoll();
    void serverMapLoaded();
    void clientFrame(float frameDt);
    float clientSecondsUntilCommand() const { return kSimTickDt - commandClock_.accumulator; }

private:
    struct SentState {
        NetPose pose;
        bool hasAnimation = false;
        std::string clip;
        float time = 0.0f;
        float speed = 1.0f;
        bool playing = false;
        bool loop = false;
        std::uint64_t tick = 0;
        std::vector<std::pair<int, NetValue>> fields;
    };

    struct ClientEntity {
        bool acked = false;
        SentState ackedState;
        std::uint64_t lastSentTick = 0;
        float priority = 0.0f;
    };

    struct SentSnapshot {
        std::uint64_t tick = 0;
        std::vector<std::pair<std::uint32_t, SentState>> entities;
        std::vector<std::uint32_t> clips;
    };

    struct ServerClient {
        NetConnectionId connection = kNoNetConnection;
        bool ready = false;
        std::uint64_t arrivedTick = 0;
        std::chrono::steady_clock::time_point refilled{};
        double tokens = 0.0;
        std::string player;
        bool ownBody = false;
        std::uint64_t lastCommandTick = 0;
        std::deque<NetCommandFrame> pending;
        NetCommandFrame latest;
        std::set<std::uint32_t> known;
        std::map<std::uint32_t, std::uint64_t> lastRelevant;
        Vector3 lastEye{};
        bool hadEye = false;
        bool loggedKnown = false;
        std::map<std::uint32_t, ClientEntity> entities;
        std::deque<SentSnapshot> sent;
        std::map<std::string, std::uint32_t> clipIds;
        std::set<std::uint32_t> ackedClips;
        std::uint64_t ackedSnapshot = 0;
        std::uint64_t statSnapshots = 0;
        std::uint64_t statBytes = 0;
        std::uint64_t statEntities = 0;
        std::uint64_t statDeferred = 0;
        std::uint64_t statAcks = 0;
        std::uint64_t statAckLag = 0;
    };

    struct EntityMotion {
        NetPose pose;
        std::uint64_t changed = 0;
        std::uint64_t restFrom = 0;
        std::uint64_t moveStart = 0;
    };

    struct ClientSample {
        std::uint64_t tick = 0;
        Vector3 position{};
        Quaternion rotation{0.0f, 0.0f, 0.0f, 1.0f};
        bool hasAnimation = false;
        std::string clip;
        float time = 0.0f;
        float speed = 1.0f;
        bool playing = false;
        bool loop = false;
    };

    struct EntityPlace {
        Vector3 at{};
        std::int32_t leaf = -1;
        bool always = false;
    };

    void serverReceive(const NetEvent& event);
    void serverHello(ServerClient& client, const std::vector<std::byte>& data);
    void serverApplyCommands();
    void serverAfterTick();
    void serverSendMap(ServerClient& client);
    void serverSendEvents(ServerClient& client, const std::vector<NetEventMessage>& events);
    void serverSendEventBatch(NetConnectionId connection, std::span<const NetEventMessage> events,
                              const std::vector<NetEventDef>& defs, NetSendMode mode);
    void serverUpdateKnown(ServerClient& client);
    void serverSendSnapshot(ServerClient& client);
    void serverAcknowledge(ServerClient& client, std::uint64_t tick);
    void serverForgetSent(ServerClient& client, const std::vector<std::uint32_t>& gone);
    void serverTrackMotion();
    bool serverEntityState(flecs::entity entity, const std::vector<const NetField*>& fields, SentState& out) const;
    bool serverStateChanged(const ClientEntity& known, const SentState& now, flecs::entity entity) const;
    void serverDrop(NetConnectionId connection, const std::string& reason);
    void serverRemove(NetConnectionId connection, const std::string& reason);
    bool serverWithinRate(ServerClient& client);
    void serverDropLate();
    bool serverVisibleTo(const ServerClient& client, flecs::entity entity) const;
    void serverAssignIds();
    void serverPlaceEntities();
    std::int32_t serverEyeLeaf(ServerClient& client, Vector3& eye, bool& haveEye) const;
    bool serverEventPoint(const NetEventMessage& event, Vector3& at) const;

    void clientReceive(const NetEvent& event);
    void clientSnapshot(const NetSnapshot& snapshot);
    void clientPresentTimeline(float frameDt);
    void clientPresentEntity(flecs::entity entity, std::deque<ClientSample>& samples, double renderTick);
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
    bool clientMayBind(flecs::entity entity, bool player) const;
    void clientRefuse(const std::string& reason);
    void clientResetView(bool reenable);
    void clientHide(const std::string& name);
    void clientShow(flecs::entity entity);
    void clientHideUnintroduced();

    void callHook(const char* name, const std::string& argument);
    void serverLogStats();
    void clientLogStats();

    flecs::world& world_;
    NetSessionConfig config_;
    std::unique_ptr<SecureTransport> transport_;
    NetRole role_ = NetRole::None;

    std::map<NetConnectionId, ServerClient> clients_;
    std::uint32_t nextNetId_ = 1;
    int nextPlayerNumber_ = 2;
    std::map<std::uint32_t, flecs::entity_t> serverEntities_;
    std::map<std::uint32_t, EntityPlace> places_;
    std::map<std::uint32_t, EntityMotion> motion_;
    std::uint64_t serverTick_ = 0;

    NetConnectionId server_ = kNoNetConnection;
    bool connected_ = false;
    std::string yourPlayer_;
    std::string expectedMap_;
    std::map<std::uint32_t, NetIntroduction> names_;
    std::map<std::uint32_t, flecs::entity_t> bound_;
    std::set<std::uint32_t> appeared_;
    std::uint64_t lastSnapshotTick_ = 0;
    std::uint64_t ackSnapshot_ = 0;
    double clock_ = 0.0;
    bool haveClock_ = false;
    std::map<std::uint32_t, std::deque<ClientSample>> history_;
    std::deque<std::pair<std::uint64_t, Vector3>> eyeHistory_;
    std::map<std::uint32_t, std::string> clipNames_;
    std::uint64_t serverStatTicks_ = 0;
    float clientStatSeconds_ = 0.0f;
    std::uint64_t statSnapshots_ = 0;
    std::uint64_t statBytes_ = 0;
    std::uint64_t statEntities_ = 0;
    std::size_t statMostEntities_ = 0;
    std::uint64_t statMissed_ = 0;
    std::uint64_t statInterval_ = 0;
    std::uint64_t statFrames_ = 0;
    std::uint64_t statStarved_ = 0;
    std::uint64_t statCorrections_ = 0;
    float statWorstCorrection_ = 0.0f;
    bool loggedSnapshots_ = false;
    SimClock commandClock_{};
    std::uint64_t commandTick_ = 0;
    std::deque<NetCommandFrame> recentCommands_;
    std::deque<PredictedMove> predicted_;
    std::map<std::string, std::vector<flecs::entity_t>> hidden_;
    bool hidOnLoad_ = false;
    bool predicting_ = false;
    Vector3 correction_{};
};

enum class KnownServer {
    New,
    Same,
    Changed,
    Unsaved,
};

KnownServer checkKnownServer(const std::filesystem::path& path, const std::string& address, const SecureKey& key);
std::string secureKeyHex(const SecureKey& key);
bool loadOrCreateServerKey(const std::filesystem::path& path, SecureKey& secret);
bool parseSecureKeyHex(std::string_view text, SecureKey& out);
std::string netAvatarName(std::string_view player);

}
