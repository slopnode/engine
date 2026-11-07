#pragma once

#include "net/transport.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace slopengine {

using SecureKey = std::array<std::uint8_t, 32>;

inline constexpr std::size_t kSecureOverheadBytes = 26;

struct SecureReplayWindow {
    std::uint64_t highest = 0;
    std::uint64_t seen = 0;
    bool any = false;

    bool accept(std::uint64_t counter);
};

struct SecureTransportOptions {
    std::optional<SecureKey> staticSecret;
    std::optional<SecureKey> pinnedServerKey;
};

class SecureTransport final : public NetTransport {
public:
    SecureTransport(std::unique_ptr<NetTransport> inner, const SecureTransportOptions& options);
    ~SecureTransport() override;

    bool listen(std::string_view address) override;
    NetConnectionId connect(std::string_view address) override;
    bool accept(NetConnectionId connection) override;
    bool send(NetConnectionId connection, std::span<const std::byte> data, NetSendMode mode) override;
    void close(NetConnectionId connection, std::string_view reason) override;
    void poll(std::vector<NetEvent>& out) override;
    NetConnectionStats stats(NetConnectionId connection) const override;

    const SecureKey& publicKey() const { return staticPublic_; }
    std::optional<SecureKey> peerKey(NetConnectionId connection) const;

private:
    enum class Phase {
        AwaitingInner,
        AwaitingHello,
        AwaitingReply,
        AwaitingConfirm,
        Established,
    };

    struct Session {
        bool client = false;
        bool accepted = false;
        Phase phase = Phase::AwaitingInner;
        SecureKey ephemeralSecret{};
        SecureKey ephemeralPublic{};
        SecureKey peerStatic{};
        SecureKey sendKey{};
        SecureKey receiveKey{};
        std::uint64_t sendCounter[2]{};
        std::uint64_t reliableReceive = 0;
        SecureReplayWindow unreliableReceive;
        NetConnectionStats stats;
    };

    void onConnected(NetConnectionId connection, std::vector<NetEvent>& out);
    void onMessage(NetConnectionId connection, const std::vector<std::byte>& data, std::vector<NetEvent>& out);
    void fail(NetConnectionId connection, const char* reason, std::vector<NetEvent>& out);
    void wipe(Session& session);

    std::unique_ptr<NetTransport> inner_;
    SecureTransportOptions options_;
    SecureKey staticSecret_{};
    SecureKey staticPublic_{};
    bool haveStatic_ = false;
    std::map<NetConnectionId, Session> sessions_;
};

std::unique_ptr<SecureTransport> createSecureTransport(
    std::unique_ptr<NetTransport> inner,
    const SecureTransportOptions& options = {});
std::string secureKeyFingerprint(const SecureKey& key);
bool generateSecureKey(SecureKey& secret);

}
