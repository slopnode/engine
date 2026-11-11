#pragma once

#include "fx/tracer_motion.hpp"

#include <flecs.h>
#include <raylib.h>

#include <vector>

namespace slopengine {

struct TracerPool {
    std::vector<Tracer> tracers;
};

void spawnTracer(flecs::world& world, Vector3 from, Vector3 to, const TracerStyle& style, float lead = 0.0f);

void clearTracers(flecs::world& world);

void registerTracerModule(flecs::world& world);

void drawTracers(flecs::world& world, const Camera3D& camera);

}
