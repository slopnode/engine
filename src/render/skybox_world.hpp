#pragma once

#include "assets/asset_store.hpp"
#include "render/skybox.hpp"
#include "render/skybox_render.hpp"

#include <raylib.h>

namespace flecs {
struct world;
}

namespace slopengine {

/** Returns the first skybox thing spawned in @p world, if any. */
const SkyboxSettings* findActiveSkybox(const flecs::world& world);

/** Draws map sky-material faces using the same sky lookup. */
void drawSkyMaterialFaces(
    flecs::world& world,
    Camera3D camera,
    AssetStore& assets,
    SkyboxShaderState& shaderState,
    const SkyboxSettings& settings);

}
