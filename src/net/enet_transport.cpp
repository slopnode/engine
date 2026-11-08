#include "net/enet_transport.hpp"

#include "core/log.hpp"

#include <enet/enet.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

namespace slopengine {

namespace {

constexpr enet_uint8 kReliableChannel = 0;
constexpr enet_uint8 kUnreliableChannel = 1;
constexpr std::size_t kChannelCount = 2;
constexpr enet_uint32 kTimeoutLimit = 32;
constexpr std::chrono::milliseconds kShutdownGrace{250};
constexpr std::size_t kMaxReasonBytes = 256;
constexpr std::size_t kMaxWaitingBytes = 4 * (kMaxNetMessageBytes + 1);

enum class Frame : std::uint8_t {
    Data = 0,
    Accept = 1,
    Reason = 2,
};

int g_users = 0;

bool acquireLibrary() {
    if (g_users == 0 && enet_initialize() != 0) {
        Log::error(LogCategory::General, "NET: ENet failed to start");
        return false;
    }
    ++g_users;
    return true;
}

void releaseLibrary() {
    if (g_users > 0 && --g_users == 0) {
        enet_deinitialize();
    }
}

bool parsePort(std::string_view text, enet_uint16& out) {
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return !text.empty() && ec == std::errc{} && end == text.data() + text.size();
}

bool parseAddress(std::string_view text, bool listening, ENetAddress& out) {
    out.host = ENET_HOST_ANY;
    out.port = 0;
    const std::size_t colon = text.rfind(':');
    if (colon == std::string_view::npos) {
        return listening && parsePort(text, out.port);
    }
    const std::string host(text.substr(0, colon));
    if (!parsePort(text.substr(colon + 1), out.port)) {
        return false;
    }
    if (host.empty()) {
        return listening;
    }
    return enet_address_set_host(&out, host.c_str()) == 0;
}

NetConnectionId peerId(const ENetPeer* peer) {
    return static_cast<NetConnectionId>(reinterpret_cast<std::uintptr_t>(peer->data));
}

class EnetTransport final : public NetTransport {
public:
    explicit EnetTransport(const EnetTransportOptions& options) : options_(options), ready_(acquireLibrary()) {}

    ~EnetTransport() override {
        if (host_ != nullptr) {
            for (auto& entry : peers_) {
                if (!entry.second.closing) {
                    enet_peer_disconnect_later(entry.second.peer, 0);
                }
            }
            const auto deadline = std::chrono::steady_clock::now() + kShutdownGrace;
            ENetEvent event{};
            while (!peers_.empty() && std::chrono::steady_clock::now() < deadline) {
                if (enet_host_service(host_, &event, 10) <= 0) {
                    continue;
                }
                if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                    enet_packet_destroy(event.packet);
                } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
                    event.peer->data = nullptr;
                    peers_.erase(peerId(event.peer));
                }
            }
            for (auto& entry : peers_) {
                enet_peer_disconnect_now(entry.second.peer, 0);
            }
            enet_host_destroy(host_);
        }
        if (ready_) {
            releaseLibrary();
        }
    }

    bool listen(std::string_view address) override {
        const std::lock_guard lock(mutex_);
        ENetAddress local{};
        if (!ready_ || host_ != nullptr || !parseAddress(address, true, local)) {
            return false;
        }
        host_ = enet_host_create(&local, options_.maxPeers, kChannelCount, 0, 0);
        if (host_ == nullptr) {
            return false;
        }
        bound(*host_);
        host_->duplicatePeers = options_.maxPeersPerAddress;
        return true;
    }

    NetConnectionId connect(std::string_view address) override {
        const std::lock_guard lock(mutex_);
        ENetAddress remote{};
        if (!ready_ || !parseAddress(address, false, remote)) {
            return kNoNetConnection;
        }
        if (host_ == nullptr) {
            host_ = enet_host_create(nullptr, options_.maxPeers, kChannelCount, 0, 0);
            if (host_ == nullptr) {
                return kNoNetConnection;
            }
            bound(*host_);
        }
        ENetPeer* peer = enet_host_connect(host_, &remote, kChannelCount, 0);
        if (peer == nullptr) {
            return kNoNetConnection;
        }
        const auto timeout = static_cast<enet_uint32>(options_.connectTimeoutMs);
        enet_peer_timeout(peer, kTimeoutLimit, timeout, timeout);
        return track(peer, false);
    }

    bool accept(NetConnectionId connection) override {
        const std::lock_guard lock(mutex_);
        Peer* peer = find(connection);
        if (peer == nullptr || !peer->incoming || peer->accepted || peer->closing) {
            return false;
        }
        peer->accepted = true;
        settle(peer->peer);
        sendFrame(*peer, Frame::Accept, {}, kReliableChannel, ENET_PACKET_FLAG_RELIABLE);
        local_.push_back(NetEvent{NetEventKind::Connected, connection, {}, {}});
        return true;
    }

    bool send(NetConnectionId connection, std::span<const std::byte> data, NetSendMode mode) override {
        const std::lock_guard lock(mutex_);
        Peer* peer = find(connection);
        if (peer == nullptr || !peer->accepted || peer->closing || data.size() > kMaxNetMessageBytes) {
            return false;
        }
        const bool reliable = mode == NetSendMode::Reliable;
        if (!sendFrame(
                *peer,
                Frame::Data,
                data,
                reliable ? kReliableChannel : kUnreliableChannel,
                reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED)) {
            return false;
        }
        ++peer->stats.sentMessages;
        return true;
    }

