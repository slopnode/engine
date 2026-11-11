#include "test_assert.hpp"

#include "net/memory_transport.hpp"
#include "net/secure_transport.hpp"

#include <algorithm>
#include <cstring>
#include <string>

namespace slopengine {

namespace {

std::vector<std::byte> bytes(const std::string& text) {
    std::vector<std::byte> out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

std::string text(const std::vector<std::byte>& data) {
    return std::string(reinterpret_cast<const char*>(data.data()), data.size());
}

struct Held {
    NetConnectionId connection;
    std::vector<std::byte> data;
    NetSendMode mode;
};

class TapTransport final : public NetTransport {
public:
    explicit TapTransport(std::unique_ptr<NetTransport> inner) : inner_(std::move(inner)) {}

    bool listen(std::string_view address) override { return inner_->listen(address); }
    NetConnectionId connect(std::string_view address) override { return inner_->connect(address); }
    bool accept(NetConnectionId connection) override { return inner_->accept(connection); }
    void close(NetConnectionId connection, std::string_view reason) override {
        inner_->close(connection, forgeReason.empty() ? reason : std::string_view(forgeReason));
    }
    void poll(std::vector<NetEvent>& out) override { inner_->poll(out); }
    NetConnectionStats stats(NetConnectionId connection) const override { return inner_->stats(connection); }

    bool send(NetConnectionId connection, std::span<const std::byte> data, NetSendMode mode) override {
        std::vector<std::byte> frame(data.begin(), data.end());
        if (corruptNext && !frame.empty()) {
            frame.back() ^= std::byte{0x01};
            corruptNext = false;
        }
        sent.push_back(Held{connection, frame, mode});
        if (hold) {
            held.push_back(Held{connection, frame, mode});
            return true;
        }
        return inner_->send(connection, frame, mode);
    }

    bool resend(const Held& item) { return inner_->send(item.connection, item.data, item.mode); }

    bool corruptNext = false;
    bool hold = false;
    std::string forgeReason;
    std::vector<Held> sent;
    std::vector<Held> held;

private:
    std::unique_ptr<NetTransport> inner_;
};

struct Link {
    std::unique_ptr<SecureTransport> server;
    std::unique_ptr<SecureTransport> client;
    TapTransport* serverTap = nullptr;
    TapTransport* clientTap = nullptr;
    NetConnectionId toServer = kNoNetConnection;
    NetConnectionId toClient = kNoNetConnection;
    std::vector<NetEvent> serverEvents;
    std::vector<NetEvent> clientEvents;

    void pump() {
        for (int i = 0; i < 8; ++i) {
            server->poll(serverEvents);
            client->poll(clientEvents);
        }
    }
};

bool has(const std::vector<NetEvent>& events, NetEventKind kind, std::string_view reason = {}) {
    return std::any_of(events.begin(), events.end(), [&](const NetEvent& event) {
        return event.kind == kind && (reason.empty() || event.reason == reason);
    });
}

Link makeLink(MemoryNetwork& network, const SecureTransportOptions& serverOptions, const SecureTransportOptions& clientOptions) {
    Link link;
    auto serverTap = std::make_unique<TapTransport>(network.endpoint());
    auto clientTap = std::make_unique<TapTransport>(network.endpoint());
    link.serverTap = serverTap.get();
    link.clientTap = clientTap.get();
    link.server = createSecureTransport(std::move(serverTap), serverOptions);
    link.client = createSecureTransport(std::move(clientTap), clientOptions);
    link.server->listen("server");
    link.toServer = link.client->connect("server");
    link.server->poll(link.serverEvents);
    for (const NetEvent& event : link.serverEvents) {
        if (event.kind == NetEventKind::Incoming) {
            link.toClient = event.connection;
            link.server->accept(event.connection);
        }
    }
    link.pump();
    return link;
}

void clear(Link& link) {
    link.serverEvents.clear();
    link.clientEvents.clear();
    link.serverTap->sent.clear();
    link.clientTap->sent.clear();
}

int messages(const std::vector<NetEvent>& events) {
    return static_cast<int>(std::count_if(
        events.begin(), events.end(), [](const NetEvent& event) { return event.kind == NetEventKind::Message; }));
}

} // namespace

void runSecureTransportTests() {
    {
        SecureReplayWindow window;
        CHECK(window.accept(5));
        CHECK(window.accept(3));
        CHECK_FALSE(window.accept(3));
        CHECK_FALSE(window.accept(5));
        CHECK(window.accept(70));
        CHECK_FALSE(window.accept(5));
        CHECK(window.accept(7));
        CHECK_FALSE(window.accept(7));
        CHECK(window.accept(1000));
        CHECK_FALSE(window.accept(70));
    }

    {
        MemoryNetwork network;
        Link link = makeLink(network, {}, {});
        CHECK(has(link.serverEvents, NetEventKind::Connected));
        CHECK(has(link.clientEvents, NetEventKind::Connected));
        const std::optional<SecureKey> seen = link.client->peerKey(link.toServer);
        CHECK(seen.has_value() && *seen == link.server->publicKey());
        CHECK_EQ(static_cast<int>(secureKeyFingerprint(link.server->publicKey()).size()), 39);
        clear(link);

        CHECK(link.client->send(link.toServer, bytes("secret-payload"), NetSendMode::Reliable));
        CHECK(link.server->send(link.toClient, bytes("secret-reply"), NetSendMode::Unreliable));
        CHECK_FALSE(link.client->send(
            link.toServer, std::vector<std::byte>(kMaxNetMessageBytes - kSecureOverheadBytes + 1), NetSendMode::Reliable));
        link.pump();
        CHECK(messages(link.serverEvents) == 1 && text(link.serverEvents[0].data) == "secret-payload");
        CHECK(messages(link.clientEvents) == 1 && text(link.clientEvents[0].data) == "secret-reply");
        const std::string wire = text(link.clientTap->sent.at(0).data);
        CHECK(wire.find("secret-payload") == std::string::npos);
        CHECK_EQ(static_cast<int>(wire.size()), static_cast<int>(14 + kSecureOverheadBytes));
        CHECK(link.client->stats(link.toServer).sentMessages == 1);
        CHECK(link.server->stats(link.toClient).receivedMessages == 1);
        clear(link);

        CHECK(link.client->send(link.toServer, bytes("once"), NetSendMode::Unreliable));
        link.pump();
        link.clientTap->resend(link.clientTap->sent.at(0));
        link.pump();
        CHECK_EQ(messages(link.serverEvents), 1);
        clear(link);

        link.clientTap->hold = true;
        for (int i = 0; i < 3; ++i) {
            CHECK(link.client->send(link.toServer, bytes("u" + std::to_string(i)), NetSendMode::Unreliable));
        }
        link.clientTap->hold = false;
        link.clientTap->resend(link.clientTap->held[2]);
        link.clientTap->resend(link.clientTap->held[0]);
        link.clientTap->resend(link.clientTap->held[1]);
        link.clientTap->resend(link.clientTap->held[0]);
        link.pump();
        CHECK_EQ(messages(link.serverEvents), 3);
        clear(link);

        link.clientTap->corruptNext = true;
        CHECK(link.client->send(link.toServer, bytes("tampered"), NetSendMode::Unreliable));
        link.pump();
        CHECK_EQ(messages(link.serverEvents), 0);
        CHECK_FALSE(has(link.serverEvents, NetEventKind::Disconnected));

        link.clientTap->corruptNext = true;
        CHECK(link.client->send(link.toServer, bytes("tampered"), NetSendMode::Reliable));
        link.pump();
        CHECK_EQ(messages(link.serverEvents), 0);
        CHECK(has(link.serverEvents, NetEventKind::Disconnected, "bad message"));
        CHECK(has(link.clientEvents, NetEventKind::Disconnected, "bad message"));
    }

    {
        MemoryNetwork network;
        Link link = makeLink(network, {}, {});
        clear(link);
        CHECK(link.client->send(link.toServer, bytes("first"), NetSendMode::Reliable));
        link.pump();
        link.clientTap->resend(link.clientTap->sent.at(0));
        link.pump();
        CHECK_EQ(messages(link.serverEvents), 1);
        CHECK(has(link.serverEvents, NetEventKind::Disconnected, "bad message"));
    }

    {
        SecureKey secret{};
        CHECK(generateSecureKey(secret));
        SecureTransportOptions serverOptions;
        serverOptions.staticSecret = secret;
        MemoryNetwork scratch;
        SecureKey expected = createSecureTransport(scratch.endpoint(), serverOptions)->publicKey();

        MemoryNetwork network;
        SecureTransportOptions pinned;
        pinned.pinnedServerKey = expected;
        Link good = makeLink(network, serverOptions, pinned);
        CHECK(has(good.clientEvents, NetEventKind::Connected));
        CHECK(good.server->publicKey() == expected);

        MemoryNetwork other;
        Link impostor = makeLink(other, {}, pinned);
        CHECK_FALSE(has(impostor.clientEvents, NetEventKind::Connected));
        CHECK(has(impostor.clientEvents, NetEventKind::Disconnected, "server key mismatch"));
        CHECK_FALSE(has(impostor.serverEvents, NetEventKind::Connected));
        CHECK(has(impostor.serverEvents, NetEventKind::Disconnected));
    }

    {
        MemoryNetwork network;
        Link link;
        auto serverTap = std::make_unique<TapTransport>(network.endpoint());
        link.serverTap = serverTap.get();
        link.server = createSecureTransport(std::move(serverTap));
        link.client = createSecureTransport(std::make_unique<TapTransport>(network.endpoint()));
        link.server->listen("server");
        link.toServer = link.client->connect("server");
        link.server->poll(link.serverEvents);
        link.toClient = link.serverEvents.at(0).connection;
        link.server->accept(link.toClient);
        link.serverTap->corruptNext = true;
        link.pump();
        CHECK(has(link.clientEvents, NetEventKind::Disconnected, "handshake failed"));
        CHECK_FALSE(has(link.clientEvents, NetEventKind::Connected));
        CHECK_FALSE(has(link.serverEvents, NetEventKind::Connected));
    }

    {
        MemoryNetwork network;
        Link link = makeLink(network, {}, {});
        CHECK(has(link.clientEvents, NetEventKind::Connected));
        link.serverTap->forgeReason = "forged";
        link.server->close(link.toClient, "kicked for spam");
        link.pump();
        CHECK(has(link.clientEvents, NetEventKind::Disconnected, "kicked for spam"));
        CHECK_FALSE(has(link.clientEvents, NetEventKind::Disconnected, "forged"));
    }

    {
        MemoryNetwork network;
        Link link = makeLink(network, {}, {});
        link.serverTap->forgeReason = "forged";
        link.serverTap->hold = true;
        link.server->close(link.toClient, "kicked for spam");
        link.pump();
        CHECK(has(link.clientEvents, NetEventKind::Disconnected, "connection lost"));
        CHECK_FALSE(has(link.clientEvents, NetEventKind::Disconnected, "forged"));
    }
}

}
