#pragma once

#include "net/net_protocol.hpp"
#include "net/net_schema.hpp"

#include <flecs.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace slopengine {

inline constexpr const char* kLocalNetPlayerId = "Player";
inline constexpr std::size_t kMaxPredictedEvents = 512;

struct NetStandIn {};

struct NetHostStandIn {};

struct NetIdentity {
    std::string player = kLocalNetPlayerId;
};

struct NetProfiles {
    std::vector<std::pair<std::string, std::vector<std::pair<int, NetValue>>>> players;
};

struct NetRoster {
    std::vector<NetRosterEntry> entries;
};

struct NetSessionLimits {
    std::string kind;
    int maxPlayers = 8;
    int playerCeiling = 8;
    bool joinable = true;
    bool remote = false;
};

using NetTravelRecord = std::vector<std::pair<std::string, NetValue>>;

struct NetRejoins {
    std::vector<std::pair<std::string, NetTravelRecord>> players;
};

struct NetPlayerRecord {
    std::string id;
    std::vector<NetValue> values;
};

struct NetStore {
    std::vector<NetPlayerRecord> players;
    std::vector<NetValue> session;
};

struct NetThingFields {
    std::vector<int> fieldIds;
    std::vector<NetValue> values;
};

struct NetEventQueue {
    bool forward = false;
    bool localPlayerHere = true;
    std::vector<NetEventMessage> pending;
};

struct NetSevered {
    std::vector<std::string> bones;
    std::vector<std::string> hidden;
};

struct NetAttachments {
    std::vector<NetAttachment> list;
};

struct NetPredictedEvent {
    std::uint32_t eventId = 0;
    std::uint64_t tick = 0;
    bool matched = false;
};

struct NetPrediction {
    bool active = false;
    std::uint64_t tick = 0;
    std::uint64_t ackedTick = 0;
    std::vector<NetPredictedEvent> raised;
};

struct NetEffectQueue {
    bool forward = false;
    std::vector<std::pair<flecs::entity_t, NetEffect>> pending;
};

struct NetSpawned {};

void queueNetEffect(flecs::world& world, flecs::entity entity, NetEffect effect);

struct NetTarget {
    NetScopeKind kind = NetScopeKind::Session;
    std::size_t player = 0;
    flecs::entity thing;
};

void registerNetState(flecs::world& world);
void resetNetState(flecs::world& world);
void resetNetSessionState(flecs::world& world);
void addNetPlayer(flecs::world& world, std::string_view id);
void removeNetPlayer(flecs::world& world, std::string_view id);
void resetNetPlayer(flecs::world& world, std::string_view id);
const NetPlayerRecord* findNetPlayer(const flecs::world& world, std::string_view id);
std::string netDisplayName(const flecs::world& world, std::string_view id);
bool setNetProfile(flecs::world& world, std::string_view id, const NetProfile& profile);
void clearNetProfile(flecs::world& world, std::string_view id);
void resetNetMapFields(flecs::world& world);
NetTravelRecord netTravelRecord(flecs::world& world, std::string_view id);
const NetTravelRecord* findNetRejoin(const flecs::world& world, std::string_view id);
void setNetRejoin(flecs::world& world, std::string_view id, NetTravelRecord record);
void clearNetRejoin(flecs::world& world, std::string_view id);
bool resolveNetTarget(const flecs::world& world, std::string_view id, NetTarget& out);
NetValue* findNetValue(flecs::world& world, const NetTarget& target, std::string_view field, const NetField** fieldOut);
std::vector<std::pair<const NetField*, const NetValue*>> netTargetFields(flecs::world& world, const NetTarget& target);

}
