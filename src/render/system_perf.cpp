#include "render/system_perf.hpp"

#include <algorithm>

namespace slopengine {

void enableSystemPerf(flecs::world& world, SystemPerfState& state) {
    ecs_measure_system_time(world.c_ptr(), true);
    state.enabled = true;
}

void sampleSystemPerf(flecs::world& world, SystemPerfState& state, int maxEntries) {
    if (!state.enabled || state.framesSinceSample <= 0) {
        return;
    }
    const double frames = static_cast<double>(state.framesSinceSample);
    std::vector<SystemTime> times;
    flecs::query<> systems = world.query_builder().with(flecs::System).build();
    systems.each([&](flecs::entity entity) {
        const ecs_system_t* system = ecs_system_get(world.c_ptr(), entity.id());
        if (system == nullptr) {
            return;
        }
        const double spent = static_cast<double>(system->time_spent);
        double& last = state.lastSpent[entity.id()];
        const double delta = spent - last;
        last = spent;
        if (delta <= 0.0) {
            return;
        }
        const flecs::string name = entity.name();
        times.push_back(SystemTime{
            name.c_str() != nullptr && name.c_str()[0] != '\0' ? name.c_str() : "(unnamed)",
            static_cast<float>(delta * 1000.0 / frames)});
    });
    std::sort(times.begin(), times.end(), [](const SystemTime& a, const SystemTime& b) {
        return a.msPerFrame > b.msPerFrame;
    });
    if (static_cast<int>(times.size()) > maxEntries) {
        times.resize(static_cast<std::size_t>(maxEntries));
    }
    state.top = std::move(times);
    state.framesSinceSample = 0;
}

}
