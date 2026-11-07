#pragma once

#include "net/net_protocol.hpp"
#include "net/net_schema.hpp"

#include <flecs.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

inline constexpr const char* kLocalNetPlayerId = "Player";

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
    std::vector<NetEventMessage> pending;
};

struct NetTarget {
    NetScopeKind kind = NetScopeKind::Session;
    std::size_t player = 0;
    flecs::entity thing;
};

void registerNetState(flecs::world& world);
void resetNetState(flecs::world& world);
void resetNetMapFields(flecs::world& world);
bool resolveNetTarget(const flecs::world& world, std::string_view id, NetTarget& out);
NetValue* findNetValue(flecs::world& world, const NetTarget& target, std::string_view field, const NetField** fieldOut);
std::vector<std::pair<const NetField*, const NetValue*>> netTargetFields(flecs::world& world, const NetTarget& target);

}
