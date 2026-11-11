#include "game/net_control.hpp"

#include <utility>

namespace slopengine {

namespace {

std::optional<NetControlRequest> g_pendingNetControl;

}

void requestNetControl(NetControlRequest request) {
    g_pendingNetControl = std::move(request);
}

std::optional<NetControlRequest> takeNetControlRequest() {
    std::optional<NetControlRequest> pending = std::move(g_pendingNetControl);
    g_pendingNetControl.reset();
    return pending;
}

const char* netStatusName(NetStatusKind kind) {
    switch (kind) {
    case NetStatusKind::Hosting:
        return "hosting";
    case NetStatusKind::Connecting:
        return "connecting";
    case NetStatusKind::Connected:
        return "connected";
    case NetStatusKind::Offline:
        break;
    }
    return "offline";
}

}
