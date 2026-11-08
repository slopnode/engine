#include "net/net_session.hpp"

#include "camera/components.hpp"
#include "core/log.hpp"
#include "game/sim_interpolation.hpp"
#include "input/input_command.hpp"
#include "input/input_context.hpp"
#include "input/input_state.hpp"
#include "input/player_command.hpp"
#include "map/bsp.hpp"
#include "map/pvs.hpp"
#include "map/player_bodies.hpp"
#include "map/thing.hpp"
#include "net/net_state.hpp"
#include "physics/character_wish.hpp"
#include "physics/components.hpp"
#include "physics/motor_rules.hpp"
#include "physics/motored_body.hpp"
#include "physics/physics_module.hpp"
#include "physics/physics_world.hpp"
#include "physics/rigid_mover.hpp"
#include "render/animation_player.hpp"
#include "render/components.hpp"
#include "script/proc_role.hpp"
#include "script/net_script.hpp"
#include "script/script_context.hpp"
#include "script/script_scope.hpp"

#include <raymath.h>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace slopengine {

namespace {

constexpr std::size_t kRedundantCommands = 4;
constexpr std::size_t kCommandBacklog = 4;
constexpr std::uint64_t kCommandWindowTicks = 60;
constexpr std::size_t kMaxQueuedCommands = 64;
constexpr std::size_t kMaxPredictedMoves = 128;
constexpr std::uint64_t kRelevanceLinger = 60;
constexpr float kPlaceLift = 0.9f;
constexpr std::size_t kEventsPerMessage = 256;
constexpr std::size_t kSentSnapshotsKept = 64;
constexpr std::size_t kSamplesKept = 32;
constexpr float kClipTimeSlack = 0.05f;
constexpr float kFreshPriority = 8.0f;
constexpr double kClockSnapTicks = 30.0;
constexpr double kClockPull = 0.1;
constexpr float kCorrectionDecay = 0.85f;

constexpr const char* kAvatarPrefix = "player:";

bool safeNetName(std::string_view name) {
    if (name.empty() || name.size() > 128) {
        return false;
    }
    bool digitsOnly = true;
    for (const char ch : name) {
        const bool plain = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == ' ';
        if (!plain) {
            return false;
        }
        digitsOnly = digitsOnly && ch >= '0' && ch <= '9';
    }
    return !digitsOnly;
}

bool safeNetMapId(std::string_view id) {
    if (id.empty() || id.size() > 128 || id.front() == '/' || id.back() == '/') {
        return false;
    }
    std::size_t start = 0;
    while (start <= id.size()) {
        const std::size_t end = std::min(id.find('/', start), id.size());
        const std::string_view part = id.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        for (const char ch : part) {
            const bool plain = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                ch == '-' || ch == '_' || ch == '.';
            if (!plain) {
                return false;
            }
        }
        start = end + 1;
    }
    return true;
}

bool within(float value, float lo, float hi) {
    return value >= lo && value <= hi;
}

bool saneSelfState(const NetSelfState& self) {
    for (int i = 0; i < 3; ++i) {
        if (!within(self.feet[i], -1.0e6f, 1.0e6f) || !within(self.velocity[i], -1.0e4f, 1.0e4f)) {
            return false;
        }
    }
    return within(self.moveSpeed, 0.0f, 1000.0f) && within(self.gravity, -1000.0f, 1000.0f) &&
        within(self.eyeHeight, -16.0f, 16.0f) && within(self.radius, 0.0f, 8.0f) && within(self.height, 0.0f, 16.0f) &&
        (self.motorShaped < 0 || (within(self.standEye, -16.0f, 16.0f) && within(self.standRadius, 0.0f, 8.0f) &&
                                  within(self.standHeight, 0.0f, 16.0f)));
}

bool replicated(flecs::entity entity) {
    if (entity.parent().is_valid() || !entity.has<LocalTransformation>() || entity.name() == nullptr ||
        !safeNetName(entity.name().c_str())) {
        return false;
    }
    return entity.has<Actor>() || entity.has<ThingTypeRef>() || entity.has<RigidMover>() ||
        entity.has<MotoredBody>() || entity.has<DebrisBody>() || entity.has<PlayerBody>();
}

std::string packageText(const std::vector<NetPackageVersion>& packages) {
    std::string out;
    for (const NetPackageVersion& package : packages) {
        out += (out.empty() ? "" : ", ") + package.id + " " + package.version;
    }
    return out;
}

std::string mismatch(const NetHello& ours, const NetHello& theirs) {
    if (theirs.protocol != ours.protocol) {
        return "protocol " + std::to_string(theirs.protocol) + " is not " + std::to_string(ours.protocol);
    }
    if (theirs.engineVersion != ours.engineVersion) {
        return "engine " + theirs.engineVersion + " is not " + ours.engineVersion;
    }
    std::vector<NetPackageVersion> a = ours.packages;
    std::vector<NetPackageVersion> b = theirs.packages;
    const auto byId = [](const NetPackageVersion& x, const NetPackageVersion& y) {
        return x.id != y.id ? x.id < y.id : x.version < y.version;
    };
    std::sort(a.begin(), a.end(), byId);
    std::sort(b.begin(), b.end(), byId);
    if (a != b) {
        return "packages differ, server runs " + packageText(a);
    }
    if (theirs.schemaHash != ours.schemaHash) {
        return "declared state differs";
    }
    return {};
}

flecs::entity localPlayer(const flecs::world& world) {
    return findLocalPlayer(world);
}

flecs::entity playerBody(const flecs::world& world, const std::string& name) {
    const flecs::entity body = world.lookup(name.c_str());
    return body.is_valid() && body.has<PlayerBody>() ? body : flecs::entity{};
}

} // namespace

NetSession::NetSession(flecs::world& world, NetSessionConfig config, std::unique_ptr<NetTransport> transport)
    : world_(world), config_(std::move(config)) {
    SecureTransportOptions options;
    if (!config_.listenAddress.empty()) {
        SecureKey secret{};
        if (loadOrCreateServerKey(config_.serverKeyPath, secret)) {
            options.staticSecret = secret;
        } else {
            Log::warning(LogCategory::General, "NET: could not keep a server key at %s, using a temporary one",
                         config_.serverKeyPath.string().c_str());
        }
    }
    options.pinnedServerKey = config_.pinnedServerKey;
    transport_ = createSecureTransport(std::move(transport), options);
}

NetSession::~NetSession() {
    for (const auto& entry : clients_) {
        transport_->close(entry.first, "server shutting down");
    }
    if (server_ != kNoNetConnection) {
        transport_->close(server_, "client quit");
    }
}

bool NetSession::start() {
    if (!config_.listenAddress.empty()) {
        if (!transport_->listen(config_.listenAddress)) {
            Log::error(LogCategory::General, "NET: cannot listen on %s", config_.listenAddress.c_str());
            return false;
        }
        role_ = NetRole::Server;
        if (world_.has<NetEventQueue>()) {
            world_.get_mut<NetEventQueue>().forward = true;
            world_.get_mut<NetEventQueue>().localPlayerHere = config_.hostPlayerVisible;
        }
        world_.set<NetSessionLimits>({config_.sessionKind, config_.maxPlayers, config_.playerCeiling});
        Log::info(LogCategory::General, "NET: listening on %s, server key %s", config_.listenAddress.c_str(),
                  secureKeyFingerprint(transport_->publicKey()).c_str());
        Log::info(LogCategory::General, "NET: session %s, up to %d players",
                  config_.sessionKind.empty() ? "of no declared kind" : config_.sessionKind.c_str(), config_.maxPlayers);
        return true;
    }
    if (!config_.connectAddress.empty()) {
        server_ = transport_->connect(config_.connectAddress);
        if (server_ == kNoNetConnection) {
            Log::error(LogCategory::General, "NET: cannot connect to %s", config_.connectAddress.c_str());
            return false;
        }
        role_ = NetRole::Client;
        world_.add<SimRemote>();
        Log::info(LogCategory::General, "NET: connecting to %s", config_.connectAddress.c_str());
        return true;
    }
    return false;
}

void NetSession::registerSystems() {
    world_.component<NetId>();
    world_.system("NetApplyRemoteCommands")
        .kind(flecs::PreUpdate)
        .run([this](flecs::iter&) {
            if (role_ == NetRole::Server) {
                serverApplyCommands();
            }
        }).add<SimSystem>();
    world_.system("NetSendSnapshots")
        .kind(flecs::PostUpdate)
        .run([this](flecs::iter&) {
            if (role_ == NetRole::Server) {
                serverAfterTick();
            }
        }).add<SimSystem>();
}

int NetSession::serverPlayerCount() const {
    return static_cast<int>(serverClientCount()) + (config_.hostPlayerVisible ? 1 : 0);
}

std::size_t NetSession::serverPendingCount() const {
    return clients_.size() - serverClientCount();
}

int NetSession::serverPlayerLimit() const {
    return world_.has<NetSessionLimits>() ? world_.get<NetSessionLimits>().maxPlayers : config_.maxPlayers;
}

std::size_t NetSession::serverClientCount() const {
    return static_cast<std::size_t>(std::count_if(
        clients_.begin(), clients_.end(), [](const auto& entry) { return entry.second.ready; }));
}

void NetSession::callHook(const char* name, const std::string& argument) {
    if (!world_.has<ScriptContext>() || world_.get<ScriptContext>().scheme == nullptr) {
        return;
    }
    s7_scheme* scheme = world_.get<ScriptContext>().scheme;
    const s7_pointer proc = s7_name_to_value(scheme, name);
    if (!s7_is_procedure(proc)) {
        return;
    }
    ScriptScopeGuard scope(ScriptScope::World);
    ScriptCallGuard guard(scheme, roleForProc(name));
    s7_call(scheme, proc, s7_list(scheme, 1, s7_make_string(scheme, argument.c_str())));
}

void NetSession::serverPoll() {
    std::vector<NetEvent> events;
    transport_->poll(events);
    for (const NetEvent& event : events) {
        serverReceive(event);
    }
}

