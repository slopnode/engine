#pragma once

#include "assets/asset_store.hpp"
#include "map/brush.hpp"
#include "map/thing.hpp"

#include <flecs.h>
#include <raylib.h>

#include <string_view>
#include <vector>

struct s7_scheme;

namespace slopengine {

struct PlayerStart {
    Vector3 position{0.0f, 0.1f, 0.0f};
    float yaw = 3.14159265358979323846f;
    float pitch = -0.05f;
    bool found = false;
};

void spawnThings(
    flecs::world& world,
    AssetStore& assets,
    s7_scheme* scheme,
    const ThingDocument& doc,
    const std::vector<Brush>* brushes = nullptr);

PlayerStart spawnMapThings(
    s7_scheme* scheme,
    flecs::world& world,
    AssetStore& assets,
    std::string_view mapName,
    const std::vector<Brush>& brushes);

}
