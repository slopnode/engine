#pragma once

#include "assets/asset_store.hpp"
#include "particles/components.hpp"
#include "particles/particle_sim.hpp"

#include <flecs.h>

#include <raylib.h>

struct s7_scheme;

namespace slopengine {

struct PhysicsWorld;

flecs::entity spawnParticleSystem(
    flecs::world& world,
    AssetStore& assets,
    const char* id,
    Vector3 position,
    float yaw,
    std::string_view path,
    bool playing = true,
    bool mapOwned = true);

void updateParticleSystems(
    flecs::world& world,
    AssetStore& assets,
    PhysicsWorld* physics,
    float dt);

void drawParticleSystems(
    flecs::world& world,
    AssetStore& assets,
    const Camera3D& camera);

void registerParticleModule(flecs::world& world, AssetStore& assets);

}
