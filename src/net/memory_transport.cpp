#include "net/memory_transport.hpp"

#include <algorithm>

namespace slopengine {

class MemoryTransport final : public NetTransport {
public:
    explicit MemoryTransport(MemoryNetwork& network) : network_(network) {}

    ~MemoryTransport() override {
        network_.detach(this);
    }

    bool listen(std::string_view address) override {
        const std::string key(address);
        if (key.empty() || network_.listeners_.count(key) != 0) {
            return false;
        }
        network_.listeners_[key] = this;
        listening_.push_back(key);
        return true;
    }

    NetConnectionId connect(std::string_view address) override {
        const NetConnectionId local = nextId_++;
        const auto listener = network_.listeners_.find(std::string(address));
        if (listener == network_.listeners_.end()) {
            NetEvent failed{NetEventKind::Disconnected, local, {}, "no listener at " + std::string(address)};
            network_.deliver(this, std::move(failed), network_.now_ + 2.0 * network_.oneWayDelay());
            return local;
        }
        MemoryTransport* server = listener->second;
        const NetConnectionId remote = server->nextId_++;
        MemoryNetwork::Link link{};
        link.side[0] = {this, local};
        link.side[1] = {server, remote};
        const std::size_t index = network_.links_.size();
        network_.links_.push_back(link);
        links_[local] = {index, 0};
        server->links_[remote] = {index, 1};
        network_.deliver(server, NetEvent{NetEventKind::Incoming, remote, {}, {}}, network_.now_ + network_.oneWayDelay());
        return local;
    }

    bool accept(NetConnectionId connection) override {
        MemoryNetwork::Link* link = findLink(connection);
        if (link == nullptr || link->accepted || link->closed || side(connection) != 1) {
            return false;
        }
        link->accepted = true;
        network_.deliver(this, NetEvent{NetEventKind::Connected, connection, {}, {}}, network_.now_);
        const MemoryNetwork::Side& client = link->side[0];
        sendOnLink(*link, 1, NetEvent{NetEventKind::Connected, client.local, {}, {}}, NetSendMode::Reliable);
        return true;
    }

    bool send(NetConnectionId connection, std::span<const std::byte> data, NetSendMode mode) override {
        MemoryNetwork::Link* link = findLink(connection);
        if (link == nullptr || !link->accepted || link->closed || data.size() > kMaxNetMessageBytes) {
            return false;
        }
        const int from = side(connection);
        const MemoryNetwork::Side& to = link->side[1 - from];
        NetEvent message{NetEventKind::Message, to.local, std::vector<std::byte>(data.begin(), data.end()), {}};
        ++stats_[connection].sentMessages;
        if (mode == NetSendMode::Unreliable && network_.lose()) {
            return true;
        }
        sendOnLink(*link, from, std::move(message), mode);
        return true;
    }

    void close(NetConnectionId connection, std::string_view reason) override {
        MemoryNetwork::Link* link = findLink(connection);
        if (link == nullptr || link->closed) {
            return;
        }
        link->closed = true;
        const int from = side(connection);
        const MemoryNetwork::Side& to = link->side[1 - from];
        if (to.endpoint != nullptr) {
            sendOnLink(*link, from, NetEvent{NetEventKind::Disconnected, to.local, {}, std::string(reason)}, NetSendMode::Reliable);
        }
    }

    void poll(std::vector<NetEvent>& out) override {
        std::vector<MemoryNetwork::InFlight> arrived;
        auto& queue = network_.inFlight_;
        for (auto it = queue.begin(); it != queue.end();) {
            if (it->to == this && it->arrival <= network_.now_) {
                arrived.push_back(std::move(*it));
                it = queue.erase(it);
            } else {
                ++it;
            }
        }
        std::sort(arrived.begin(), arrived.end(), [](const auto& a, const auto& b) {
            return a.arrival != b.arrival ? a.arrival < b.arrival : a.order < b.order;
        });
        for (MemoryNetwork::InFlight& item : arrived) {
            if (item.event.kind == NetEventKind::Message) {
                ++stats_[item.event.connection].receivedMessages;
            }
            out.push_back(std::move(item.event));
        }
    }

