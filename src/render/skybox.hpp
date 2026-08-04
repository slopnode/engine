#pragma once

#include "assets/asset_store.hpp"
#include "assets/material_loader.hpp"
#include "map/sky_types.hpp"
#include "map/thing.hpp"

namespace slopengine {

/** True when the material defines explicit sky appearance (gradient/cube/solid). */
bool materialHasSkyAppearance(const MaterialAsset& asset);

/** Builds runtime sky settings from a sky-tagged material asset. */
SkyboxSettings skyboxSettingsFromMaterial(const MaterialAsset& asset);

/** Builds runtime sky settings from a parsed map thing. */
SkyboxSettings skyboxSettingsFromThing(const Thing& placement, AssetStore* assets = nullptr);

}