void NetSession::serverReceive(const NetEvent& event) {
    switch (event.kind) {
    case NetEventKind::Incoming:
        if (serverPlayerCount() >= serverPlayerLimit()) {
            transport_->close(event.connection, "server full");
            return;
        }
        if (serverPendingCount() >= config_.maxPendingClients) {
            transport_->close(event.connection, "server busy, try again");
            return;
        }
        if (transport_->accept(event.connection)) {
            ServerClient client{};
            client.connection = event.connection;
            client.arrivedTick = serverTick_;
            client.refilled = std::chrono::steady_clock::now();
            client.tokens = config_.messageBurst;
            clients_[event.connection] = std::move(client);
        }
        return;
    case NetEventKind::Connected:
        if (clients_.count(event.connection) != 0) {
            Log::info(LogCategory::General, "NET: client connected, waiting for its hello");
        }
        return;
    case NetEventKind::Disconnected:
        serverRemove(event.connection, event.reason);
        return;
    case NetEventKind::Message:
        break;
    }
    const auto found = clients_.find(event.connection);
    if (found == clients_.end()) {
        return;
    }
    ServerClient& client = found->second;
    if (!serverWithinRate(client)) {
        serverDrop(event.connection, "too many messages");
        return;
    }
    const std::optional<NetMessageKind> kind = netMessageKind(event.data);
    if (!client.ready) {
        if (kind == NetMessageKind::Hello) {
            serverHello(client, event.data);
        } else if (kind != NetMessageKind::Commands) {
            serverDrop(event.connection, "expected hello");
        }
        return;
    }
    if (kind != NetMessageKind::Commands) {
        serverDrop(event.connection, "unexpected message");
        return;
    }
    std::vector<NetCommandFrame> frames;
    std::uint64_t ackedSnapshot = 0;
    if (!decodeNetCommands(event.data, frames, ackedSnapshot)) {
        serverDrop(event.connection, "malformed commands");
        return;
    }
    if (ackedSnapshot > client.ackedSnapshot && ackedSnapshot <= serverTick_) {
        ++client.statAcks;
        client.statAckLag += serverTick_ - ackedSnapshot;
        serverAcknowledge(client, ackedSnapshot);
    }
    for (NetCommandFrame& frame : frames) {
        const bool queued = std::any_of(client.pending.begin(), client.pending.end(), [&](const NetCommandFrame& other) {
            return other.tick == frame.tick;
        });
        if (frame.tick > client.lastCommandTick && !queued) {
            client.pending.push_back(std::move(frame));
        }
    }
    std::sort(client.pending.begin(), client.pending.end(), [](const auto& a, const auto& b) { return a.tick < b.tick; });
    while (client.pending.size() > kMaxQueuedCommands) {
        client.pending.pop_front();
    }
}

void NetSession::serverHello(ServerClient& client, const std::vector<std::byte>& data) {
    NetHello hello;
    if (!decodeNetHello(data, hello)) {
        serverDrop(client.connection, "malformed hello");
        return;
    }
    const std::string problem = mismatch(config_.hello, hello);
    if (!problem.empty()) {
        Log::info(LogCategory::General, "NET: refused client: %s", problem.c_str());
        serverDrop(client.connection, problem);
        return;
    }
    if (serverPlayerCount() >= serverPlayerLimit()) {
        serverDrop(client.connection, "server full");
        return;
    }
    const bool mapPlayerTaken = !config_.firstClientTakesMapPlayer ||
        std::any_of(clients_.begin(), clients_.end(), [](const auto& entry) {
            return entry.second.ready && entry.second.player == kLocalNetPlayerId;
        });
    if (!mapPlayerTaken) {
        client.player = kLocalNetPlayerId;
        client.ownBody = false;
    } else {
        client.player = "player-" + std::to_string(nextPlayerNumber_++);
        client.ownBody = true;
        spawnPlayerBody(world_, client.player, nextPlayerNumber_);
        addNetPlayer(world_, client.player);
    }
    client.ready = true;
    Log::info(LogCategory::General, "NET: client ready as %s", client.player.c_str());
    callHook("on-player-join", client.player);
    serverSendMap(client);
}

void NetSession::serverSendMap(ServerClient& client) {
    client.known.clear();
    client.lastRelevant.clear();
    client.hadEye = false;
    client.entities.clear();
    client.sent.clear();
    client.clipIds.clear();
    client.ackedClips.clear();
    if (!world_.has<CurrentMap>()) {
        return;
    }
    transport_->send(
        client.connection,
        encodeNetMapChange({world_.get<CurrentMap>().id, "fresh", client.player}),
        NetSendMode::Reliable);
    serverUpdateKnown(client);
}

bool NetSession::serverWithinRate(ServerClient& client) {
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - client.refilled).count();
    client.refilled = now;
    client.tokens = std::min(config_.messageBurst, client.tokens + elapsed * config_.messagesPerSecond);
    if (client.tokens < 1.0) {
        return false;
    }
    client.tokens -= 1.0;
    return true;
}

void NetSession::serverDropLate() {
    std::vector<NetConnectionId> late;
    for (const auto& [connection, client] : clients_) {
        if (!client.ready && serverTick_ - client.arrivedTick > config_.helloTimeoutTicks) {
            late.push_back(connection);
        }
    }
    for (NetConnectionId connection : late) {
        serverDrop(connection, "took too long to join");
    }
}

void NetSession::serverDrop(NetConnectionId connection, const std::string& reason) {
    transport_->close(connection, reason);
    serverRemove(connection, reason);
}

void NetSession::serverRemove(NetConnectionId connection, const std::string& reason) {
    const auto found = clients_.find(connection);
    if (found == clients_.end()) {
        return;
    }
    const ServerClient client = std::move(found->second);
    clients_.erase(found);
    if (!client.ready) {
        return;
    }
    Log::info(LogCategory::General, "NET: %s left: %s", client.player.c_str(), reason.c_str());
    callHook("on-player-leave", client.player);
    if (client.ownBody) {
        despawnPlayerBody(world_, playerBody(world_, client.player));
        removeNetPlayer(world_, client.player);
    } else if (!config_.hostPlayerVisible) {
        resetPlayerBody(world_, playerBody(world_, client.player));
        resetNetPlayer(world_, client.player);
    }
}

void NetSession::serverMapLoaded() {
    serverEntities_.clear();
    nextNetId_ = 1;
    int index = 2;
    for (auto& entry : clients_) {
        ServerClient& client = entry.second;
        if (client.ready && client.ownBody) {
            spawnPlayerBody(world_, client.player, index++);
        }
    }
    serverAssignIds();
    for (auto& entry : clients_) {
        if (entry.second.ready) {
            serverSendMap(entry.second);
        }
    }
}

void NetSession::serverApplyCommands() {
    const std::size_t actions = world_.has<InputState>() ? world_.get<InputState>().actionDown.size() : 0;
    for (auto& entry : clients_) {
        ServerClient& client = entry.second;
        if (!client.ready) {
            continue;
        }
        const flecs::entity body = playerBody(world_, client.player);
        if (!body.is_valid()) {
            client.pending.clear();
            continue;
        }
        NetCommandFrame& latest = client.latest;
        std::vector<char> pressed;
        bool moved = false;
        client.leastPending = std::min(client.leastPending, client.pending.size());
        std::size_t surplus = 0;
        if (++client.pendingWindow >= kCommandWindowTicks) {
            surplus = client.leastPending > 1 ? client.leastPending - 1 : 0;
            client.pendingWindow = 0;
            client.leastPending = kMaxQueuedCommands;
        }
        while (!client.pending.empty() && (!moved || client.pending.size() > kCommandBacklog || surplus > 0)) {
            if (moved && surplus > 0) {
                --surplus;
            }
            NetCommandFrame frame = std::move(client.pending.front());
            client.pending.pop_front();
            if (pressed.size() < frame.pressed.size()) {
                pressed.resize(frame.pressed.size(), 0);
            }
            for (std::size_t i = 0; i < frame.pressed.size(); ++i) {
                pressed[i] = static_cast<char>(pressed[i] | frame.pressed[i]);
            }
            if (moved) {
                serverStepAhead(body, latest);
            }
            client.lastCommandTick = frame.tick;
            latest = std::move(frame);
            moved = true;
        }
        if (!moved) {
            latest.moveForward = 0.0f;
            latest.moveStrafe = 0.0f;
            latest.moveUp = 0.0f;
        }
        PlayerCommand command{};
        command.tick = world_.get<SimClock>().tick;
        command.moveForward = std::clamp(latest.moveForward, -1.0f, 1.0f);
        command.moveStrafe = std::clamp(latest.moveStrafe, -1.0f, 1.0f);
        command.moveUp = std::clamp(latest.moveUp, -1.0f, 1.0f);
        command.yaw = latest.yaw;
        command.pitch = std::clamp(latest.pitch, -1.55f, 1.55f);
        command.actionDown.assign(actions, 0);
        command.actionPressed.assign(actions, 0);
        for (std::size_t i = 0; i < actions && i < latest.down.size(); ++i) {
            command.actionDown[i] = latest.down[i];
        }
        for (std::size_t i = 0; i < actions && i < pressed.size(); ++i) {
            command.actionPressed[i] = pressed[i];
        }
        if (FirstPersonController* controller = body.try_get_mut<FirstPersonController>()) {
            if (controller->allowLook) {
                controller->yaw = command.yaw;
                controller->pitch = command.pitch;
            } else {
                command.yaw = controller->yaw;
                command.pitch = controller->pitch;
            }
        }
        body.set<PlayerCommand>(std::move(command));
    }
}

