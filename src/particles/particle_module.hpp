#pragma once

#include "assets/asset_store.hpp"
#include "particles/components.hpp"
#include "particles/particle_sim.hpp"

#include <flecs.h>

#include <raylib.h>
#include <string_view>

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

flecs::entity spawnParticleSystemAimed(
    flecs::world& world,
    AssetStore& assets,
    const char* id,
    Vector3 position,
    Vector3 direction,
    std::string_view path,
    bool playing = true,
    bool mapOwned = true);

flecs::entity spawnParticleSystemFp(
    flecs::world& world,
    AssetStore& assets,
    const char* id,
    flecs::entity hostViewSprite,
    const std::string& attachName,
    std::string_view path,
    float depth = 0.35f,
    bool mapOwned = true);

flecs::entity spawnParticleSystemWorldAttach(
    flecs::world& world,
    AssetStore& assets,
    const char* id,
    flecs::entity hostSprite,
    const std::string& attachName,
    std::string_view path,
    bool mapOwned = true);

flecs::entity spawnParticleSystemFollow(
    flecs::world& world,
    AssetStore& assets,
    const char* id,
    flecs::entity host,
    float back,
    std::string_view path,
    bool mapOwned = true);

/** Spawns @p path on @p bone of the rigged @p host and keeps it there, aimed out of the limb,
 *  or out of the stump when @p fromParent is set. */
flecs::entity spawnParticleSystemBone(
    flecs::world& world,
    AssetStore& assets,
    const char* id,
    flecs::entity host,
    std::string_view bone,
    bool fromParent,
    std::string_view path,
    bool mapOwned = true);

void updateParticleSystems(
    flecs::world& world,
    AssetStore& assets,
    PhysicsWorld* physics,
    float dt);

void lightParticleSystems(flecs::world& world, const Frustum& frustum, bool unlit);

void drawParticleSystems(
    flecs::world& world,
    AssetStore& assets,
    const Camera3D& camera,
    const Frustum* frustum = nullptr,
    bool unlit = false);

void drawMuzzleParticleSystems(
    flecs::world& world,
    AssetStore& assets,
    const Camera3D& camera,
    bool unlit = false);

void registerParticleModule(flecs::world& world, AssetStore& assets);

}
