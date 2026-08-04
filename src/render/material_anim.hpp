#pragma once

#include "render/material_anim_types.hpp"

#include <flecs.h>

namespace slopengine {

struct AssetStore;
struct GeoAsset;

/** Attaches @p targets to @p entity when non-empty. */
void attachMaterialAnimTargets(flecs::entity entity, MaterialAnimTargets&& targets);

/** Collects bindings from @p geo and attaches them to @p entity when present. */
void attachMaterialAnimTargetsFromGeo(flecs::entity entity, const GeoAsset& geo, AssetStore& assets);

void registerMaterialAnimSystem(flecs::world& world);

}
