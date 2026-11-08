#pragma once

#include "net/transport.hpp"

#include <cstddef>
#include <memory>

namespace slopengine {

struct EnetTransportOptions {
    int connectTimeoutMs = 5000;
    std::size_t maxPeers = 64;
    std::size_t maxPeersPerAddress = 4;
    int stallTimeoutMs = 20000;
};

std::unique_ptr<NetTransport> createEnetTransport(const EnetTransportOptions& options = {});

}
