#pragma once

#include <flecs.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace slopengine {

struct SystemTime {
    std::string name;
    float msPerFrame = 0.0f;
};

struct SystemPerfState {
    bool enabled = false;
    std::unordered_map<flecs::entity_t, double> lastSpent;
    std::vector<SystemTime> top;
    double lastSampleTime = 0.0;
    int framesSinceSample = 0;
};

void enableSystemPerf(flecs::world& world, SystemPerfState& state);
void sampleSystemPerf(flecs::world& world, SystemPerfState& state, int maxEntries);

}
