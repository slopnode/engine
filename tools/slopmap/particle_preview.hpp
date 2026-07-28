#pragma once

#include "assets/asset_store.hpp"
#include "map/thing.hpp"
#include "particles/components.hpp"
#include "particles/particle_sim.hpp"

#include <raylib.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace slopmap {

struct ParticlePreviewState {
    bool enabled = true;
    std::unordered_map<std::string, slopengine::ParticleSystemInstance> byThingId;
};

void syncParticlePreview(
    ParticlePreviewState& state,
    slopengine::AssetStore& assets,
    const std::vector<slopengine::Thing>& things,
    bool restartAll = false);

void tickParticlePreview(
    ParticlePreviewState& state,
    slopengine::AssetStore& assets,
    const std::vector<slopengine::Thing>& things,
    float dt);

void drawParticlePreview(
    ParticlePreviewState& state,
    slopengine::AssetStore& assets,
    const std::vector<slopengine::Thing>& things,
    const Camera3D& camera);

}
