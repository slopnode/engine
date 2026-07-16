#pragma once

#include "physics/physics_world.hpp"

#include <flecs.h>

namespace slopengine {

struct PhysicsContext {
    PhysicsWorld* world = nullptr;
};

void registerPhysicsModule(flecs::world& world, PhysicsWorld* physics);
void unregisterPhysicsModule(flecs::world& world);

}
