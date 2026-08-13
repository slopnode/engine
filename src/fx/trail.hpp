#pragma once

#include "assets/asset_store.hpp"

#include <flecs.h>
#include <raylib.h>

#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

struct TrailEffect {
    std::vector<Vector3> points;
    std::string texturePath;
    float width = 0.15f;
    float lifetime = 0.25f;
    float age = 0.0f;
    Color color = WHITE;
};

flecs::entity spawnTrailFp(
    flecs::world& world,
    const char* id,
    flecs::entity hostViewSprite,
    const std::string& attachName,
    float depth,
    Vector3 endPoint,
    std::string_view texturePath,
    float lifetime = 0.12f,
    float width = 0.08f,
    bool mapOwned = true);

flecs::entity spawnTrail(
    flecs::world& world,
    const char* id,
    Vector3 startPoint,
    Vector3 endPoint,
    std::string_view texturePath,
    float lifetime = 0.12f,
    float width = 0.08,
    bool mapOwned = true);

void registerTrailModule(flecs::world& world);

void drawTrailEffects(flecs::world& world, AssetStore& assets, const Camera3D& camera, bool unlit = false);

}