void NetSession::serverStepAhead(flecs::entity body, const NetCommandFrame& frame) {
    if (!world_.has<PhysicsContext>() || world_.get<PhysicsContext>().world == nullptr) {
        return;
    }
    PhysicsWorld& physics = *world_.get_mut<PhysicsContext>().world;
    const std::uint64_t id = static_cast<std::uint64_t>(body.id());
    CharacterMotor* motor = body.try_get_mut<CharacterMotor>();
    const FirstPersonController* controller = body.try_get<FirstPersonController>();
    if (motor == nullptr || !physics.hasCharacter(id)) {
        return;
    }
    const bool allowMove = controller == nullptr || controller->allowMove;
    MotorRuleState rules = body.has<MotorRuleState>() ? body.get<MotorRuleState>() : MotorRuleState{};
    stepMotorRules(physics, id, *motor, rules,
                   MotorInput{std::clamp(frame.moveForward, -1.0f, 1.0f), &frame.down, &frame.pressed}, allowMove);
    body.set<MotorRuleState>(rules);
    motor = body.try_get_mut<CharacterMotor>();
    if (!allowMove) {
        return;
    }
    applyMoveWish(*motor, std::clamp(frame.moveForward, -1.0f, 1.0f), std::clamp(frame.moveStrafe, -1.0f, 1.0f),
                  std::clamp(frame.moveUp, -1.0f, 1.0f), frame.yaw);
    CharacterStep step{};
    step.id = id;
    step.motor = motor;
    physics.stepCharacterAlone(step);
}

bool NetSession::serverVisibleTo(const ServerClient& client, flecs::entity entity) const {
    if (!entity.is_alive()) {
        return false;
    }
    if (!entity.has<PlayerBody>()) {
        return true;
    }
    const std::string name = entity.name().c_str();
    if (name == client.player) {
        return false;
    }
    if (name == kLocalNetPlayerId && config_.hostPlayerVisible) {
        return true;
    }
    return std::any_of(clients_.begin(), clients_.end(), [&](const auto& entry) {
        return entry.second.ready && entry.second.player == name;
    });
}

void NetSession::serverAssignIds() {
    std::vector<flecs::entity> fresh;
    world_.each([&](flecs::entity entity, const LocalTransformation&) {
        if (!entity.has<NetId>() && replicated(entity)) {
            fresh.push_back(entity);
        }
    });
    std::sort(fresh.begin(), fresh.end(), [](flecs::entity a, flecs::entity b) {
        return std::string_view(a.name().c_str()) < std::string_view(b.name().c_str());
    });
    for (flecs::entity entity : fresh) {
        const std::uint32_t netId = nextNetId_++;
        entity.set<NetId>({netId});
        serverEntities_[netId] = entity.id();
    }
    for (auto it = serverEntities_.begin(); it != serverEntities_.end();) {
        if (!world_.entity(it->second).is_alive()) {
            it = serverEntities_.erase(it);
        } else {
            ++it;
        }
    }
}

void NetSession::serverPlaceEntities() {
    places_.clear();
    const BspTree* tree = world_.has<MapBsp>() && world_.has<MapPvs>() ? &world_.get<MapBsp>().tree : nullptr;
    for (const auto& [netId, raw] : serverEntities_) {
        const flecs::entity entity = world_.entity(raw);
        const LocalTransformation* local = entity.try_get<LocalTransformation>();
        if (local == nullptr) {
            continue;
        }
        EntityPlace place;
        place.at = local->position;
        place.always = entity.has<RigidMover>();
        if (tree != nullptr && !place.always) {
            place.leaf = pvsSampleLeaf(*tree, {place.at.x, place.at.y + kPlaceLift, place.at.z});
        }
        places_[netId] = place;
    }
}

std::int32_t NetSession::serverEyeLeaf(ServerClient& client, Vector3& eye, bool& haveEye) const {
    const flecs::entity body = playerBody(world_, client.player);
    if (body.is_valid() && body.has<Lens>()) {
        client.lastEye = body.get<Lens>().camera.position;
        client.hadEye = true;
    }
    haveEye = client.hadEye;
    eye = client.lastEye;
    if (!haveEye || !world_.has<MapBsp>() || !world_.has<MapPvs>()) {
        return -1;
    }
    return pvsSampleLeaf(world_.get<MapBsp>().tree, eye);
}

bool NetSession::serverEventPoint(const NetEventMessage& event, Vector3& at) const {
    if (event.where == NetEventWhere::Point) {
        at = {event.point[0], event.point[1], event.point[2]};
        return true;
    }
    if (event.where != NetEventWhere::Player) {
        return false;
    }
    const flecs::entity body = playerBody(world_, event.player);
    if (!body.is_valid()) {
        return false;
    }
    if (body.has<Lens>()) {
        at = body.get<Lens>().camera.position;
        return true;
    }
    if (body.has<LocalTransformation>()) {
        at = body.get<LocalTransformation>().position;
        return true;
    }
    return false;
}

