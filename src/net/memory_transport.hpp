#pragma once

#include "net/transport.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace slopengine {

struct MemoryLinkConditions {
    float latencyMs = 0.0f;
    float jitterMs = 0.0f;
    float lossFraction = 0.0f;
};

class MemoryTransport;

class MemoryNetwork {
public:
    explicit MemoryNetwork(std::uint32_t seed = 1);
    ~MemoryNetwork();

    MemoryNetwork(const MemoryNetwork&) = delete;
    MemoryNetwork& operator=(const MemoryNetwork&) = delete;

    std::unique_ptr<NetTransport> endpoint();
    void setConditions(const MemoryLinkConditions& conditions);
    const MemoryLinkConditions& conditions() const { return conditions_; }
    void advance(double seconds);
    double now() const { return now_; }

private:
    friend class MemoryTransport;

    struct Side {
        MemoryTransport* endpoint = nullptr;
        NetConnectionId local = kNoNetConnection;
    };

    struct Link {
        Side side[2];
        bool accepted = false;
        bool closed = false;
        double lastArrival[2]{};
    };

    struct InFlight {
        double arrival = 0.0;
        std::uint64_t order = 0;
        MemoryTransport* to = nullptr;
        NetEvent event;
    };

    void deliver(MemoryTransport* to, NetEvent event, double arrival);
    double oneWayDelay();
    bool lose();
    void detach(MemoryTransport* endpoint);

    MemoryLinkConditions conditions_{};
    std::mt19937 random_;
    double now_ = 0.0;
    std::uint64_t nextOrder_ = 0;
    std::map<std::string, MemoryTransport*> listeners_;
    std::vector<Link> links_;
    std::vector<InFlight> inFlight_;
};

}