    void close(NetConnectionId connection, std::string_view reason) override {
        const std::lock_guard lock(mutex_);
        Peer* peer = find(connection);
        if (peer == nullptr || peer->closing) {
            return;
        }
        const auto* text = reinterpret_cast<const std::byte*>(reason.data());
        sendFrame(*peer, Frame::Reason, {text, std::min(reason.size(), kMaxReasonBytes)}, kReliableChannel, ENET_PACKET_FLAG_RELIABLE);
        enet_peer_disconnect_later(peer->peer, 0);
        peer->closing = true;
        if (host_ != nullptr) {
            enet_host_flush(host_);
        }
    }

    void poll(std::vector<NetEvent>& out) override {
        const std::lock_guard lock(mutex_);
        for (NetEvent& event : local_) {
            out.push_back(std::move(event));
        }
        local_.clear();
        service(out);
    }

    void keepAlive() override {
        const std::lock_guard lock(mutex_);
        service(local_);
    }

    NetConnectionStats stats(NetConnectionId connection) const override {
        const std::lock_guard lock(mutex_);
        const auto found = peers_.find(connection);
        if (found == peers_.end()) {
            return {};
        }
        NetConnectionStats out = found->second.stats;
        out.pingMs = static_cast<float>(found->second.peer->roundTripTime);
        out.lossFraction =
            static_cast<float>(found->second.peer->packetLoss) / static_cast<float>(ENET_PEER_PACKET_LOSS_SCALE);
        return out;
    }

private:
    void service(std::vector<NetEvent>& out) {
        if (host_ == nullptr) {
            return;
        }
        ENetEvent event{};
        while (enet_host_service(host_, &event, 0) > 0) {
            switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                if (peerId(event.peer) == kNoNetConnection) {
                    out.push_back(NetEvent{NetEventKind::Incoming, track(event.peer, true), {}, {}});
                } else {
                    settle(event.peer);
                }
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                receive(event, out);
                enet_packet_destroy(event.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                disconnected(peerId(event.peer), out);
                break;
            case ENET_EVENT_TYPE_NONE:
                break;
            }
        }
        enet_host_flush(host_);
    }

    struct Peer {
        ENetPeer* peer = nullptr;
        bool incoming = false;
        bool accepted = false;
        bool closing = false;
        std::string reason;
        NetConnectionStats stats;
    };

    static void bound(ENetHost& host) {
        host.maximumPacketSize = kMaxNetMessageBytes + 1;
        host.maximumWaitingData = kMaxWaitingBytes;
    }

    NetConnectionId track(ENetPeer* peer, bool incoming) {
        const NetConnectionId id = nextId_++;
        peer->data = reinterpret_cast<void*>(static_cast<std::uintptr_t>(id));
        Peer entry{};
        entry.peer = peer;
        entry.incoming = incoming;
        peers_[id] = entry;
        return id;
    }

    Peer* find(NetConnectionId connection) {
        const auto found = peers_.find(connection);
        return found == peers_.end() ? nullptr : &found->second;
    }

    bool sendFrame(Peer& peer, Frame frame, std::span<const std::byte> data, enet_uint8 channel, enet_uint32 flags) {
        ENetPacket* packet = enet_packet_create(nullptr, data.size() + 1, flags);
        if (packet == nullptr) {
            return false;
        }
        packet->data[0] = static_cast<enet_uint8>(frame);
        if (!data.empty()) {
            std::memcpy(packet->data + 1, data.data(), data.size());
        }
        if (enet_peer_send(peer.peer, channel, packet) != 0) {
            enet_packet_destroy(packet);
            return false;
        }
        return true;
    }

    void receive(const ENetEvent& event, std::vector<NetEvent>& out) {
        Peer* peer = find(peerId(event.peer));
        if (peer == nullptr || event.packet->dataLength == 0) {
            return;
        }
        const auto* payload = reinterpret_cast<const std::byte*>(event.packet->data) + 1;
        const std::size_t size = event.packet->dataLength - 1;
        switch (static_cast<Frame>(event.packet->data[0])) {
        case Frame::Data:
            if (peer->accepted && !peer->closing) {
                ++peer->stats.receivedMessages;
                out.push_back(NetEvent{NetEventKind::Message, peerId(event.peer), {payload, payload + size}, {}});
            }
            break;
        case Frame::Accept:
            if (!peer->incoming && !peer->accepted) {
                peer->accepted = true;
                out.push_back(NetEvent{NetEventKind::Connected, peerId(event.peer), {}, {}});
            }
            break;
        case Frame::Reason:
            peer->reason.assign(reinterpret_cast<const char*>(payload), std::min(size, kMaxReasonBytes));
            break;
        }
    }

    void settle(ENetPeer* peer) const {
        const auto stall = static_cast<enet_uint32>(std::max(options_.connectTimeoutMs, options_.stallTimeoutMs));
        enet_peer_timeout(peer, kTimeoutLimit, stall, stall);
    }

    void disconnected(NetConnectionId connection, std::vector<NetEvent>& out) {
        const auto found = peers_.find(connection);
        if (found == peers_.end()) {
            return;
        }
        const Peer& peer = found->second;
        peer.peer->data = nullptr;
        if (!peer.closing) {
            std::string reason = peer.reason;
            if (reason.empty()) {
                reason = peer.accepted || peer.incoming ? "connection lost" : "no response";
            }
            out.push_back(NetEvent{NetEventKind::Disconnected, connection, {}, std::move(reason)});
        }
        peers_.erase(found);
    }

    EnetTransportOptions options_;
    bool ready_ = false;
    ENetHost* host_ = nullptr;
    NetConnectionId nextId_ = 1;
    std::map<NetConnectionId, Peer> peers_;
    std::vector<NetEvent> local_;
    mutable std::mutex mutex_;
};

} // namespace

std::unique_ptr<NetTransport> createEnetTransport(const EnetTransportOptions& options) {
    return std::make_unique<EnetTransport>(options);
}

}