    NetConnectionStats stats(NetConnectionId connection) const override {
        NetConnectionStats out{};
        const auto found = stats_.find(connection);
        if (found != stats_.end()) {
            out = found->second;
        }
        out.pingMs = 2.0f * network_.conditions_.latencyMs;
        out.lossFraction = network_.conditions_.lossFraction;
        return out;
    }

    void forgetListeners() {
        for (const std::string& key : listening_) {
            network_.listeners_.erase(key);
        }
        listening_.clear();
    }

    void closeAll() {
        for (const auto& [local, ref] : links_) {
            MemoryNetwork::Link& link = network_.links_[ref.first];
            if (!link.closed) {
                close(local, "peer went away");
            }
            link.side[ref.second].endpoint = nullptr;
        }
    }

private:
    MemoryNetwork::Link* findLink(NetConnectionId connection) {
        const auto found = links_.find(connection);
        return found == links_.end() ? nullptr : &network_.links_[found->second.first];
    }

    int side(NetConnectionId connection) const {
        return links_.at(connection).second;
    }

    void sendOnLink(MemoryNetwork::Link& link, int from, NetEvent event, NetSendMode mode) {
        MemoryTransport* to = link.side[1 - from].endpoint;
        if (to == nullptr) {
            return;
        }
        double arrival = network_.now_ + network_.oneWayDelay();
        if (mode == NetSendMode::Reliable) {
            int resends = 0;
            while (resends < 8 && network_.lose()) {
                arrival += 2.0 * network_.oneWayDelay();
                ++resends;
            }
            arrival = std::max(arrival, link.lastArrival[1 - from]);
            link.lastArrival[1 - from] = arrival;
        }
        network_.deliver(to, std::move(event), arrival);
    }

    MemoryNetwork& network_;
    NetConnectionId nextId_ = 1;
    std::map<NetConnectionId, std::pair<std::size_t, int>> links_;
    std::map<NetConnectionId, NetConnectionStats> stats_;
    std::vector<std::string> listening_;
};

MemoryNetwork::MemoryNetwork(std::uint32_t seed) : random_(seed) {}

MemoryNetwork::~MemoryNetwork() = default;

std::unique_ptr<NetTransport> MemoryNetwork::endpoint() {
    return std::make_unique<MemoryTransport>(*this);
}

void MemoryNetwork::setConditions(const MemoryLinkConditions& conditions) {
    conditions_ = conditions;
}

void MemoryNetwork::advance(double seconds) {
    if (seconds > 0.0) {
        now_ += seconds;
    }
}

void MemoryNetwork::deliver(MemoryTransport* to, NetEvent event, double arrival) {
    inFlight_.push_back(InFlight{arrival, nextOrder_++, to, std::move(event)});
}

double MemoryNetwork::oneWayDelay() {
    double delayMs = conditions_.latencyMs;
    if (conditions_.jitterMs > 0.0f) {
        std::uniform_real_distribution<double> jitter(-conditions_.jitterMs, conditions_.jitterMs);
        delayMs += jitter(random_);
    }
    return std::max(0.0, delayMs) / 1000.0;
}

bool MemoryNetwork::lose() {
    if (conditions_.lossFraction <= 0.0f) {
        return false;
    }
    std::uniform_real_distribution<float> roll(0.0f, 1.0f);
    return roll(random_) < conditions_.lossFraction;
}

void MemoryNetwork::detach(MemoryTransport* endpoint) {
    endpoint->forgetListeners();
    endpoint->closeAll();
    inFlight_.erase(
        std::remove_if(inFlight_.begin(), inFlight_.end(), [endpoint](const InFlight& item) { return item.to == endpoint; }),
        inFlight_.end());
}

}
