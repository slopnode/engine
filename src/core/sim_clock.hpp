#pragma once

#include <cstdint>

namespace slopengine {

inline constexpr float kSimTickDt = 1.0f / 60.0f;
inline constexpr int kMaxSimTicksPerFrame = 4;
inline constexpr float kMaxSimFrameDt = 0.25f;

struct SimSystem {};

struct SimRemote {};

struct SimShared {};

struct Headless {};

struct CosmeticSystem {};

struct CosmeticClock {
    float accumulator = 0.0f;
};

struct SimClock {
    std::uint64_t tick = 0;
    float accumulator = 0.0f;
};

int advanceSimClock(SimClock& clock, float frameDt);
void resetSimAccumulator(SimClock& clock);
float simBlendAlpha(const SimClock& clock);

}
