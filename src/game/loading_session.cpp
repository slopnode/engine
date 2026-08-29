#include "game/loading_session.hpp"

#include "game/game_state.hpp"
#include "game/menu_background.hpp"
#include "map/map_scene.hpp"

#include <raylib.h>

namespace slopengine {

namespace {

constexpr const char* kStageLabels[5] = {
    "Loading BSP",
    "Loading VIS",
    "Loading RAD",
    "Loading Textures",
    "Assembling scene",
};

void finishLoadingSessionFailure(flecs::world& world, const std::string& mapName) {
    TraceLog(LOG_WARNING, "MAP: staged load failed for '%s'", mapName.c_str());
    world.remove<LoadingSession>();
    enterMenu(world);
}

void finishLoadingSessionSuccess(flecs::world& world) {
    world.remove<LoadingSession>();
    clearMenuBackgroundImage(world);
    enterPlaying(world);
}

}

void registerLoadingSessionModule(flecs::world& world) {
    world.component<LoadingSession>()
        .on_remove([](flecs::iter&, size_t, LoadingSession& session) {
            if (session.snapshotTexture.id != 0) {
                UnloadTexture(session.snapshotTexture);
            }
            session.snapshotTexture = {};
        });
}

bool shouldShowLoadingScreen(std::string_view reason) {
    return reason == "fresh" || reason == "load" || reason == "carry" || reason == "direct";
}

bool hasActiveLoadingSession(const flecs::world& world) {
    return world.has<LoadingSession>() && world.get<LoadingSession>().active;
}

bool isLoadingCrossfadeActive(const flecs::world& world) {
    return hasActiveLoadingSession(world) && world.get<LoadingSession>().phase == LoadingPhase::Crossfade;
}

void beginLoadingSession(
    flecs::world& world,
    AssetStore& assets,
    s7_scheme* scheme,
    std::string_view mapName,
    std::string_view reason) {
    Image shot = LoadImageFromScreen();
    Texture2D snapshot{};
    if (shot.data != nullptr) {
        snapshot = LoadTextureFromImage(shot);
        UnloadImage(shot);
    }

    unloadMapScene(world);
    markTitleMapActive(world, false);

    LoadingSession session{};
    session.active = true;
    session.phase = LoadingPhase::Stages;
    session.mapName = std::string(mapName);
    session.reason = std::string(reason);
    session.stageLabel = kStageLabels[0];
    session.stageIndex = 0;
    session.snapshotTexture = snapshot;
    world.set<LoadingSession>(std::move(session));
}

void advanceLoadingSession(flecs::world& world, AssetStore& assets, s7_scheme* scheme) {
    if (!hasActiveLoadingSession(world)) {
        return;
    }
    LoadingSession& session = world.get_mut<LoadingSession>();

    if (session.phase == LoadingPhase::Stages) {
        bool ok = true;
        switch (session.stageIndex) {
        case 0:
            ok = loadMapStageBsp(scheme, assets, session.mapName, session.work);
            break;
        case 1:
            ok = loadMapStageVis(assets, session.work);
            break;
        case 2:
            ok = loadMapStageRad(assets, session.work);
            break;
        case 3: {
            auto loaded = loadMapStageTextures(assets, std::move(session.work));
            ok = loaded.has_value();
            if (ok) {
                session.loadedMap = std::move(*loaded);
            }
            break;
        }
        case 4:
            ok = session.loadedMap.has_value() &&
                assembleMapScene(
                    world, assets, scheme, session.mapName, session.reason, std::move(*session.loadedMap));
            break;
        default:
            ok = false;
            break;
        }

        if (!ok) {
            finishLoadingSessionFailure(world, session.mapName);
            return;
        }

        ++session.stageIndex;
        if (session.stageIndex < 5) {
            session.stageLabel = kStageLabels[session.stageIndex];
        } else {
            session.phase = LoadingPhase::Crossfade;
            session.crossfadeElapsedSeconds = 0.0f;
        }
        return;
    }

    session.crossfadeElapsedSeconds += GetFrameTime();
    if (session.crossfadeElapsedSeconds >= session.crossfadeDurationSeconds) {
        finishLoadingSessionSuccess(world);
    }
}

}