void NetSession::serverUpdateKnown(ServerClient& client) {
    std::vector<NetIntroduction> added;
    std::vector<std::uint32_t> gone;
    Vector3 eye{};
    bool haveEye = false;
    const std::int32_t eyeLeaf = serverEyeLeaf(client, eye, haveEye);
    const PvsFile* pvs = world_.has<MapPvs>() ? &world_.get<MapPvs>().pvs : nullptr;
    const float nearSq = config_.nearRadius * config_.nearRadius;
    std::vector<std::pair<float, std::uint32_t>> wanted;
    for (const auto& [netId, raw] : serverEntities_) {
        const flecs::entity entity = world_.entity(raw);
        const auto place = places_.find(netId);
        if (place == places_.end() || !serverVisibleTo(client, entity)) {
            continue;
        }
        const float distSq = haveEye ? Vector3DistanceSqr(place->second.at, eye) : 0.0f;
        bool relevant = place->second.always || pvs == nullptr ||
            (haveEye && (distSq <= nearSq || pvsCanSee(*pvs, eyeLeaf, place->second.leaf)));
        if (relevant) {
            client.lastRelevant[netId] = serverTick_;
        } else {
            const auto last = client.lastRelevant.find(netId);
            relevant = last != client.lastRelevant.end() && serverTick_ - last->second <= kRelevanceLinger;
        }
        if (relevant) {
            wanted.emplace_back(place->second.always ? -1.0f : distSq, netId);
        }
    }
    std::sort(wanted.begin(), wanted.end());
    if (wanted.size() > config_.maxKnownEntities) {
        wanted.resize(config_.maxKnownEntities);
    }
    if (!client.loggedKnown && haveEye) {
        client.loggedKnown = true;
        Log::info(LogCategory::General, "NET: %s sees %zu of %zu entities", client.player.c_str(), wanted.size(),
                  serverEntities_.size());
    }
    std::set<std::uint32_t> desired;
    for (const auto& entry : wanted) {
        desired.insert(entry.second);
    }
    for (std::uint32_t netId : desired) {
        if (client.known.insert(netId).second) {
            const flecs::entity entity = world_.entity(serverEntities_.at(netId));
            added.push_back({netId, entity.name().c_str(),
                             entity.has<PlayerBody>() ? NetEntityKind::Player : NetEntityKind::Thing});
        }
    }
    for (auto it = client.known.begin(); it != client.known.end();) {
        if (desired.count(*it) == 0) {
            gone.push_back(*it);
            it = client.known.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = client.lastRelevant.begin(); it != client.lastRelevant.end();) {
        if (serverEntities_.count(it->first) == 0) {
            it = client.lastRelevant.erase(it);
        } else {
            ++it;
        }
    }
    if (!added.empty()) {
        transport_->send(client.connection, encodeNetIntroduce(added), NetSendMode::Reliable);
    }
    if (!gone.empty()) {
        serverForgetSent(client, gone);
        transport_->send(client.connection, encodeNetForget(gone), NetSendMode::Reliable);
    }
}

void NetSession::serverSendEvents(ServerClient& client, const std::vector<NetEventMessage>& events) {
    if (events.empty()) {
        return;
    }
    const std::vector<NetEventDef>& defs = netSchema().events();
    Vector3 eye{};
    bool haveEye = false;
    const std::int32_t eyeLeaf = serverEyeLeaf(client, eye, haveEye);
    const BspTree* tree = world_.has<MapBsp>() && world_.has<MapPvs>() ? &world_.get<MapBsp>().tree : nullptr;
    std::vector<NetEventMessage> reliable;
    std::vector<NetEventMessage> unreliable;
    for (const NetEventMessage& event : events) {
        if (event.eventId >= defs.size()) {
            continue;
        }
        const NetEventDef& def = defs[event.eventId];
        const bool aimedAtClient = event.where == NetEventWhere::Player && event.player == client.player;
        bool send = true;
        if (def.to == NetEventAudience::Owner) {
            send = aimedAtClient;
        } else if (def.to == NetEventAudience::Near || def.to == NetEventAudience::Pvs) {
            Vector3 at{};
            const bool located = serverEventPoint(event, at);
            if (aimedAtClient) {
                send = true;
            } else if (!located || !haveEye) {
                send = false;
            } else if (def.to == NetEventAudience::Near) {
                send = Vector3DistanceSqr(at, eye) <= def.nearRadius * def.nearRadius;
            } else if (tree != nullptr) {
                send = pvsVisiblePointFromLeaf(*tree, world_.get<MapPvs>().pvs, eyeLeaf, {at.x, at.y + 0.1f, at.z});
            }
        }
        if (send) {
            (def.reliable ? reliable : unreliable).push_back(event);
        }
    }
    serverSendEventBatch(client.connection, reliable, defs, NetSendMode::Reliable);
    serverSendEventBatch(client.connection, unreliable, defs, NetSendMode::Unreliable);
}

void NetSession::serverSendEventBatch(NetConnectionId connection, std::span<const NetEventMessage> events,
                                      const std::vector<NetEventDef>& defs, NetSendMode mode) {
    if (events.empty()) {
        return;
    }
    if (events.size() > kEventsPerMessage) {
        serverSendEventBatch(connection, events.first(kEventsPerMessage), defs, mode);
        serverSendEventBatch(connection, events.subspan(kEventsPerMessage), defs, mode);
        return;
    }
    const std::vector<NetEventMessage> batch(events.begin(), events.end());
    const std::vector<std::byte> data = encodeNetEvents(batch, defs);
    if (data.size() <= kMaxNetMessageBytes - kSecureOverheadBytes) {
        transport_->send(connection, data, mode);
        return;
    }
    if (events.size() == 1) {
        Log::warning(LogCategory::General, "NET: event %s is too large to send", defs[events.front().eventId].name.c_str());
        return;
    }
    const std::size_t half = events.size() / 2;
    serverSendEventBatch(connection, events.first(half), defs, mode);
    serverSendEventBatch(connection, events.subspan(half), defs, mode);
}

void NetSession::serverSendSnapshot(ServerClient& client) {
    NetSnapshot snapshot;
    snapshot.serverTick = serverTick_;
    snapshot.ackedCommand = client.lastCommandTick;
    const flecs::entity body = playerBody(world_, client.player);
    if (body.is_valid() && body.has<Lens>()) {
        const Vector3 eye = body.get<Lens>().camera.position;
        snapshot.hasEye = true;
        snapshot.eye[0] = eye.x;
        snapshot.eye[1] = eye.y;
        snapshot.eye[2] = eye.z;
    }
    if (body.is_valid() && world_.has<PhysicsContext>() && world_.get<PhysicsContext>().world != nullptr) {
        const PhysicsWorld& physics = *world_.get<PhysicsContext>().world;
        const std::uint64_t id = static_cast<std::uint64_t>(body.id());
        const CharacterMotor* motor = body.try_get<CharacterMotor>();
        if (physics.hasCharacter(id) && motor != nullptr) {
            const JPH::RVec3 feet = physics.characterPosition(id);
            const JPH::Vec3 velocity = physics.characterVelocity(id);
            NetSelfState& self = snapshot.self;
            snapshot.hasSelf = true;
            self.feet[0] = static_cast<float>(feet.GetX());
            self.feet[1] = static_cast<float>(feet.GetY());
            self.feet[2] = static_cast<float>(feet.GetZ());
            self.velocity[0] = velocity.GetX();
            self.velocity[1] = velocity.GetY();
            self.velocity[2] = velocity.GetZ();
            self.moveSpeed = motor->moveSpeed;
            self.gravity = motor->gravity;
            self.eyeHeight = motor->eyeHeight;
            self.radius = motor->radius;
            self.height = motor->height;
            const FirstPersonController* controller = body.try_get<FirstPersonController>();
            self.allowMove = controller == nullptr || controller->allowMove;
            self.allowLook = controller == nullptr || controller->allowLook;
            if (const MotorRuleState* rules = body.try_get<MotorRuleState>()) {
                self.motorEnabled = rules->enabled;
                self.motorActive = rules->active;
                self.motorShaped = rules->shapedRule;
                self.standRadius = rules->standRadius;
                self.standHeight = rules->standHeight;
                self.standEye = rules->standEye;
            }
        }
    }
    const std::vector<const NetField*> fields = netSchema().fieldsIn(NetScope{NetScopeKind::Player, {}});
    if (const NetPlayerRecord* record = findNetPlayer(world_, client.player)) {
        for (std::size_t i = 0; i < fields.size() && i < record->values.size(); ++i) {
            if (fields[i]->send != NetAudience::None) {
                snapshot.playerFields.emplace_back(static_cast<int>(i), record->values[i]);
            }
        }
    }
    const NetSnapshot header = snapshot;
    std::size_t used = encodeNetSnapshot(header, fields).size() + 4;
    const std::size_t budget =
        std::clamp(config_.snapshotBudgetBytes, static_cast<std::size_t>(256), kMaxNetMessageBytes / 2);
    Vector3 eye{};
    bool haveEye = false;
    serverEyeLeaf(client, eye, haveEye);
    const float nearRadius = std::max(1.0f, config_.nearRadius);
    struct Candidate {
        std::uint32_t netId = 0;
        SentState state;
    };
    std::vector<Candidate> candidates;
    for (std::uint32_t netId : client.known) {
        const auto raw = serverEntities_.find(netId);
        if (raw == serverEntities_.end()) {
            continue;
        }
        const flecs::entity entity = world_.entity(raw->second);
        SentState state;
        if (!serverEntityState(entity, fields, state)) {
            continue;
        }
        ClientEntity& known = client.entities[netId];
        if (!serverStateChanged(known, state, entity)) {
            known.priority = 0.0f;
            continue;
        }
        const auto place = places_.find(netId);
        const float distance = haveEye && place != places_.end() ? Vector3Distance(place->second.at, eye) : 0.0f;
        float weight = std::clamp(nearRadius / std::max(distance, nearRadius * 0.25f), 0.05f, 4.0f);
        if (entity.has<PlayerBody>()) {
            weight *= 2.0f;
        }
        if (!known.acked && known.lastSentTick == 0) {
            weight += kFreshPriority;
        }
        known.priority += weight;
        candidates.push_back({netId, std::move(state)});
    }
    std::sort(candidates.begin(), candidates.end(), [&client](const Candidate& a, const Candidate& b) {
        const float pa = client.entities[a.netId].priority;
        const float pb = client.entities[b.netId].priority;
        return pa != pb ? pa > pb : a.netId < b.netId;
    });
    SentSnapshot record;
    record.tick = serverTick_;
    std::set<std::uint32_t> named;
    for (Candidate& candidate : candidates) {
        if (snapshot.entities.size() >= kMaxNetEntities) {
            break;
        }
        ClientEntity& known = client.entities[candidate.netId];
        const SentState& state = candidate.state;
        NetEntityState out;
        out.netId = candidate.netId;
        out.pose = state.pose;
        out.fields = state.fields;
        const auto motion = motion_.find(candidate.netId);
        if (motion != motion_.end() && motion->second.moveStart > 0 && known.lastSentTick > 0 &&
            known.lastSentTick >= motion->second.restFrom &&
            known.lastSentTick + 1 < motion->second.moveStart && serverTick_ + 1 >= motion->second.moveStart) {
            out.hold = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(serverTick_ + 1 - motion->second.moveStart, kMaxNetHoldTicks));
        }
        std::size_t clipBytes = 0;
        std::uint32_t clipId = 0;
        bool newName = false;
        if (state.hasAnimation) {
            const auto found = client.clipIds.find(state.clip);
            if (found != client.clipIds.end()) {
                clipId = found->second;
            } else if (client.clipIds.size() < kMaxNetClips) {
                clipId = static_cast<std::uint32_t>(client.clipIds.size());
                client.clipIds.emplace(state.clip, clipId);
            } else {
                clipId = kMaxNetClips;
            }
            if (clipId < kMaxNetClips) {
                out.hasAnimation = true;
                out.clipId = clipId;
                out.time = state.time;
                out.speed = state.speed;
                out.playing = state.playing;
                out.loop = state.loop;
                newName = client.ackedClips.count(clipId) == 0 && named.count(clipId) == 0;
                if (newName) {
                    clipBytes = netClipNameBytes(clipId, state.clip);
                }
            }
        }
        const std::size_t bytes = netEntityStateBytes(out, fields) + clipBytes;
        if (used + bytes > budget && !snapshot.entities.empty()) {
            continue;
        }
        used += bytes;
        if (newName) {
            named.insert(clipId);
            snapshot.clipNames.emplace_back(clipId, state.clip);
            record.clips.push_back(clipId);
        }
        known.priority = 0.0f;
        known.lastSentTick = serverTick_;
        snapshot.entities.push_back(std::move(out));
        record.entities.emplace_back(candidate.netId, std::move(candidate.state));
    }
    client.sent.push_back(std::move(record));
    while (client.sent.size() > kSentSnapshotsKept) {
        client.sent.pop_front();
    }
    const std::vector<std::byte> encoded = encodeNetSnapshot(snapshot, fields);
    ++client.statSnapshots;
    client.statBytes += encoded.size();
    client.statEntities += snapshot.entities.size();
    client.statDeferred += candidates.size() - snapshot.entities.size();
    transport_->send(client.connection, encoded, NetSendMode::Unreliable);
}

bool NetSession::serverEntityState(flecs::entity entity, const std::vector<const NetField*>& fields, SentState& out) const {
    const LocalTransformation* local = entity.try_get<LocalTransformation>();
    if (local == nullptr) {
        return false;
    }
    const float position[3]{local->position.x, local->position.y, local->position.z};
    const float rotation[4]{local->rotation.x, local->rotation.y, local->rotation.z, local->rotation.w};
    if (!packNetPose(position, rotation, out.pose)) {
        return false;
    }
    out.tick = serverTick_;
    if (const AnimationPlayer* animation = entity.try_get<AnimationPlayer>()) {
        if (!animation->clipName.empty() && animation->clipName.size() <= 64 && std::isfinite(animation->speed) &&
            std::fabs(animation->speed) <= 1000.0f) {
            out.hasAnimation = true;
            out.clip = animation->clipName;
            out.time = animation->time;
            out.speed = animation->speed;
            out.playing = animation->playing;
            out.loop = animation->loop;
        }
    }
    if (entity.has<PlayerBody>()) {
        if (const NetPlayerRecord* other = findNetPlayer(world_, entity.name().c_str())) {
            for (std::size_t i = 0; i < fields.size() && i < other->values.size(); ++i) {
                if (fields[i]->send == NetAudience::All) {
                    out.fields.emplace_back(static_cast<int>(i), other->values[i]);
                }
            }
        }
    }
    return true;
}

bool NetSession::serverStateChanged(const ClientEntity& known, const SentState& now, flecs::entity entity) const {
    if (!known.acked) {
        return true;
    }
    const SentState& was = known.ackedState;
    if (!(was.pose == now.pose) || was.hasAnimation != now.hasAnimation || was.fields != now.fields) {
        return true;
    }
    if (!now.hasAnimation) {
        return false;
    }
    if (was.clip != now.clip || was.playing != now.playing || was.loop != now.loop || was.speed != now.speed) {
        return true;
    }
    if (!now.playing) {
        return std::fabs(was.time - now.time) > 0.001f;
    }
    const float duration = config_.clipDuration ? config_.clipDuration(entity) : 0.0f;
    float expected = was.time + static_cast<float>(now.tick - was.tick) * kSimTickDt * now.speed;
    if (duration > 0.0f && now.loop) {
        expected = std::fmod(expected, duration);
        if (expected < 0.0f) {
            expected += duration;
        }
    } else if (duration > 0.0f) {
        expected = std::min(expected, duration);
    }
    float drift = std::fabs(expected - now.time);
    if (duration > 0.0f && now.loop) {
        drift = std::min(drift, duration - drift);
    }
    return drift > kClipTimeSlack;
}

