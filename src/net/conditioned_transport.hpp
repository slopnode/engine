#pragma once

#include "net/transport.hpp"

#include <cstdint>
#include <memory>

namespace slopengine {

struct NetConditions {
    float lagMs = 0.0f;
    float jitterMs = 0.0f;
    float lossFraction = 0.0f;

    bool active() const { return lagMs > 0.0f || jitterMs > 0.0f || lossFraction > 0.0f; }
};

std::unique_ptr<NetTransport> createConditionedTransport(
    std::unique_ptr<NetTransport> inner,
    const NetConditions& conditions,
    std::uint32_t seed);

}
