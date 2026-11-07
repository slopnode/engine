#include "net/net_session.hpp"

#include "camera/components.hpp"
#include "core/log.hpp"
#include "game/sim_interpolation.hpp"
#include "input/input_command.hpp"
#include "input/input_context.hpp"
#include "input/input_state.hpp"
#include "input/player_command.hpp"
#include "map/bsp.hpp"
#include "map/player_bodies.hpp"
#include "map/thing.hpp"
#include "net/net_state.hpp"
#include "physics/components.hpp"
#include "physics/motored_body.hpp"
#include "physics/rigid_mover.hpp"
#include "render/animation_player.hpp"
#include "render/components.hpp"
#include "script/proc_role.hpp"
#include "script/net_script.hpp"
#include "script/script_context.hpp"
#include "script/script_scope.hpp"

#include <raymath.h>

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace slopengine {

namespace {

constexpr std::size_t kRedundantCommands = 4;

constexpr const char* kAvatarPrefix = "player:";

bool replicated(flecs::entity entity) {
    if (entity.parent().is_valid() || !entity.has<LocalTransformation>() || entity.name() == nullptr) {
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
        }
        Log::info(LogCategory::General, "NET: listening on %s, server key %s", config_.listenAddress.c_str(),
                  secureKeyFingerprint(transport_->publicKey()).c_str());
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
        if (clients_.size() >= config_.maxClients) {
            transport_->close(event.connection, "server full");
            return;
        }
        if (transport_->accept(event.connection)) {
            ServerClient client{};
            client.connection = event.connection;
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
    const std::optional<NetMessageKind> kind = netMessageKind(event.data);
    if (!client.ready) {
        if (kind == NetMessageKind::Hello) {
            serverHello(client, event.data);
        } else {
            serverDrop(event.connection, "expected hello");
        }
        return;
    }
    if (kind != NetMessageKind::Commands) {
        serverDrop(event.connection, "unexpected message");
        return;
    }
    std::vector<NetCommandFrame> frames;
    if (!decodeNetCommands(event.data, frames)) {
        serverDrop(event.connection, "malformed commands");
        return;
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
    while (client.pending.size() > 64) {
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
    if (!world_.has<CurrentMap>()) {
        return;
    }
    transport_->send(
        client.connection,
        encodeNetMapChange({world_.get<CurrentMap>().id, "fresh", client.player}),
        NetSendMode::Reliable);
    serverUpdateKnown(client);
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
        while (!client.pending.empty()) {
            NetCommandFrame frame = std::move(client.pending.front());
            client.pending.pop_front();
            if (pressed.size() < frame.pressed.size()) {
                pressed.resize(frame.pressed.size(), 0);
            }
            for (std::size_t i = 0; i < frame.pressed.size(); ++i) {
                pressed[i] = static_cast<char>(pressed[i] | frame.pressed[i]);
            }
            client.lastCommandTick = frame.tick;
            latest = std::move(frame);
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
            controller->yaw = command.yaw;
            controller->pitch = command.pitch;
        }
        body.set<PlayerCommand>(std::move(command));
    }
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

void NetSession::serverUpdateKnown(ServerClient& client) {
    std::vector<NetIntroduction> added;
    std::vector<std::uint32_t> gone;
    for (const auto& [netId, raw] : serverEntities_) {
        const flecs::entity entity = world_.entity(raw);
        if (serverVisibleTo(client, entity) && client.known.count(netId) == 0) {
            client.known.insert(netId);
            added.push_back({netId, entity.name().c_str(),
                             entity.has<PlayerBody>() ? NetEntityKind::Player : NetEntityKind::Thing});
        }
    }
    for (auto it = client.known.begin(); it != client.known.end();) {
        const auto entity = serverEntities_.find(*it);
        if (entity == serverEntities_.end() || !serverVisibleTo(client, world_.entity(entity->second))) {
            gone.push_back(*it);
            it = client.known.erase(it);
        } else {
            ++it;
        }
    }
    if (!added.empty()) {
        transport_->send(client.connection, encodeNetIntroduce(added), NetSendMode::Reliable);
    }
    if (!gone.empty()) {
        transport_->send(client.connection, encodeNetForget(gone), NetSendMode::Reliable);
    }
}

void NetSession::serverSendEvents(ServerClient& client, const std::vector<NetEventMessage>& events) {
    if (events.empty()) {
        return;
    }
    const std::vector<NetEventDef>& defs = netSchema().events();
    const flecs::entity body = playerBody(world_, client.player);
    const bool haveEye = body.is_valid() && body.has<Lens>();
    const Vector3 eye = haveEye ? body.get<Lens>().camera.position : Vector3{};
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
        } else if (def.to == NetEventAudience::Near && event.where == NetEventWhere::Point) {
            const Vector3 at{event.point[0], event.point[1], event.point[2]};
            send = haveEye && Vector3DistanceSqr(at, eye) <= def.nearRadius * def.nearRadius;
        }
        if (send) {
            (def.reliable ? reliable : unreliable).push_back(event);
        }
    }
    if (!reliable.empty()) {
        transport_->send(client.connection, encodeNetEvents(reliable, defs), NetSendMode::Reliable);
    }
    if (!unreliable.empty()) {
        transport_->send(client.connection, encodeNetEvents(unreliable, defs), NetSendMode::Unreliable);
    }
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
    const std::vector<const NetField*> fields = netSchema().fieldsIn(NetScope{NetScopeKind::Player, {}});
    if (const NetPlayerRecord* record = findNetPlayer(world_, client.player)) {
        for (std::size_t i = 0; i < fields.size() && i < record->values.size(); ++i) {
            if (fields[i]->send != NetAudience::None) {
                snapshot.playerFields.emplace_back(static_cast<int>(i), record->values[i]);
            }
        }
    }
    for (std::uint32_t netId : client.known) {
        const auto raw = serverEntities_.find(netId);
        if (raw == serverEntities_.end()) {
            continue;
        }
        const flecs::entity entity = world_.entity(raw->second);
        const LocalTransformation* local = entity.try_get<LocalTransformation>();
        if (local == nullptr || snapshot.entities.size() >= kMaxNetEntities) {
            continue;
        }
        NetEntityState state;
        state.netId = netId;
        state.position[0] = local->position.x;
        state.position[1] = local->position.y;
        state.position[2] = local->position.z;
        state.rotation[0] = local->rotation.x;
        state.rotation[1] = local->rotation.y;
        state.rotation[2] = local->rotation.z;
        state.rotation[3] = local->rotation.w;
        if (const AnimationPlayer* animation = entity.try_get<AnimationPlayer>()) {
            if (!animation->clipName.empty() && animation->clipName.size() <= 64) {
                state.hasAnimation = true;
                state.clip = animation->clipName;
                state.time = animation->time;
                state.speed = animation->speed;
                state.playing = animation->playing;
                state.loop = animation->loop;
            }
        }
        snapshot.entities.push_back(std::move(state));
    }
    transport_->send(client.connection, encodeNetSnapshot(snapshot, fields), NetSendMode::Unreliable);
}

void NetSession::serverAfterTick() {
    ++serverTick_;
    serverAssignIds();
    std::vector<NetEventMessage> events;
    if (world_.has<NetEventQueue>()) {
        events.swap(world_.get_mut<NetEventQueue>().pending);
    }
    for (auto& entry : clients_) {
        ServerClient& client = entry.second;
        if (!client.ready) {
            continue;
        }
        serverUpdateKnown(client);
        serverSendEvents(client, events);
        serverSendSnapshot(client);
    }
}

void NetSession::clientFrame(float frameDt) {
    std::vector<NetEvent> events;
    transport_->poll(events);
    for (const NetEvent& event : events) {
        clientReceive(event);
    }
    if (world_.has<SimClock>()) {
        SimClock& clock = world_.get_mut<SimClock>();
        clock.accumulator = std::min(kSimTickDt, clock.accumulator + std::max(0.0f, frameDt));
    }
    const int ticks = advanceSimClock(commandClock_, frameDt);
    for (int i = 0; i < ticks; ++i) {
        clientSendCommands();
    }
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
        transport_->send(server_, encodeNetHello(config_.hello), NetSendMode::Reliable);
        return;
    }
    case NetEventKind::Disconnected:
        Log::warning(LogCategory::General, "NET: disconnected: %s", event.reason.c_str());
        connected_ = false;
        server_ = kNoNetConnection;
        world_.remove<SimRemote>();
        names_.clear();
        bound_.clear();
        appeared_.clear();
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
        if (decodeNetMapChange(event.data, change)) {
            names_.clear();
            bound_.clear();
            appeared_.clear();
            yourPlayer_ = change.yourPlayer;
            expectedMap_ = change.mapId;
            lastSnapshotTick_ = 0;
            loggedSnapshots_ = false;
            if (config_.loadMap) {
                config_.loadMap(change.mapId, change.reason);
            }
        }
    } else if (kind == NetMessageKind::Introduce) {
        std::vector<NetIntroduction> entries;
        if (decodeNetIntroduce(event.data, entries)) {
            for (NetIntroduction& entry : entries) {
                if (entry.kind == NetEntityKind::Player && entry.name == yourPlayer_) {
                    continue;
                }
                bound_.erase(entry.netId);
                names_[entry.netId] = std::move(entry);
            }
        }
    } else if (kind == NetMessageKind::Forget) {
        std::vector<std::uint32_t> ids;
        if (decodeNetForget(event.data, ids)) {
            for (std::uint32_t netId : ids) {
                const auto name = names_.find(netId);
                if (name != names_.end() && appeared_.erase(netId) != 0) {
                    callHook("on-player-vanish", netAvatarName(name->second.name));
                }
                if (name != names_.end()) {
                    names_.erase(name);
                }
                bound_.erase(netId);
            }
        }
    } else if (kind == NetMessageKind::Events) {
        std::vector<NetEventMessage> events;
        if (decodeNetEvents(event.data, netSchema().events(), events) && world_.has<ScriptContext>()) {
            s7_scheme* scheme = world_.get<ScriptContext>().scheme;
            for (const NetEventMessage& message : events) {
                deliverNetEvent(scheme, message);
            }
        }
    } else if (kind == NetMessageKind::Snapshot) {
        NetSnapshot snapshot;
        if (decodeNetSnapshot(event.data, netSchema().fieldsIn(NetScope{NetScopeKind::Player, {}}), snapshot) &&
            snapshot.serverTick > lastSnapshotTick_) {
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
    if (!entity.is_valid() && player && mapReady && appeared_.insert(netId).second) {
        callHook("on-player-appear", local);
        entity = world_.lookup(local.c_str());
    }
    if (!entity.is_valid() || !entity.has<LocalTransformation>()) {
        return {};
    }
    bound_[netId] = entity.id();
    return entity;
}

void NetSession::clientSnapshot(const NetSnapshot& snapshot) {
    std::size_t boundCount = 0;
    for (const NetEntityState& state : snapshot.entities) {
        const flecs::entity entity = clientEntity(state.netId);
        if (!entity.is_valid()) {
            continue;
        }
        ++boundCount;
        LocalTransformation target = entity.get<LocalTransformation>();
        target.position = {state.position[0], state.position[1], state.position[2]};
        target.rotation = QuaternionNormalize({state.rotation[0], state.rotation[1], state.rotation[2], state.rotation[3]});
        if (SimPose* pose = entity.try_get_mut<SimPose>()) {
            pose->prev = pose->cur;
            pose->cur = target;
            if (Vector3DistanceSqr(pose->prev.position, pose->cur.position) > kSimPoseSnapDistance * kSimPoseSnapDistance) {
                pose->prev = pose->cur;
            }
        } else {
            entity.set<SimPose>({target, target, target});
        }
        if (state.hasAnimation) {
            if (AnimationPlayer* animation = entity.try_get_mut<AnimationPlayer>()) {
                if (animation->clipName != state.clip) {
                    animation->play(state.clip, state.loop, state.speed);
                    animation->prevTime = state.time;
                } else {
                    animation->prevTime = animation->time;
                }
                animation->time = state.time;
                animation->speed = state.speed;
                animation->loop = state.loop;
                animation->playing = state.playing;
                animation->wrappedLastTick = state.loop && state.time < animation->prevTime;
                animation->poseStale = true;
            }
        }
    }
    const flecs::entity player = localPlayer(world_);
    if (snapshot.hasEye && player.is_valid()) {
        const Vector3 eye{snapshot.eye[0], snapshot.eye[1], snapshot.eye[2]};
        if (SimEyePose* pose = player.try_get_mut<SimEyePose>()) {
            pose->prev = pose->cur;
            pose->cur = eye;
            if (Vector3DistanceSqr(pose->prev, pose->cur) > kSimPoseSnapDistance * kSimPoseSnapDistance) {
                pose->prev = pose->cur;
            }
        } else {
            player.set<SimEyePose>({eye, eye, eye});
        }
    }
    if (world_.has<NetStore>() && !world_.get<NetStore>().players.empty()) {
        std::vector<NetValue>& values = world_.get_mut<NetStore>().players.front().values;
        for (const auto& [slot, value] : snapshot.playerFields) {
            if (static_cast<std::size_t>(slot) < values.size()) {
                values[static_cast<std::size_t>(slot)] = value;
            }
        }
    }
    if (world_.has<SimClock>()) {
        world_.get_mut<SimClock>().accumulator = 0.0f;
    }
    if (!loggedSnapshots_) {
        loggedSnapshots_ = true;
        Log::info(LogCategory::General, "NET: receiving snapshots as %s, %zu of %zu entities bound, eye at %.2f %.2f %.2f",
                  yourPlayer_.c_str(), boundCount, snapshot.entities.size(), snapshot.eye[0], snapshot.eye[1],
                  snapshot.eye[2]);
    }
}

void NetSession::clientSendCommands() {
    if (!connected_ || server_ == kNoNetConnection || !world_.has<InputState>() || !world_.has<InputCommand>()) {
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
    recentCommands_.push_back(std::move(frame));
    while (recentCommands_.size() > kRedundantCommands) {
        recentCommands_.pop_front();
    }
    transport_->send(
        server_,
        encodeNetCommands(std::vector<NetCommandFrame>(recentCommands_.begin(), recentCommands_.end())),
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

std::string netAvatarName(std::string_view player) {
    return std::string(kAvatarPrefix) + std::string(player);
}

}
