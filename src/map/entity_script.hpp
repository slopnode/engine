#pragma once

#include "assets/asset_store.hpp"
#include "map/placement.hpp"

#include <flecs.h>
#include <raylib.h>

#include <string_view>

struct s7_scheme;

namespace slopengine {

struct PlayerStart {
    Vector3 position{0.0f, 0.1f, 0.0f};
    float yaw = 3.14159265358979323846f;
    bool found = false;
};

void spawnPlacements(
    flecs::world& world,
    AssetStore& assets,
    s7_scheme* scheme,
    const PlacementDocument& doc);

PlayerStart loadMapEntities(
    s7_scheme* scheme,
    flecs::world& world,
    AssetStore& assets,
    std::string_view mapName);

}
