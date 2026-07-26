#pragma once

#include "assets/asset_store.hpp"
#include "map/radiosity.hpp"

#include <raylib.h>

#include <string_view>
#include <vector>

struct s7_scheme;

namespace slopengine {

/** Lights + optional ambient collected from map things for bake. */
struct RadiosityThingLights {
    std::vector<RadiosityLight> lights;
    Vector3 ambient{0.0f, 0.0f, 0.0f};
    bool hasAmbient = false;
};

/** Collects point/spot/sun lights and ambient-light from things.s7 / prefab sidecars. */
RadiosityThingLights collectRadiosityLights(
    s7_scheme* scheme,
    AssetStore& assets,
    std::string_view mapName);

}
