#pragma once

#include "assets/asset_store.hpp"
#include "map/brush.hpp"
#include "map/lightmap.hpp"
#include "map/thing.hpp"

#include <flecs.h>
#include <raylib.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct s7_scheme;

namespace slopengine {

struct PlayerStart {
    Vector3 position{0.0f, 0.1f, 0.0f};
    float yaw = 3.14159265358979323846f;
    float pitch = -0.05f;
    bool found = false;

    bool haveMotor = false;
    float motorRadius = 0.3f;
    float motorHeight = 0.88f;
    float motorEyeHeight = 0.8f;
};

struct MapThingPlacements {
    std::vector<Thing> things;
    std::unordered_map<std::string, std::size_t> byId;
};

/** Attaches an AnimationPlayer to a skinned geo entity, resolving its clip bank from
 *  @p animBank or, when empty, from the skeleton the geo binds to. Plays @p clip when set. */
void attachGeoAnimationPlayer(
    flecs::entity entity,
    AssetStore& assets,
    std::string_view geoPath,
    std::string_view animBank,
    std::string_view clip,
    bool loop);

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
    const std::vector<Brush>& brushes,
    const RadFile* rad = nullptr,
    const std::vector<Texture2D>* lightmapAtlases = nullptr,
    bool hasLightmaps = false);

bool respawnMapThing(flecs::world& world, AssetStore& assets, s7_scheme* scheme, std::string_view id);

}
