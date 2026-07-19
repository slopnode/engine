#pragma once

#include "assets/asset_store.hpp"

#include <flecs.h>
#include <raylib.h>

#include <string_view>

struct s7_scheme;

namespace slopengine {

/** Player spawn pose resolved from a map entities script. */
struct PlayerStart {
    Vector3 position{0.0f, 0.1f, 0.0f};
    float yaw = 3.14159265358979323846f;
    bool found = false;
};

/** Loads `maps/<mapName>/entities.s7`, spawns props/usables into @p world, and returns the player start. */
PlayerStart loadMapEntities(
    s7_scheme* scheme,
    flecs::world& world,
    AssetStore& assets,
    std::string_view mapName);

}
