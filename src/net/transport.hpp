#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

using NetConnectionId = std::uint32_t;

inline constexpr NetConnectionId kNoNetConnection = 0;
inline constexpr std::size_t kMaxNetMessageBytes = 512 * 1024;

enum class NetSendMode {
    Unreliable,
    Reliable,
};

enum class NetEventKind {
    Incoming,
    Connected,
    Disconnected,
    Message,
};

struct NetEvent {
    NetEventKind kind = NetEventKind::Message;
    NetConnectionId connection = kNoNetConnection;
    std::vector<std::byte> data;
    std::string reason;
};

struct NetConnectionStats {
    float pingMs = 0.0f;
    float lossFraction = 0.0f;
    std::uint64_t sentMessages = 0;
    std::uint64_t receivedMessages = 0;
};

class NetTransport {
public:
    virtual ~NetTransport() = default;

    virtual bool listen(std::string_view address) = 0;
    virtual NetConnectionId connect(std::string_view address) = 0;
    virtual bool accept(NetConnectionId connection) = 0;
    virtual bool send(NetConnectionId connection, std::span<const std::byte> data, NetSendMode mode) = 0;
    virtual void close(NetConnectionId connection, std::string_view reason) = 0;
    virtual void poll(std::vector<NetEvent>& out) = 0;
    virtual NetConnectionStats stats(NetConnectionId connection) const = 0;
    virtual void keepAlive() {}
    virtual void setPeersPerAddress(std::size_t) {}
};

}