void NetSession::serverAcknowledge(ServerClient& client, std::uint64_t tick) {
    client.ackedSnapshot = tick;
    while (!client.sent.empty() && client.sent.front().tick < tick) {
        client.sent.pop_front();
    }
    if (client.sent.empty() || client.sent.front().tick != tick) {
        return;
    }
    SentSnapshot record = std::move(client.sent.front());
    client.sent.pop_front();
    for (auto& [netId, state] : record.entities) {
        if (client.known.count(netId) == 0) {
            continue;
        }
        ClientEntity& known = client.entities[netId];
        if (!known.acked || state.tick > known.ackedState.tick) {
            known.acked = true;
            known.ackedState = std::move(state);
        }
    }
    for (std::uint32_t clipId : record.clips) {
        client.ackedClips.insert(clipId);
    }
}

void NetSession::serverForgetSent(ServerClient& client, const std::vector<std::uint32_t>& gone) {
    const std::set<std::uint32_t> ids(gone.begin(), gone.end());
    for (std::uint32_t netId : ids) {
        client.entities.erase(netId);
    }
    for (SentSnapshot& record : client.sent) {
        std::erase_if(record.entities, [&ids](const auto& entry) { return ids.count(entry.first) != 0; });
    }
}

void NetSession::serverTrackMotion() {
    for (auto it = motion_.begin(); it != motion_.end();) {
        if (serverEntities_.count(it->first) == 0) {
            it = motion_.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& [netId, raw] : serverEntities_) {
        const LocalTransformation* local = world_.entity(raw).try_get<LocalTransformation>();
        if (local == nullptr) {
            continue;
        }
        const float position[3]{local->position.x, local->position.y, local->position.z};
        const float rotation[4]{local->rotation.x, local->rotation.y, local->rotation.z, local->rotation.w};
        NetPose pose;
        if (!packNetPose(position, rotation, pose)) {
            continue;
        }
        const auto found = motion_.find(netId);
        if (found == motion_.end()) {
            motion_[netId] = EntityMotion{pose, serverTick_, 0, 0};
            continue;
        }
        EntityMotion& motion = found->second;
        if (motion.pose == pose) {
            continue;
        }
        if (serverTick_ > motion.changed + 1) {
            motion.restFrom = motion.changed;
            motion.moveStart = serverTick_;
        }
        motion.changed = serverTick_;
        motion.pose = pose;
    }
}

void NetSession::serverAfterTick() {
    ++serverTick_;
    serverDropLate();
    serverAssignIds();
    serverPlaceEntities();
    serverTrackMotion();
    std::vector<NetEventMessage> events;
    if (world_.has<NetEventQueue>()) {
        events.swap(world_.get_mut<NetEventQueue>().pending);
    }
    const std::uint64_t interval = std::max<std::uint64_t>(1, config_.snapshotIntervalTicks);
    for (auto& entry : clients_) {
        ServerClient& client = entry.second;
        if (!client.ready) {
            continue;
        }
        serverUpdateKnown(client);
        serverSendEvents(client, events);
        if ((serverTick_ + client.connection) % interval == 0) {
            serverSendSnapshot(client);
        }
    }
    serverLogStats();
}

void NetSession::serverLogStats() {
    if (config_.statsSeconds <= 0.0f) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (serverLastTick_ != std::chrono::steady_clock::time_point{}) {
        serverWorstGapMs_ =
            std::max(serverWorstGapMs_, std::chrono::duration<double, std::milli>(now - serverLastTick_).count());
    }
    serverLastTick_ = now;
    const auto every = static_cast<std::uint64_t>(std::max(1.0f, config_.statsSeconds / kSimTickDt));
    if (++serverStatTicks_ < every) {
        return;
    }
    const double seconds = static_cast<double>(serverStatTicks_) * kSimTickDt;
    serverStatTicks_ = 0;
    Log::info(LogCategory::General, "NET STATS server: %zu players, longest gap between ticks %.1f ms",
              static_cast<std::size_t>(serverPlayerCount()), serverWorstGapMs_);
    serverWorstGapMs_ = 0.0;
    for (auto& [connection, client] : clients_) {
        if (!client.ready) {
            continue;
        }
        const double snapshots = static_cast<double>(std::max<std::uint64_t>(1, client.statSnapshots));
        Log::info(LogCategory::General,
                  "NET STATS server to %s: %.1f snapshots/s, %.1f KB/s, %.1f entities and %.1f deferred per snapshot, "
                  "%zu known, ack lag %.1f ticks, ping %.0f ms",
                  client.player.c_str(), static_cast<double>(client.statSnapshots) / seconds,
                  static_cast<double>(client.statBytes) / seconds / 1024.0,
                  static_cast<double>(client.statEntities) / snapshots,
                  static_cast<double>(client.statDeferred) / snapshots, client.known.size(),
                  client.statAcks == 0 ? -1.0
                                       : static_cast<double>(client.statAckLag) / static_cast<double>(client.statAcks),
                  static_cast<double>(transport_->stats(connection).pingMs));
        client.statSnapshots = 0;
        client.statBytes = 0;
        client.statEntities = 0;
        client.statDeferred = 0;
        client.statAcks = 0;
        client.statAckLag = 0;
    }
}

std::vector<std::pair<std::string, bool>> NetSession::clientKnownEntities() const {
    std::vector<std::pair<std::string, bool>> out;
    for (const auto& [netId, introduction] : names_) {
        const auto bound = bound_.find(netId);
        if (bound == bound_.end() || !world_.is_alive(bound->second)) {
            continue;
        }
        const bool player = introduction.kind == NetEntityKind::Player;
        out.emplace_back(player ? netAvatarName(introduction.name) : introduction.name, player);
    }
    return out;
}

void NetSession::clientLogStats() {
    if (config_.statsSeconds <= 0.0f || clientStatSeconds_ < config_.statsSeconds) {
        return;
    }
    const double seconds = clientStatSeconds_;
    std::size_t bound = 0;
    for (const auto& entry : names_) {
        bound += bound_.count(entry.first);
    }
    Log::info(LogCategory::General,
              "NET STATS client %s: %.1f snapshots/s, %.1f KB/s, %.1f entities per snapshot, %zu at most, "
              "%llu missed, %zu of %zu bound, %llu of %llu frames starved, %llu corrections, worst %.3f m, "
              "%.1f events/s, %.1f commands in flight, %zu at most",
              yourPlayer_.c_str(), static_cast<double>(statSnapshots_) / seconds,
              static_cast<double>(statBytes_) / seconds / 1024.0,
              static_cast<double>(statEntities_) / static_cast<double>(std::max<std::uint64_t>(1, statSnapshots_)),
              statMostEntities_, static_cast<unsigned long long>(statMissed_), bound, names_.size(),
              static_cast<unsigned long long>(statStarved_), static_cast<unsigned long long>(statFrames_),
              static_cast<unsigned long long>(statCorrections_), static_cast<double>(statWorstCorrection_),
              static_cast<double>(statEvents_) / seconds,
              static_cast<double>(statInFlight_) / static_cast<double>(std::max<std::uint64_t>(1, statReconciles_)),
              statMostInFlight_);
    clientStatSeconds_ = 0.0f;
    statSnapshots_ = 0;
    statBytes_ = 0;
    statEntities_ = 0;
    statMostEntities_ = 0;
    statMissed_ = 0;
    statFrames_ = 0;
    statStarved_ = 0;
    statCorrections_ = 0;
    statWorstCorrection_ = 0.0f;
    statEvents_ = 0;
    statReconciles_ = 0;
    statInFlight_ = 0;
    statMostInFlight_ = 0;
}

void NetSession::clientFrame(float frameDt) {
    std::vector<NetEvent> events;
    transport_->poll(events);
    for (const NetEvent& event : events) {
        clientReceive(event);
    }
    clientPresentTimeline(frameDt);
    if (connected_ && haveClock_) {
        clientStatSeconds_ += std::max(0.0f, frameDt);
        ++statFrames_;
        if (config_.interpolationDelayTicks > 0 &&
            clock_ - static_cast<double>(config_.interpolationDelayTicks) > static_cast<double>(lastSnapshotTick_)) {
            ++statStarved_;
        }
        clientLogStats();
    }
    const int ticks = advanceSimClock(commandClock_, frameDt);
    for (int i = 0; i < ticks; ++i) {
        clientSendCommands();
    }
    const flecs::entity player = localPlayer(world_);
    if (SimEyePose* eye = player.is_valid() ? player.try_get_mut<SimEyePose>() : nullptr) {
        eye->ownAlpha = predicting_ ? simBlendAlpha(commandClock_) : -1.0f;
    }
}

bool NetSession::clientPredictMove(const PredictedMove& move) {
    const flecs::entity player = localPlayer(world_);
    if (!player.is_valid() || !world_.has<PhysicsContext>() || world_.get<PhysicsContext>().world == nullptr) {
        return false;
    }
    PhysicsWorld& physics = *world_.get_mut<PhysicsContext>().world;
    const std::uint64_t id = static_cast<std::uint64_t>(player.id());
    CharacterMotor* motor = player.try_get_mut<CharacterMotor>();
    if (motor == nullptr || !physics.hasCharacter(id)) {
        return false;
    }
    const FirstPersonController* controller = player.try_get<FirstPersonController>();
    const bool allowMove = controller == nullptr || controller->allowMove;
    MotorRuleState rules = player.has<MotorRuleState>() ? player.get<MotorRuleState>() : MotorRuleState{};
    stepMotorRules(physics, id, *motor, rules, MotorInput{move.moveForward, &move.down, &move.pressed}, allowMove);
    const float eyeHeight = motor->eyeHeight;
    player.set<MotorRuleState>(rules);
    motor = player.try_get_mut<CharacterMotor>();
    if (FirstPersonController* view = player.try_get_mut<FirstPersonController>()) {
        view->eyeHeight = eyeHeight;
    }
    if (allowMove) {
        applyMoveWish(*motor, move.moveForward, move.moveStrafe, move.moveUp, move.yaw);
    } else {
        motor->wishX = 0.0f;
        motor->wishY = 0.0f;
        motor->wishZ = 0.0f;
    }
    CharacterStep step{};
    step.id = id;
    step.motor = motor;
    physics.stepCharacterAlone(step);
    return true;
}

void NetSession::clientPresentPrediction(bool advance) {
    const flecs::entity player = localPlayer(world_);
    if (!player.is_valid() || !world_.has<PhysicsContext>() || world_.get<PhysicsContext>().world == nullptr) {
        return;
    }
    const PhysicsWorld& physics = *world_.get<PhysicsContext>().world;
    const std::uint64_t id = static_cast<std::uint64_t>(player.id());
    const CharacterMotor* motor = player.try_get<CharacterMotor>();
    if (motor == nullptr || !physics.hasCharacter(id)) {
        return;
    }
    const JPH::RVec3 feet = physics.characterPosition(id);
    const Vector3 feetAt{static_cast<float>(feet.GetX()), static_cast<float>(feet.GetY()), static_cast<float>(feet.GetZ())};
    const Vector3 eye = Vector3Add({feetAt.x, feetAt.y + motor->eyeHeight, feetAt.z}, correction_);
    if (SimEyePose* pose = player.try_get_mut<SimEyePose>()) {
        if (advance) {
            pose->prev = pose->cur;
        }
        pose->cur = eye;
        if (!predicting_ || Vector3DistanceSqr(pose->prev, pose->cur) > kSimPoseSnapDistance * kSimPoseSnapDistance) {
            pose->prev = pose->cur;
        }
    } else {
        player.set<SimEyePose>({eye, eye, eye});
    }
    if (LocalTransformation* local = player.try_get_mut<LocalTransformation>()) {
        local->position = feetAt;
    }
}

void NetSession::clientReconcile(const NetSnapshot& snapshot) {
    const flecs::entity player = localPlayer(world_);
    if (!player.is_valid() || !world_.has<PhysicsContext>() || world_.get<PhysicsContext>().world == nullptr) {
        predicting_ = false;
        return;
    }
    PhysicsWorld& physics = *world_.get_mut<PhysicsContext>().world;
    const std::uint64_t id = static_cast<std::uint64_t>(player.id());
    CharacterMotor* motor = player.try_get_mut<CharacterMotor>();
    if (motor == nullptr || !physics.hasCharacter(id)) {
        predicting_ = false;
        return;
    }
    const NetSelfState& self = snapshot.self;
    motor->moveSpeed = self.moveSpeed;
    motor->gravity = self.gravity;
    motor->eyeHeight = self.eyeHeight;
    if (std::fabs(motor->radius - self.radius) > 1.0e-4f || std::fabs(motor->height - self.height) > 1.0e-4f) {
        CharacterMotor resized = *motor;
        resized.radius = self.radius;
        resized.height = self.height;
        if (physics.resizeCharacter(id, resized, 10.0f)) {
            motor->radius = self.radius;
            motor->height = self.height;
        }
    }
    if (FirstPersonController* controller = player.try_get_mut<FirstPersonController>()) {
        controller->allowMove = self.allowMove;
        controller->allowLook = self.allowLook;
        controller->eyeHeight = self.eyeHeight;
    }
    player.set<MotorRuleState>(
        {self.motorEnabled, self.motorActive, self.motorShaped, self.standRadius, self.standHeight, self.standEye});
    motor = player.try_get_mut<CharacterMotor>();
    const SimEyePose* shown = player.try_get<SimEyePose>();
    const Vector3 before = shown != nullptr ? shown->cur : Vector3{};
    const bool wasPredicting = predicting_ && shown != nullptr;
    physics.resetCharacterState(
        id,
        JPH::RVec3(self.feet[0], self.feet[1], self.feet[2]),
        JPH::Vec3(self.velocity[0], self.velocity[1], self.velocity[2]));
    while (!predicted_.empty() && predicted_.front().tick <= snapshot.ackedCommand) {
        predicted_.pop_front();
    }
    ++statReconciles_;
    statInFlight_ += predicted_.size();
    statMostInFlight_ = std::max(statMostInFlight_, predicted_.size());
    for (const PredictedMove& move : predicted_) {
        clientPredictMove(move);
    }
    const JPH::RVec3 feet = physics.characterPosition(id);
    const Vector3 truth{static_cast<float>(feet.GetX()), static_cast<float>(feet.GetY()) + motor->eyeHeight,
                        static_cast<float>(feet.GetZ())};
    const float moved = Vector3Distance(Vector3Subtract(before, correction_), truth);
    correction_ = wasPredicting ? Vector3Subtract(before, truth) : Vector3{};
    if (wasPredicting && moved > 0.01f) {
        ++statCorrections_;
        statWorstCorrection_ = std::max(statWorstCorrection_, moved);
    }
    if (Vector3Length(correction_) > kSimPoseSnapDistance) {
        correction_ = {};
    }
    clientPresentPrediction(false);
    predicting_ = true;
}

void NetSession::clientReceive(const NetEvent& event) {
    if (event.connection != server_) {
        return;
    }
    switch (event.kind) {
    case NetEventKind::Incoming:
        return;
    case NetEventKind::Connected: {
        connected_ = true;
        const std::optional<SecureKey> key = transport_->peerKey(server_);
        Log::info(LogCategory::General, "NET: connected, server key %s",
                  key ? secureKeyFingerprint(*key).c_str() : "unknown");
        if (!config_.pinnedServerKey && !config_.knownServersPath.empty()) {
            const KnownServer known =
                key ? checkKnownServer(config_.knownServersPath, config_.connectAddress, *key) : KnownServer::Changed;
            if (known == KnownServer::Changed) {
                clientRefuse("the key of " + config_.connectAddress + " changed since the last visit, remove its line from " +
                             config_.knownServersPath.string() + " if that was expected");
                return;
            }
            if (known == KnownServer::New) {
                Log::info(LogCategory::General, "NET: first visit to %s, remembering its key", config_.connectAddress.c_str());
            }
            if (known == KnownServer::Unsaved) {
                Log::warning(LogCategory::General, "NET: could not remember the key of %s in %s",
                             config_.connectAddress.c_str(), config_.knownServersPath.string().c_str());
            }
        }
        transport_->send(server_, encodeNetHello(config_.hello), NetSendMode::Reliable);
        return;
    }
    case NetEventKind::Disconnected:
        Log::warning(LogCategory::General, "NET: disconnected: %s", event.reason.c_str());
        connected_ = false;
        server_ = kNoNetConnection;
        world_.remove<SimRemote>();
        clientResetView(true);
        yourPlayer_.clear();
        expectedMap_.clear();
        commandTick_ = 0;
        commandClock_ = SimClock{};
        recentCommands_.clear();
        if (config_.disconnected) {
            config_.disconnected(event.reason);
        }
        return;
    case NetEventKind::Message:
        break;
    }
    const std::optional<NetMessageKind> kind = netMessageKind(event.data);
    if (kind == NetMessageKind::MapChange) {
        NetMapChange change;
        if (!decodeNetMapChange(event.data, change) || !safeNetMapId(change.mapId) ||
            !safeNetName(change.yourPlayer) || !safeNetName(change.reason)) {
            clientRefuse("server sent a bad map change");
            return;
        }
        clientResetView(false);
        yourPlayer_ = change.yourPlayer;
        expectedMap_ = change.mapId;
        if (config_.loadMap) {
            config_.loadMap(change.mapId, change.reason);
        }
    } else if (kind == NetMessageKind::Introduce) {
        std::vector<NetIntroduction> entries;
        if (!decodeNetIntroduce(event.data, entries)) {
            clientRefuse("server sent a bad introduction");
            return;
        }
        for (NetIntroduction& entry : entries) {
            if (!safeNetName(entry.name)) {
                clientRefuse("server sent a bad entity name");
                return;
            }
            if (entry.kind == NetEntityKind::Player && entry.name == yourPlayer_) {
                continue;
            }
            if (names_.count(entry.netId) == 0 && names_.size() >= kMaxNetEntities) {
                clientRefuse("server sent too many entities");
                return;
            }
            bound_.erase(entry.netId);
            names_[entry.netId] = std::move(entry);
        }
    } else if (kind == NetMessageKind::Forget) {
        std::vector<std::uint32_t> ids;
        if (decodeNetForget(event.data, ids)) {
            for (std::uint32_t netId : ids) {
                const auto name = names_.find(netId);
                if (name != names_.end() && appeared_.erase(netId) != 0) {
                    removeNetPlayer(world_, netAvatarName(name->second.name));
                    callHook("on-player-vanish", netAvatarName(name->second.name));
                }
                if (name != names_.end() && name->second.kind == NetEntityKind::Thing) {
                    clientHide(name->second.name);
                }
                if (name != names_.end()) {
                    names_.erase(name);
                }
                bound_.erase(netId);
                history_.erase(netId);
            }
        }
    } else if (kind == NetMessageKind::Events) {
        std::vector<NetEventMessage> events;
        if (decodeNetEvents(event.data, netSchema().events(), events) && world_.has<ScriptContext>()) {
            s7_scheme* scheme = world_.get<ScriptContext>().scheme;
            for (const NetEventMessage& message : events) {
                ++statEvents_;
                deliverNetEvent(scheme, message);
            }
        }
    } else if (kind == NetMessageKind::Snapshot) {
        NetSnapshot snapshot;
        if (decodeNetSnapshot(event.data, netSchema().fieldsIn(NetScope{NetScopeKind::Player, {}}), snapshot) &&
            snapshot.serverTick > lastSnapshotTick_) {
            if (snapshot.hasSelf && !saneSelfState(snapshot.self)) {
                clientRefuse("server sent an impossible player state");
                return;
            }
            if (lastSnapshotTick_ != 0) {
                const std::uint64_t step = snapshot.serverTick - lastSnapshotTick_;
                statInterval_ = statInterval_ == 0 ? step : std::min(statInterval_, step);
                statMissed_ += step / std::max<std::uint64_t>(1, statInterval_) - 1;
            }
            ++statSnapshots_;
            statBytes_ += event.data.size();
            statEntities_ += snapshot.entities.size();
            statMostEntities_ = std::max(statMostEntities_, snapshot.entities.size());
            lastSnapshotTick_ = snapshot.serverTick;
            clientSnapshot(snapshot);
        }
    }
}

flecs::entity NetSession::clientEntity(std::uint32_t netId) {
    const auto bound = bound_.find(netId);
    if (bound != bound_.end()) {
        const flecs::entity entity = world_.entity(bound->second);
        if (entity.is_alive()) {
            return entity;
        }
        bound_.erase(bound);
    }
    const auto name = names_.find(netId);
    if (name == names_.end()) {
        return {};
    }
    const bool player = name->second.kind == NetEntityKind::Player;
    const std::string local = player ? netAvatarName(name->second.name) : name->second.name;
    flecs::entity entity = world_.lookup(local.c_str());
    const bool mapReady = world_.has<CurrentMap>() && world_.get<CurrentMap>().id == expectedMap_;
    if (!entity.is_valid() && player && mapReady && appeared_.size() < kMaxNetPlayers &&
        appeared_.insert(netId).second) {
        addNetPlayer(world_, local);
        callHook("on-player-appear", local);
        entity = world_.lookup(local.c_str());
        if (entity.is_valid()) {
            entity.add<NetStandIn>();
        }
    }
    if (entity.is_valid() && !clientMayBind(entity, player)) {
        return {};
    }
    if (!player && entity.is_valid()) {
        clientShow(entity);
    }
    if (!entity.is_valid() || !entity.has<LocalTransformation>()) {
        return {};
    }
    bound_[netId] = entity.id();
    return entity;
}

void NetSession::applyStandInFields(const NetEntityState& state) {
    const auto name = names_.find(state.netId);
    if (name == names_.end() || name->second.kind != NetEntityKind::Player || !world_.has<NetStore>()) {
        return;
    }
    const std::string local = netAvatarName(name->second.name);
    for (NetPlayerRecord& record : world_.get_mut<NetStore>().players) {
        if (record.id != local) {
            continue;
        }
        for (const auto& [slot, value] : state.fields) {
            if (static_cast<std::size_t>(slot) < record.values.size()) {
                record.values[static_cast<std::size_t>(slot)] = value;
            }
        }
        return;
    }
}

bool NetSession::clientMayBind(flecs::entity entity, bool player) const {
    if (entity.has<LocalPlayer>()) {
        return false;
    }
    if (player) {
        return !entity.parent().is_valid();
    }
    return !entity.has<NetStandIn>() && replicated(entity);
}

void NetSession::clientRefuse(const std::string& reason) {
    Log::warning(LogCategory::General, "NET: leaving server: %s", reason.c_str());
    if (server_ != kNoNetConnection) {
        transport_->close(server_, reason);
    }
    clientReceive(NetEvent{NetEventKind::Disconnected, server_, {}, reason});
}

void NetSession::clientResetView(bool reenable) {
    if (reenable) {
        for (const auto& entry : hidden_) {
            for (flecs::entity_t id : entry.second) {
                if (world_.is_alive(id)) {
                    world_.entity(id).enable();
                }
            }
        }
    }
    clearStandInRecords();
    names_.clear();
    bound_.clear();
    appeared_.clear();
    predicted_.clear();
    predicting_ = false;
    correction_ = {};
    hidden_.clear();
    hidOnLoad_ = false;
    lastSnapshotTick_ = 0;
    loggedSnapshots_ = false;
    ackSnapshot_ = 0;
    haveClock_ = false;
    history_.clear();
    eyeHistory_.clear();
    clipNames_.clear();
}

void NetSession::clientHide(const std::string& name) {
    const flecs::entity root = world_.lookup(name.c_str());
    if (!root.is_valid() || hidden_.count(name) != 0 || !clientMayBind(root, false)) {
        return;
    }
    std::vector<flecs::entity_t> subtree;
    std::vector<flecs::entity> pending{root};
    while (!pending.empty()) {
        const flecs::entity entity = pending.back();
        pending.pop_back();
        subtree.push_back(entity.id());
        entity.children([&](flecs::entity child) { pending.push_back(child); });
    }
    for (flecs::entity_t id : subtree) {
        world_.entity(id).disable();
    }
    hidden_[name] = std::move(subtree);
}

void NetSession::clientShow(flecs::entity entity) {
    const auto hidden = hidden_.find(entity.name().c_str());
    if (hidden == hidden_.end()) {
        return;
    }
    for (flecs::entity_t id : hidden->second) {
        if (world_.is_alive(id)) {
            world_.entity(id).enable();
        }
    }
    hidden_.erase(hidden);
    entity.remove<SimPose>();
}

void NetSession::clientHideUnintroduced() {
    std::set<std::string> introduced;
    for (const auto& [netId, entry] : names_) {
        if (entry.kind == NetEntityKind::Thing) {
            introduced.insert(entry.name);
        }
    }
    std::vector<std::string> unseen;
    world_.each([&](flecs::entity entity, const LocalTransformation&) {
        if (!replicated(entity) || entity.has<PlayerBody>() || entity.has<NetStandIn>()) {
            return;
        }
        const std::string name = entity.name().c_str();
        if (introduced.count(name) == 0) {
            unseen.push_back(name);
        }
    });
    for (const std::string& name : unseen) {
        clientHide(name);
    }
}

void NetSession::clearStandInRecords() {
    if (!world_.has<NetStore>()) {
        return;
    }
    std::vector<NetPlayerRecord>& players = world_.get_mut<NetStore>().players;
    players.erase(
        std::remove_if(players.begin(), players.end(), [](const NetPlayerRecord& record) {
            return record.id.rfind(kAvatarPrefix, 0) == 0;
        }),
        players.end());
}

void NetSession::clientSnapshot(const NetSnapshot& snapshot) {
    if (!hidOnLoad_ && world_.has<CurrentMap>() && world_.get<CurrentMap>().id == expectedMap_) {
        hidOnLoad_ = true;
        clientHideUnintroduced();
    }
    for (const auto& [clipId, name] : snapshot.clipNames) {
        clipNames_[clipId] = name;
    }
    const bool mapReady = hidOnLoad_ && world_.has<CurrentMap>() && world_.get<CurrentMap>().id == expectedMap_;
    bool complete = mapReady;
    std::size_t boundCount = 0;
    for (const NetEntityState& state : snapshot.entities) {
        if (names_.count(state.netId) == 0) {
            complete = false;
            continue;
        }
        const flecs::entity entity = clientEntity(state.netId);
        if (!entity.is_valid()) {
            continue;
        }
        ++boundCount;
        if (!state.fields.empty()) {
            applyStandInFields(state);
        }
        ClientSample sample;
        sample.tick = snapshot.serverTick;
        float position[3]{};
        float rotation[4]{};
        unpackNetPose(state.pose, position, rotation);
        sample.position = {position[0], position[1], position[2]};
        sample.rotation = QuaternionNormalize({rotation[0], rotation[1], rotation[2], rotation[3]});
        std::deque<ClientSample>& samples = history_[state.netId];
        if (state.hasAnimation) {
            const auto clip = clipNames_.find(state.clipId);
            if (clip != clipNames_.end()) {
                sample.hasAnimation = true;
                sample.clip = clip->second;
                sample.time = state.time;
                sample.speed = state.speed;
                sample.playing = state.playing;
                sample.loop = state.loop;
            } else if (!samples.empty()) {
                const ClientSample& last = samples.back();
                sample.hasAnimation = last.hasAnimation;
                sample.clip = last.clip;
                sample.time = last.time;
                sample.speed = last.speed;
                sample.playing = last.playing;
                sample.loop = last.loop;
                complete = false;
            }
        }
        if (state.hold != 0 && !samples.empty() && state.hold < sample.tick &&
            samples.back().tick < sample.tick - state.hold) {
            ClientSample held = samples.back();
            if (held.hasAnimation && held.playing) {
                held.time += static_cast<float>(sample.tick - state.hold - held.tick) * kSimTickDt * held.speed;
            }
            held.tick = sample.tick - state.hold;
            samples.push_back(std::move(held));
        }
        samples.push_back(std::move(sample));
        while (samples.size() > kSamplesKept) {
            samples.pop_front();
        }
    }
    if (complete) {
        ackSnapshot_ = snapshot.serverTick;
    }
    const double tick = static_cast<double>(snapshot.serverTick);
    if (!haveClock_ || std::fabs(tick - clock_) > kClockSnapTicks) {
        clock_ = tick;
        haveClock_ = true;
    } else {
        clock_ += (tick - clock_) * kClockPull;
    }
    const flecs::entity player = localPlayer(world_);
    if (snapshot.hasSelf) {
        clientReconcile(snapshot);
    }
    if (snapshot.hasEye && player.is_valid() && !predicting_) {
        eyeHistory_.emplace_back(snapshot.serverTick, Vector3{snapshot.eye[0], snapshot.eye[1], snapshot.eye[2]});
        while (eyeHistory_.size() > kSamplesKept) {
            eyeHistory_.pop_front();
        }
    } else if (predicting_) {
        eyeHistory_.clear();
    }
    if (world_.has<NetStore>() && !world_.get<NetStore>().players.empty()) {
        std::vector<NetValue>& values = world_.get_mut<NetStore>().players.front().values;
        for (const auto& [slot, value] : snapshot.playerFields) {
            if (static_cast<std::size_t>(slot) < values.size()) {
                values[static_cast<std::size_t>(slot)] = value;
            }
        }
    }
    if (!loggedSnapshots_ && mapReady) {
        loggedSnapshots_ = true;
        Log::info(LogCategory::General, "NET: receiving snapshots as %s, %zu of %zu entities bound, eye at %.2f %.2f %.2f",
                  yourPlayer_.c_str(), boundCount, snapshot.entities.size(), snapshot.eye[0], snapshot.eye[1],
                  snapshot.eye[2]);
    }
}

namespace {

struct TimelinePose {
    Vector3 position{};
    Quaternion rotation{0.0f, 0.0f, 0.0f, 1.0f};
};

template <typename Sample, typename Blend>
auto sampleTimeline(const std::deque<Sample>& samples, double tick, Blend blend) {
    std::size_t from = 0;
    while (from + 1 < samples.size() && static_cast<double>(samples[from + 1].tick) <= tick) {
        ++from;
    }
    const Sample& a = samples[from];
    if (from + 1 >= samples.size() || tick <= static_cast<double>(a.tick)) {
        return blend(a, a, 0.0f);
    }
    const Sample& b = samples[from + 1];
    const float alpha = static_cast<float>(
        std::clamp((tick - static_cast<double>(a.tick)) / static_cast<double>(b.tick - a.tick), 0.0, 1.0));
    return blend(a, b, alpha);
}

template <typename Sample>
void pruneTimeline(std::deque<Sample>& samples, double tick) {
    while (samples.size() >= 2 && static_cast<double>(samples[1].tick) <= tick) {
        samples.pop_front();
    }
}

struct EyeSample {
    std::uint64_t tick = 0;
    Vector3 eye{};
};

}

void NetSession::clientPresentTimeline(float frameDt) {
    if (!haveClock_) {
        return;
    }
    clock_ += std::max(0.0f, frameDt) / kSimTickDt;
    if (world_.has<SimClock>()) {
        world_.get_mut<SimClock>().accumulator = kSimTickDt;
    }
    const double renderTick = config_.interpolationDelayTicks == 0
        ? static_cast<double>(lastSnapshotTick_)
        : clock_ - static_cast<double>(config_.interpolationDelayTicks);
    for (auto& [netId, samples] : history_) {
        const auto bound = bound_.find(netId);
        if (bound != bound_.end() && world_.is_alive(bound->second)) {
            clientPresentEntity(world_.entity(bound->second), samples, renderTick);
        }
    }
    const flecs::entity player = localPlayer(world_);
    if (eyeHistory_.empty() || !player.is_valid() || predicting_) {
        return;
    }
    std::deque<EyeSample> eyes;
    for (const auto& [tick, eye] : eyeHistory_) {
        eyes.push_back({tick, eye});
    }
    const auto blendEye = [](const EyeSample& a, const EyeSample& b, float alpha) {
        if (Vector3DistanceSqr(a.eye, b.eye) > kSimPoseSnapDistance * kSimPoseSnapDistance) {
            return alpha >= 1.0f ? b.eye : a.eye;
        }
        return Vector3Lerp(a.eye, b.eye, alpha);
    };
    const Vector3 previous = sampleTimeline(eyes, renderTick - 1.0, blendEye);
    const Vector3 current = sampleTimeline(eyes, renderTick, blendEye);
    player.set<SimEyePose>({previous, current, current});
    while (eyeHistory_.size() >= 2 && static_cast<double>(eyeHistory_[1].first) <= renderTick - 1.0) {
        eyeHistory_.pop_front();
    }
}

void NetSession::clientPresentEntity(flecs::entity entity, std::deque<ClientSample>& samples, double renderTick) {
    pruneTimeline(samples, renderTick - 1.0);
    if (samples.empty()) {
        return;
    }
    const auto blendPose = [](const ClientSample& a, const ClientSample& b, float alpha) {
        if (Vector3DistanceSqr(a.position, b.position) > kSimPoseSnapDistance * kSimPoseSnapDistance) {
            return alpha >= 1.0f ? TimelinePose{b.position, b.rotation} : TimelinePose{a.position, a.rotation};
        }
        return TimelinePose{Vector3Lerp(a.position, b.position, alpha), QuaternionSlerp(a.rotation, b.rotation, alpha)};
    };
    const TimelinePose before = sampleTimeline(samples, renderTick - 1.0, blendPose);
    const TimelinePose now = sampleTimeline(samples, renderTick, blendPose);
    LocalTransformation previous = entity.get<LocalTransformation>();
    previous.position = before.position;
    previous.rotation = before.rotation;
    LocalTransformation current = previous;
    current.position = now.position;
    current.rotation = now.rotation;
    entity.set<SimPose>({previous, current, current});
    entity.set<LocalTransformation>(current);
    AnimationPlayer* animation = entity.try_get_mut<AnimationPlayer>();
    if (animation == nullptr) {
        return;
    }
    std::size_t index = 0;
    while (index + 1 < samples.size() && static_cast<double>(samples[index + 1].tick) <= renderTick) {
        ++index;
    }
    const ClientSample& from = samples[index];
    const ClientSample* to = index + 1 < samples.size() && renderTick > static_cast<double>(from.tick)
        ? &samples[index + 1]
        : nullptr;
    if (!from.hasAnimation) {
        return;
    }
    if (animation->clipName != from.clip) {
        animation->play(from.clip, from.loop, from.speed);
    }
    animation->speed = from.speed;
    animation->loop = from.loop;
    animation->playing = from.playing;
    const float duration = config_.clipDuration ? config_.clipDuration(entity) : 0.0f;
    float time = from.time;
    if (to != nullptr && to->hasAnimation && to->clip == from.clip && from.playing && to->playing) {
        const float alpha = static_cast<float>(std::clamp(
            (renderTick - static_cast<double>(from.tick)) / static_cast<double>(to->tick - from.tick), 0.0, 1.0));
        time = blendAnimationTime(from.time, to->time, duration, from.loop && to->time < from.time, alpha);
    } else if (from.playing) {
        time += static_cast<float>(renderTick - static_cast<double>(from.tick)) * kSimTickDt * from.speed;
        if (duration > 0.0f && from.loop) {
            time = std::fmod(time, duration);
            if (time < 0.0f) {
                time += duration;
            }
        } else if (duration > 0.0f) {
            time = std::clamp(time, 0.0f, duration);
        }
    }
    if (time != animation->time || from.playing) {
        animation->poseStale = true;
    }
    animation->time = time;
    animation->prevTime = time;
    animation->wrappedLastTick = false;
}

void NetSession::clientSendCommands() {
    if (!connected_ || server_ == kNoNetConnection || yourPlayer_.empty() || !world_.has<InputState>() ||
        !world_.has<InputCommand>()) {
        return;
    }
    const flecs::entity player = localPlayer(world_);
    float yaw = 0.0f;
    float pitch = 0.0f;
    if (player.is_valid()) {
        if (const FirstPersonController* controller = player.try_get<FirstPersonController>()) {
            yaw = controller->yaw;
            pitch = controller->pitch;
        }
    }
    PlayerCommand command{};
    buildPlayerCommand(
        command,
        world_.get_mut<InputState>(),
        world_.get<InputCommand>(),
        yaw,
        pitch,
        world_.get<InputContextStack>().allowsGameplay(),
        ++commandTick_);
    NetCommandFrame frame;
    frame.tick = command.tick;
    frame.moveForward = command.moveForward;
    frame.moveStrafe = command.moveStrafe;
    frame.moveUp = command.moveUp;
    frame.yaw = command.yaw;
    frame.pitch = command.pitch;
    frame.down = command.actionDown;
    frame.pressed = command.actionPressed;
    frame.down.resize(std::min(frame.down.size(), kMaxNetActions));
    frame.pressed.resize(std::min(frame.pressed.size(), kMaxNetActions));
    if (predicting_) {
        const PredictedMove move{frame.tick, frame.moveForward, frame.moveStrafe, frame.moveUp, frame.yaw,
                                 frame.down, frame.pressed};
        if (clientPredictMove(move)) {
            predicted_.push_back(move);
            while (predicted_.size() > kMaxPredictedMoves) {
                predicted_.pop_front();
            }
            correction_ = Vector3Scale(correction_, kCorrectionDecay);
            if (Vector3LengthSqr(correction_) < 1.0e-8f) {
                correction_ = {};
            }
            clientPresentPrediction(true);
        }
    }
    recentCommands_.push_back(std::move(frame));
    while (recentCommands_.size() > kRedundantCommands) {
        recentCommands_.pop_front();
    }
    transport_->send(
        server_,
        encodeNetCommands(std::vector<NetCommandFrame>(recentCommands_.begin(), recentCommands_.end()), ackSnapshot_),
        NetSendMode::Unreliable);
}

bool loadOrCreateServerKey(const std::filesystem::path& path, SecureKey& secret) {
    if (path.empty()) {
        return false;
    }
    std::error_code error;
    if (std::filesystem::exists(path, error)) {
        std::ifstream in(path, std::ios::binary);
        in.read(reinterpret_cast<char*>(secret.data()), static_cast<std::streamsize>(secret.size()));
        return in.gcount() == static_cast<std::streamsize>(secret.size());
    }
    if (!generateSecureKey(secret)) {
        return false;
    }
    std::filesystem::create_directories(path.parent_path(), error);
#ifdef _WIN32
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(secret.data()), static_cast<std::streamsize>(secret.size()));
        if (!out) {
            return false;
        }
    }
    std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace, error);
    return true;
