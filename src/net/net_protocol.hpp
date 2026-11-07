#pragma once

#include "net/net_message.hpp"
#include "net/net_schema.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace slopengine {

inline constexpr std::uint32_t kNetProtocolVersion = 1;
inline constexpr std::size_t kMaxNetCommandsPerMessage = 8;
inline constexpr std::size_t kMaxNetActions = 256;
inline constexpr std::size_t kMaxNetEntities = 4096;

enum class NetMessageKind : std::uint8_t {
    Hello = 1,
    MapChange = 2,
    Introduce = 3,
    Forget = 4,
    Commands = 5,
    Snapshot = 6,
    Events = 7,
};

enum class NetEventWhere : std::uint8_t {
    None = 0,
    Point = 1,
    Player = 2,
};

struct NetEventMessage {
    std::uint32_t eventId = 0;
    NetEventWhere where = NetEventWhere::None;
    float point[3]{};
    std::string player;
    std::vector<NetValue> args;
};

struct NetPackageVersion {
    std::string id;
    std::string version;

    bool operator==(const NetPackageVersion&) const = default;
};

struct NetHello {
    std::uint32_t protocol = kNetProtocolVersion;
    std::string engineVersion;
    std::uint64_t schemaHash = 0;
    std::vector<NetPackageVersion> packages;
};

struct NetMapChange {
    std::string mapId;
    std::string reason;
};

struct NetIntroduction {
    std::uint32_t netId = 0;
    std::string name;
};

struct NetCommandFrame {
    std::uint64_t tick = 0;
    float moveForward = 0.0f;
    float moveStrafe = 0.0f;
    float moveUp = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
    std::vector<char> down;
    std::vector<char> pressed;
};

struct NetEntityState {
    std::uint32_t netId = 0;
    float position[3]{};
    float rotation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    bool hasAnimation = false;
    std::string clip;
    float time = 0.0f;
    float speed = 1.0f;
    bool playing = false;
    bool loop = false;
};

struct NetSnapshot {
    std::uint64_t serverTick = 0;
    std::uint64_t ackedCommand = 0;
    bool hasEye = false;
    float eye[3]{};
    std::vector<std::pair<int, NetValue>> playerFields;
    std::vector<NetEntityState> entities;
};

std::optional<NetMessageKind> netMessageKind(std::span<const std::byte> data);

std::vector<std::byte> encodeNetHello(const NetHello& hello);
bool decodeNetHello(std::span<const std::byte> data, NetHello& out);
std::vector<std::byte> encodeNetMapChange(const NetMapChange& change);
bool decodeNetMapChange(std::span<const std::byte> data, NetMapChange& out);
std::vector<std::byte> encodeNetIntroduce(const std::vector<NetIntroduction>& entries);
bool decodeNetIntroduce(std::span<const std::byte> data, std::vector<NetIntroduction>& out);
std::vector<std::byte> encodeNetForget(const std::vector<std::uint32_t>& netIds);
bool decodeNetForget(std::span<const std::byte> data, std::vector<std::uint32_t>& out);
std::vector<std::byte> encodeNetCommands(const std::vector<NetCommandFrame>& frames);
bool decodeNetCommands(std::span<const std::byte> data, std::vector<NetCommandFrame>& out);
std::vector<std::byte> encodeNetEvents(const std::vector<NetEventMessage>& events, const std::vector<NetEventDef>& defs);
bool decodeNetEvents(
    std::span<const std::byte> data,
    const std::vector<NetEventDef>& defs,
    std::vector<NetEventMessage>& out);
std::vector<std::byte> encodeNetSnapshot(const NetSnapshot& snapshot, const std::vector<const NetField*>& playerFields);
bool decodeNetSnapshot(
    std::span<const std::byte> data,
    const std::vector<const NetField*>& playerFields,
    NetSnapshot& out);

}
