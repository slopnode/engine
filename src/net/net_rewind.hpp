#pragma once

#include <flecs.h>
#include <raylib.h>

#include <cstdint>
#include <deque>
#include <vector>

namespace slopengine {

inline constexpr std::uint64_t kDefaultMaxRewindTicks = 30;

struct NetView {
    double tick = 0.0;
    std::uint64_t command = 0;
};

struct NetRewindPose {
    flecs::entity_t entity = 0;
    Vector3 position{};
    Quaternion rotation{0.0f, 0.0f, 0.0f, 1.0f};
    bool character = false;
    Vector3 feet{};
};

struct NetRewindFrame {
    std::uint64_t tick = 0;
    std::vector<NetRewindPose> poses;
};

struct NetRewind {
    std::uint64_t maxTicks = kDefaultMaxRewindTicks;
    std::deque<NetRewindFrame> frames;
    std::vector<NetRewindPose> saved;
    int depth = 0;
    double rewoundTo = -1.0;
};

void recordNetRewind(flecs::world& world, std::uint64_t tick);

void clearNetRewind(flecs::world& world);

void beginNetRewind(flecs::world& world, flecs::entity shooter);

void endNetRewind(flecs::world& world);

}
