#include "test_assert.hpp"

#include "net/enet_transport.hpp"
#include "net/secure_transport.hpp"

#include <enet/enet.h>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

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

template <typename Done>
bool pumpUntil(NetTransport& a, NetTransport& b, std::vector<NetEvent>& eventsA, std::vector<NetEvent>& eventsB, Done done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        a.poll(eventsA);
        b.poll(eventsB);
        if (done()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

const NetEvent* first(const std::vector<NetEvent>& events, NetEventKind kind) {
    for (const NetEvent& event : events) {
        if (event.kind == kind) {
            return &event;
        }
    }
    return nullptr;
}

int listenSomewhere(NetTransport& server) {
    for (int candidate = 27900; candidate < 27950; ++candidate) {
        if (server.listen(std::to_string(candidate))) {
            return candidate;
        }
    }
    return 0;
}

} // namespace

void runEnetTransportTests() {
    {
        auto server = createEnetTransport();
        auto client = createEnetTransport();
        const int port = listenSomewhere(*server);
        CHECK(port != 0);
        CHECK_FALSE(server->listen(std::to_string(port + 1)));

        const NetConnectionId toServer = client->connect("127.0.0.1:" + std::to_string(port));
        CHECK(toServer != kNoNetConnection);
        CHECK_FALSE(client->send(toServer, bytes("early"), NetSendMode::Reliable));

        std::vector<NetEvent> serverEvents;
        std::vector<NetEvent> clientEvents;
        CHECK(pumpUntil(*server, *client, serverEvents, clientEvents, [&] {
            return first(serverEvents, NetEventKind::Incoming) != nullptr;
        }));
        const NetConnectionId toClient = serverEvents.empty() ? kNoNetConnection : serverEvents[0].connection;
        CHECK(first(clientEvents, NetEventKind::Connected) == nullptr);
        CHECK_FALSE(server->send(toClient, bytes("early"), NetSendMode::Reliable));
        CHECK(server->accept(toClient));
        CHECK_FALSE(server->accept(toClient));
        CHECK(pumpUntil(*server, *client, serverEvents, clientEvents, [&] {
            return first(serverEvents, NetEventKind::Connected) && first(clientEvents, NetEventKind::Connected);
        }));
        serverEvents.clear();
        clientEvents.clear();

        for (int i = 0; i < 50; ++i) {
            CHECK(client->send(toServer, bytes(std::to_string(i)), NetSendMode::Reliable));
        }
        CHECK(server->send(toClient, bytes("welcome"), NetSendMode::Reliable));
        CHECK(server->send(toClient, bytes("snapshot"), NetSendMode::Unreliable));
        CHECK_FALSE(client->send(toServer, std::vector<std::byte>(kMaxNetMessageBytes + 1), NetSendMode::Reliable));
        CHECK(pumpUntil(*server, *client, serverEvents, clientEvents, [&] {
            return serverEvents.size() >= 50 && clientEvents.size() >= 2;
        }));
        bool ordered = serverEvents.size() == 50;
        for (std::size_t i = 0; ordered && i < serverEvents.size(); ++i) {
            ordered = serverEvents[i].kind == NetEventKind::Message && serverEvents[i].connection == toClient &&
                text(serverEvents[i].data) == std::to_string(i);
        }
        CHECK(ordered);
        CHECK(clientEvents.size() == 2 && text(clientEvents[0].data) == "welcome" && text(clientEvents[1].data) == "snapshot");
        CHECK(client->stats(toServer).sentMessages == 50);
        CHECK(server->stats(toClient).receivedMessages == 50);
        serverEvents.clear();
        clientEvents.clear();

        client->close(toServer, "bye");
        CHECK_FALSE(client->send(toServer, bytes("after"), NetSendMode::Reliable));
        CHECK(pumpUntil(*server, *client, serverEvents, clientEvents, [&] {
            return first(serverEvents, NetEventKind::Disconnected) != nullptr;
        }));
        const NetEvent* gone = first(serverEvents, NetEventKind::Disconnected);
        CHECK(gone != nullptr && gone->connection == toClient && gone->reason == "bye");
        CHECK_FALSE(server->send(toClient, bytes("after"), NetSendMode::Reliable));
        CHECK(first(clientEvents, NetEventKind::Disconnected) == nullptr);
    }

    {
        auto server = createEnetTransport();
        auto client = createEnetTransport();
        const int port = listenSomewhere(*server);
        const NetConnectionId toServer = client->connect("127.0.0.1:" + std::to_string(port));
        std::vector<NetEvent> serverEvents;
        std::vector<NetEvent> clientEvents;
        CHECK(pumpUntil(*server, *client, serverEvents, clientEvents, [&] {
            return first(serverEvents, NetEventKind::Incoming) != nullptr;
        }));
        if (const NetEvent* incoming = first(serverEvents, NetEventKind::Incoming)) {
            server->close(incoming->connection, "full");
        }
        CHECK(pumpUntil(*server, *client, serverEvents, clientEvents, [&] {
            return first(clientEvents, NetEventKind::Disconnected) != nullptr;
        }));
        const NetEvent* refused = first(clientEvents, NetEventKind::Disconnected);
        CHECK(refused != nullptr && refused->connection == toServer && refused->reason == "full");
        CHECK(first(clientEvents, NetEventKind::Connected) == nullptr);
    }

    {
        auto server = createEnetTransport();
        auto client = createEnetTransport();
        const int port = listenSomewhere(*server);
        const NetConnectionId toServer = client->connect("127.0.0.1:" + std::to_string(port));
        std::vector<NetEvent> serverEvents;
        std::vector<NetEvent> clientEvents;
        CHECK(pumpUntil(*server, *client, serverEvents, clientEvents, [&] {
            if (const NetEvent* incoming = first(serverEvents, NetEventKind::Incoming)) {
                server->accept(incoming->connection);
            }
            return first(clientEvents, NetEventKind::Connected) != nullptr;
        }));
        server.reset();
        std::vector<NetEvent> none;
        auto idle = createEnetTransport();
        CHECK(pumpUntil(*client, *idle, clientEvents, none, [&] {
            return first(clientEvents, NetEventKind::Disconnected) != nullptr;
        }));
        const NetEvent* lost = first(clientEvents, NetEventKind::Disconnected);
        CHECK(lost != nullptr && lost->connection == toServer);
    }

    {
        auto server = createEnetTransport();
        auto client = createEnetTransport();
        const int port = listenSomewhere(*server);
        const NetConnectionId toServer = client->connect("127.0.0.1:" + std::to_string(port));
        std::vector<NetEvent> serverEvents;
        std::vector<NetEvent> clientEvents;
        CHECK(pumpUntil(*server, *client, serverEvents, clientEvents, [&] {
            if (const NetEvent* incoming = first(serverEvents, NetEventKind::Incoming)) {
                server->accept(incoming->connection);
                serverEvents.clear();
            }
            return first(serverEvents, NetEventKind::Connected) && first(clientEvents, NetEventKind::Connected);
        }));
        serverEvents.clear();
        clientEvents.clear();
        CHECK(client->send(toServer, bytes("after a long load"), NetSendMode::Reliable));
        client->poll(clientEvents);
        enet_time_set(enet_time_get() + 6000);
        CHECK(pumpUntil(*server, *client, serverEvents, clientEvents, [&] {
            return first(serverEvents, NetEventKind::Message) != nullptr || first(clientEvents, NetEventKind::Disconnected);
        }));
        CHECK(first(clientEvents, NetEventKind::Disconnected) == nullptr);
        const NetEvent* late = first(serverEvents, NetEventKind::Message);
        CHECK(late != nullptr && text(late->data) == "after a long load");

        serverEvents.clear();
        clientEvents.clear();
        const NetConnectionId toClient = late != nullptr ? late->connection : kNoNetConnection;
        for (int i = 0; i < 100; ++i) {
            CHECK(server->send(toClient, bytes("warm"), NetSendMode::Reliable));
            pumpUntil(*server, *client, serverEvents, clientEvents, [&] { return !clientEvents.empty(); });
            clientEvents.clear();
        }
        CHECK(server->send(toClient, bytes("while you load"), NetSendMode::Reliable));
        for (int i = 0; i < 48; ++i) {
            server->poll(serverEvents);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            enet_time_set(enet_time_get() + 250);
        }
        CHECK(pumpUntil(*server, *client, serverEvents, clientEvents, [&] {
            return first(clientEvents, NetEventKind::Message) != nullptr || first(serverEvents, NetEventKind::Disconnected);
        }));
        CHECK(first(serverEvents, NetEventKind::Disconnected) == nullptr);
        const NetEvent* loaded = first(clientEvents, NetEventKind::Message);
        CHECK(loaded != nullptr && text(loaded->data) == "while you load");
    }

    {
        auto server = createEnetTransport();
        auto client = createEnetTransport();
        const int port = listenSomewhere(*server);
        const NetConnectionId toServer = client->connect("127.0.0.1:" + std::to_string(port));
        std::vector<NetEvent> serverEvents;
        std::vector<NetEvent> clientEvents;
        CHECK(pumpUntil(*server, *client, serverEvents, clientEvents, [&] {
            if (const NetEvent* incoming = first(serverEvents, NetEventKind::Incoming)) {
                server->accept(incoming->connection);
                serverEvents.clear();
            }
            return first(serverEvents, NetEventKind::Connected) && first(clientEvents, NetEventKind::Connected);
        }));
        const NetEvent* joined = first(serverEvents, NetEventKind::Connected);
        const NetConnectionId toClient = joined != nullptr ? joined->connection : kNoNetConnection;
        serverEvents.clear();
        clientEvents.clear();
        CHECK(server->send(toClient, bytes("new map"), NetSendMode::Reliable));
        for (int i = 0; i < 200; ++i) {
            server->poll(serverEvents);
            client->keepAlive();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            enet_time_set(enet_time_get() + 250);
        }
        CHECK(first(serverEvents, NetEventKind::Disconnected) == nullptr);
        client->poll(clientEvents);
        CHECK(first(clientEvents, NetEventKind::Disconnected) == nullptr);
        const NetEvent* held = first(clientEvents, NetEventKind::Message);
        CHECK(held != nullptr && held->connection == toServer && text(held->data) == "new map");
    }

    {
        auto server = createSecureTransport(createEnetTransport());
        auto client = createSecureTransport(createEnetTransport());
        const int port = listenSomewhere(*server);
        CHECK(port != 0);
        const NetConnectionId toServer = client->connect("127.0.0.1:" + std::to_string(port));
        std::vector<NetEvent> serverEvents;
        std::vector<NetEvent> clientEvents;
        CHECK(pumpUntil(*server, *client, serverEvents, clientEvents, [&] {
            if (const NetEvent* incoming = first(serverEvents, NetEventKind::Incoming)) {
                server->accept(incoming->connection);
                serverEvents.clear();
            }
            return first(serverEvents, NetEventKind::Connected) && first(clientEvents, NetEventKind::Connected);
        }));
        const std::optional<SecureKey> seen = client->peerKey(toServer);
        CHECK(seen.has_value() && *seen == server->publicKey());
        serverEvents.clear();
        CHECK(client->send(toServer, bytes("over the wire"), NetSendMode::Reliable));
        CHECK(pumpUntil(*server, *client, serverEvents, clientEvents, [&] {
            return first(serverEvents, NetEventKind::Message) != nullptr;
        }));
        const NetEvent* message = first(serverEvents, NetEventKind::Message);
        CHECK(message != nullptr && text(message->data) == "over the wire");
    }

    {
        auto server = createEnetTransport();
        const int port = listenSomewhere(*server);
        CHECK(port != 0);
        for (int round = 0; round < 3; ++round) {
            auto client = createEnetTransport();
            const NetConnectionId toServer = client->connect("127.0.0.1:" + std::to_string(port));
            std::vector<NetEvent> serverEvents;
            std::vector<NetEvent> clientEvents;
            CHECK(pumpUntil(*server, *client, serverEvents, clientEvents, [&] {
                if (const NetEvent* incoming = first(serverEvents, NetEventKind::Incoming)) {
                    server->accept(incoming->connection);
                    serverEvents.clear();
                }
                return first(clientEvents, NetEventKind::Connected) != nullptr;
            }));
            client->close(toServer, "round over");
            CHECK(pumpUntil(*server, *client, serverEvents, clientEvents, [&] {
                return first(serverEvents, NetEventKind::Disconnected) != nullptr;
            }));
        }
    }

    {
        auto client = createEnetTransport(EnetTransportOptions{1500, 4});
        auto idle = createEnetTransport();
        CHECK(client->connect("not an address") == kNoNetConnection);
        const NetConnectionId lost = client->connect("127.0.0.1:1");
        CHECK(lost != kNoNetConnection);
        std::vector<NetEvent> clientEvents;
        std::vector<NetEvent> none;
        CHECK(pumpUntil(*client, *idle, clientEvents, none, [&] {
            return first(clientEvents, NetEventKind::Disconnected) != nullptr;
        }));
        const NetEvent* failed = first(clientEvents, NetEventKind::Disconnected);
        CHECK(failed != nullptr && failed->connection == lost && failed->reason == "no response");
    }
}

}
