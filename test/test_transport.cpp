#include "test_assert.hpp"

#include "net/memory_transport.hpp"

#include <cstring>
#include <set>
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

std::vector<NetEvent> drain(NetTransport& transport) {
    std::vector<NetEvent> events;
    transport.poll(events);
    return events;
}

struct Pair {
    std::unique_ptr<NetTransport> server;
    std::unique_ptr<NetTransport> client;
    NetConnectionId serverSide = kNoNetConnection;
    NetConnectionId clientSide = kNoNetConnection;
};

Pair connectPair(MemoryNetwork& network, double settle) {
    Pair pair;
    pair.server = network.endpoint();
    pair.client = network.endpoint();
    pair.server->listen("server");
    pair.clientSide = pair.client->connect("server");
    network.advance(settle);
    for (const NetEvent& event : drain(*pair.server)) {
        if (event.kind == NetEventKind::Incoming) {
            pair.serverSide = event.connection;
            pair.server->accept(event.connection);
        }
    }
    network.advance(settle);
    drain(*pair.server);
    drain(*pair.client);
    return pair;
}

} // namespace

void runTransportTests() {
    {
        MemoryNetwork network;
        auto server = network.endpoint();
        auto client = network.endpoint();
        CHECK(server->listen("server"));
        CHECK_FALSE(client->listen("server"));
        const NetConnectionId toServer = client->connect("server");
        CHECK(toServer != kNoNetConnection);
        CHECK_FALSE(client->send(toServer, bytes("early"), NetSendMode::Reliable));

        std::vector<NetEvent> events = drain(*server);
        CHECK_EQ(static_cast<int>(events.size()), 1);
        CHECK(events[0].kind == NetEventKind::Incoming);
        const NetConnectionId toClient = events[0].connection;
        CHECK_FALSE(server->send(toClient, bytes("early"), NetSendMode::Reliable));
        CHECK(server->accept(toClient));
        CHECK_FALSE(server->accept(toClient));
        CHECK_FALSE(client->accept(toServer));

        events = drain(*server);
        CHECK(events.size() == 1 && events[0].kind == NetEventKind::Connected);
        events = drain(*client);
        CHECK(events.size() == 1 && events[0].kind == NetEventKind::Connected && events[0].connection == toServer);

        CHECK(client->send(toServer, bytes("hello"), NetSendMode::Reliable));
        CHECK(server->send(toClient, bytes("welcome"), NetSendMode::Unreliable));
        events = drain(*server);
        CHECK(events.size() == 1 && events[0].kind == NetEventKind::Message && text(events[0].data) == "hello");
        events = drain(*client);
        CHECK(events.size() == 1 && text(events[0].data) == "welcome");

        CHECK_FALSE(client->send(toServer, std::vector<std::byte>(kMaxNetMessageBytes + 1), NetSendMode::Reliable));
        CHECK(client->stats(toServer).sentMessages == 1);
        CHECK(server->stats(toClient).receivedMessages == 1);
    }

    {
        MemoryNetwork network;
        network.setConditions({20.0f, 0.0f, 0.0f});
        auto client = network.endpoint();
        const NetConnectionId lost = client->connect("nowhere");
        network.advance(0.039);
        CHECK(drain(*client).empty());
        network.advance(0.002);
        const std::vector<NetEvent> events = drain(*client);
        CHECK(events.size() == 1 && events[0].kind == NetEventKind::Disconnected && events[0].connection == lost);
        CHECK(!events.empty() && events[0].reason.find("nowhere") != std::string::npos);
    }

    {
        MemoryNetwork network;
        network.setConditions({50.0f, 0.0f, 0.0f});
        Pair pair = connectPair(network, 0.06);
        CHECK(pair.serverSide != kNoNetConnection);
        CHECK(pair.client->send(pair.clientSide, bytes("late"), NetSendMode::Reliable));
        network.advance(0.049);
        CHECK(drain(*pair.server).empty());
        network.advance(0.002);
        CHECK_EQ(static_cast<int>(drain(*pair.server).size()), 1);
        CHECK(pair.client->stats(pair.clientSide).pingMs == 100.0f);
    }

    {
        MemoryNetwork network(7);
        network.setConditions({30.0f, 25.0f, 0.3f});
        Pair pair = connectPair(network, 1.0);
        for (int i = 0; i < 200; ++i) {
            pair.client->send(pair.clientSide, bytes(std::to_string(i)), NetSendMode::Reliable);
            network.advance(0.001);
        }
        network.advance(5.0);
        const std::vector<NetEvent> events = drain(*pair.server);
        CHECK_EQ(static_cast<int>(events.size()), 200);
        bool ordered = true;
        for (std::size_t i = 0; i < events.size(); ++i) {
            ordered = ordered && text(events[i].data) == std::to_string(i);
        }
        CHECK(ordered);
    }

    {
        MemoryNetwork network(11);
        network.setConditions({30.0f, 25.0f, 0.5f});
        Pair pair = connectPair(network, 1.0);
        for (int i = 0; i < 400; ++i) {
            pair.client->send(pair.clientSide, bytes(std::to_string(i)), NetSendMode::Unreliable);
            network.advance(0.001);
        }
        network.advance(5.0);
        const std::vector<NetEvent> events = drain(*pair.server);
        std::set<std::string> seen;
        bool reordered = false;
        int previous = -1;
        for (const NetEvent& event : events) {
            seen.insert(text(event.data));
            const int value = std::stoi(text(event.data));
            reordered = reordered || value < previous;
            previous = value;
        }
        CHECK(events.size() > 100 && events.size() < 300);
        CHECK(seen.size() == events.size());
        CHECK(reordered);
    }

    {
        MemoryNetwork network;
        network.setConditions({10.0f, 0.0f, 0.0f});
        Pair pair = connectPair(network, 0.02);
        pair.client->close(pair.clientSide, "bye");
        CHECK_FALSE(pair.client->send(pair.clientSide, bytes("after"), NetSendMode::Reliable));
        network.advance(0.02);
        std::vector<NetEvent> events = drain(*pair.server);
        CHECK(events.size() == 1 && events[0].kind == NetEventKind::Disconnected && events[0].reason == "bye");
        CHECK_FALSE(pair.server->send(pair.serverSide, bytes("after"), NetSendMode::Reliable));
        CHECK(drain(*pair.client).empty());
    }

    {
        MemoryNetwork network;
        auto server = network.endpoint();
        auto client = network.endpoint();
        server->listen("server");
        const NetConnectionId toServer = client->connect("server");
        const std::vector<NetEvent> incoming = drain(*server);
        CHECK(incoming.size() == 1);
        server->close(incoming[0].connection, "full");
        const std::vector<NetEvent> events = drain(*client);
        CHECK(events.size() == 1 && events[0].kind == NetEventKind::Disconnected && events[0].reason == "full");
        CHECK_FALSE(client->send(toServer, bytes("x"), NetSendMode::Reliable));
    }

    {
        MemoryNetwork network;
        Pair pair = connectPair(network, 0.0);
        pair.server.reset();
        const std::vector<NetEvent> events = drain(*pair.client);
        CHECK(events.size() == 1 && events[0].kind == NetEventKind::Disconnected);
        auto replacement = network.endpoint();
        CHECK(replacement->listen("server"));
    }
}

}
