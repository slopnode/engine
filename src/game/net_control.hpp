#pragma once

#include <optional>
#include <string>

namespace slopengine {

inline constexpr int kDefaultNetPort = 27960;

enum class NetControlKind {
    Host,
    Join,
    Leave,
};

struct NetControlRequest {
    NetControlKind kind = NetControlKind::Leave;
    std::string address;
    std::string session;
    std::string map;
    int maxPlayers = 0;
};

enum class NetStatusKind {
    Offline,
    Hosting,
    Connecting,
    Connected,
};

struct NetStatus {
    NetStatusKind kind = NetStatusKind::Offline;
    std::string address;
    std::string session;
    std::string error;
    int players = 0;
    int maxPlayers = 0;
};

void requestNetControl(NetControlRequest request);
std::optional<NetControlRequest> takeNetControlRequest();

const char* netStatusName(NetStatusKind kind);

}
