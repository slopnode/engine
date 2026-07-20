#pragma once

#include "assets/asset_store.hpp"
#include "map/placement.hpp"

#include <flecs.h>
#include <raylib.h>

#include <string_view>

struct s7_scheme;

namespace slopengine {

/** Player spawn pose taken from the first player-start placement. */
struct PlayerStart {
    Vector3 position{0.0f, 0.1f, 0.0f};
    float yaw = 3.14159265358979323846f;
    bool found = false;
};

/** Spawns flecs entities from @p doc (props, usables, lights). */
void spawnPlacements(
    flecs::world& world,
    AssetStore& assets,
    s7_scheme* scheme,
    const PlacementDocument& doc);

/** Loads map entities.s7, spawns placements, and returns the player start. */
PlayerStart loadMapEntities(
    s7_scheme* scheme,
    flecs::world& world,
    AssetStore& assets,
    std::string_view mapName);

}
