#pragma once

#include "assets/asset_store.hpp"
#include "map/csg_script.hpp"

#include <flecs.h>
#include <raylib.h>

#include <optional>
#include <string>
#include <string_view>

struct s7_scheme;

namespace slopengine {

enum class LoadingPhase {
    Stages,
    Crossfade,
};

struct LoadingSession {
    bool active = false;
    LoadingPhase phase = LoadingPhase::Stages;
    std::string mapName;
    std::string reason;
    std::string stageLabel;
    int stageIndex = 0;
    Texture2D snapshotTexture{};
    float crossfadeElapsedSeconds = 0.0f;
    float crossfadeDurationSeconds = 0.5f;
    MapLoadWork work;
    std::optional<LoadedMap> loadedMap;
};

void registerLoadingSessionModule(flecs::world& world);

bool shouldShowLoadingScreen(std::string_view reason);
bool hasActiveLoadingSession(const flecs::world& world);
bool isLoadingCrossfadeActive(const flecs::world& world);

/** Captures a snapshot of the current backbuffer, unloads the current map, and
 *  begins the staged load of mapName, one stage per advanceLoadingSession call. */
void beginLoadingSession(
    flecs::world& world,
    AssetStore& assets,
    s7_scheme* scheme,
    std::string_view mapName,
    std::string_view reason);

/** Runs exactly one stage of an active session's work, or ticks the crossfade. */
void advanceLoadingSession(flecs::world& world, AssetStore& assets, s7_scheme* scheme);

}
