#pragma once

#include <flecs.h>

struct s7_scheme;

namespace slopengine {

struct PlayerCommand;

class PredictedCommandGuard {
public:
    explicit PredictedCommandGuard(const PlayerCommand& command);
    ~PredictedCommandGuard();

    PredictedCommandGuard(const PredictedCommandGuard&) = delete;
    PredictedCommandGuard& operator=(const PredictedCommandGuard&) = delete;

private:
    const PlayerCommand* previous_;
};

void bindInputApi(flecs::world& world, s7_scheme* scheme);

}