#else
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        return false;
    }
    const bool written = ::write(fd, secret.data(), secret.size()) == static_cast<ssize_t>(secret.size());
    return ::close(fd) == 0 && written;
#endif
}

bool parseSecureKeyHex(std::string_view text, SecureKey& out) {
    if (text.size() != 64) {
        return false;
    }
    for (std::size_t i = 0; i < out.size(); ++i) {
        unsigned value = 0;
        if (std::sscanf(std::string(text.substr(i * 2, 2)).c_str(), "%2x", &value) != 1) {
            return false;
        }
        out[i] = static_cast<std::uint8_t>(value);
    }
    return true;
}

std::string secureKeyHex(const SecureKey& key) {
    std::string out;
    char pair[3];
    for (const std::uint8_t byte : key) {
        std::snprintf(pair, sizeof(pair), "%02x", byte);
        out += pair;
    }
    return out;
}

KnownServer checkKnownServer(const std::filesystem::path& path, const std::string& address, const SecureKey& key) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t space = line.rfind(' ');
        SecureKey known{};
        if (space == std::string::npos || line.substr(0, space) != address ||
            !parseSecureKeyHex(line.substr(space + 1), known)) {
            continue;
        }
        return known == key ? KnownServer::Same : KnownServer::Changed;
    }
    in.close();
    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    std::ofstream out(path, std::ios::app);
    out << address << ' ' << secureKeyHex(key) << '\n';
    return out ? KnownServer::New : KnownServer::Unsaved;
}

std::string netAvatarName(std::string_view player) {
    return std::string(kAvatarPrefix) + std::string(player);
}

}
