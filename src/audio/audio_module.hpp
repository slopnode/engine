#pragma once

#include "audio/audio_world.hpp"
#include "assets/asset_store.hpp"

#include <flecs.h>

struct s7_scheme;

namespace slopengine {

struct AudioContext {
    AudioWorld* world = nullptr;
    AssetStore* assets = nullptr;
};

void registerAudioModule(
    flecs::world& world,
    AudioWorld* audio,
    AssetStore& assets,
    s7_scheme* scheme);
void unregisterAudioModule(flecs::world& world);

}
