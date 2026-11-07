#include "core/sim_clock.hpp"

#include <cmath>

namespace slopengine {

int advanceSimClock(SimClock& clock, float frameDt) {
    if (!(frameDt > 0.0f)) {
        return 0;
    }
    if (frameDt > kMaxSimFrameDt) {
        frameDt = kMaxSimFrameDt;
    }
    clock.accumulator += frameDt;
    int ticks = 0;
    while (clock.accumulator >= kSimTickDt && ticks < kMaxSimTicksPerFrame) {
        clock.accumulator -= kSimTickDt;
        ++ticks;
    }
    if (clock.accumulator >= kSimTickDt) {
        clock.accumulator = std::fmod(clock.accumulator, kSimTickDt);
    }
    return ticks;
}

void resetSimAccumulator(SimClock& clock) {
    clock.accumulator = 0.0f;
}

float simBlendAlpha(const SimClock& clock) {
    const float alpha = clock.accumulator / kSimTickDt;
    if (!(alpha > 0.0f)) {
        return 0.0f;
    }
    return alpha < 1.0f ? alpha : 1.0f;
}

}
