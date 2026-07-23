#include "map/map_scene.hpp"

#include "assets/asset_store.hpp"
#include "assets/skeleton_loader.hpp"
#include "audio/audio_module.hpp"
#include "audio/components.hpp"
#include "camera/components.hpp"
#include "game/game_state.hpp"
#include "map/bsp.hpp"
#include "map/csg_script.hpp"
#include "map/graph.hpp"
#include "map/graph_script.hpp"
#include "map/light_components.hpp"
#include "map/light_sample.hpp"
#include "map/things_spawn.hpp"
#include "map/fac.hpp"
#include "map/pvs.hpp"
#include "physics/components.hpp"
#include "physics/map_collision.hpp"
#include "physics/physics_module.hpp"
#include "physics/trigger_components.hpp"
#include "render/components.hpp"
#include "render/dynamic_light.hpp"
#include "render/dynamic_light_shadows.hpp"
#include "render/render_context.hpp"
#include "script/first_person_script.hpp"
#include "script/save_script.hpp"
#include "ui/ui_state.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <raylib.h>
#include <raymath.h>

namespace slopengine {

namespace {

void unloadMapGpuResources(flecs::entity entity) {
    if (!entity.has<Model3D>()) {
        return;
    }
    Model3D& model3d = entity.get_mut<Model3D>();
    if (entity.has<MapLightmapState>()) {
        MapLightmapState& lightmaps = entity.get_mut<MapLightmapState>();
        std::vector<unsigned int> atlasIds;
        for (int materialIndex = 0; materialIndex < model3d.model.materialCount; ++materialIndex) {
            Material& material = model3d.model.materials[materialIndex];
            if (material.maps == nullptr) {
                continue;
            }
            const unsigned int atlasId = material.maps[MATERIAL_MAP_METALNESS].texture.id;
            if (atlasId == 0) {
                continue;
            }
            if (std::find(atlasIds.begin(), atlasIds.end(), atlasId) == atlasIds.end()) {
                atlasIds.push_back(atlasId);
            }
            material.maps[MATERIAL_MAP_METALNESS].texture = {};
        }
        if (model3d.model.meshCount > 0) {
            UnloadModel(model3d.model);
        }
        model3d.model = {};
        for (unsigned int atlasId : atlasIds) {
            Texture2D atlas{};
            atlas.id = atlasId;
            UnloadTexture(atlas);
        }
        if (lightmaps.lightmapShader.id != 0) {
            UnloadShader(lightmaps.lightmapShader);
            lightmaps.lightmapShader = {};
        }
        lightmaps.available = false;
        lightmaps.useLightmapLoc = -1;
        return;
    }

    unloadClonedGeoModelInstance(model3d.model);
}

void destroyNamedEntityTree(flecs::world& world, const char* name) {
    flecs::entity entity = world.lookup(name);
    if (entity.is_valid()) {
        entity.destruct();
    }
}

} // namespace

void unloadMapScene(flecs::world& world) {
    if (world.has<AudioContext>()) {
        AudioContext& audioCtx = world.get_mut<AudioContext>();
        if (audioCtx.world != nullptr) {
            audioCtx.world->clearSteamAudioScene();
        }
    }

    if (world.has<PhysicsContext>()) {
        PhysicsWorld* physics = world.get_mut<PhysicsContext>().world;
        if (physics != nullptr) {
            physics->clearStaticBrushes();
            physics->clearKinematics();
            physics->destroyAllCharacters();
        }
    }

    std::vector<flecs::entity> owned;
    world.each([&](flecs::entity entity, MapOwned) {
        owned.push_back(entity);
    });
    for (flecs::entity entity : owned) {
        if (!entity.is_valid()) {
            continue;
        }
        unloadMapGpuResources(entity);
        entity.destruct();
    }

    destroyNamedEntityTree(world, "PlayerFp");
    destroyNamedEntityTree(world, "Player");
    destroyNamedEntityTree(world, "MapStatic");

    if (world.has<MapLighting>()) {
        world.remove<MapLighting>();
    }
    if (world.has<MapBsp>()) {
        world.remove<MapBsp>();
    }
    if (world.has<MapFac>()) {
        world.remove<MapFac>();
    }
    if (world.has<MapPvs>()) {
        world.remove<MapPvs>();
    }
    if (world.has<MapGraphs>()) {
        world.remove<MapGraphs>();
    }
    if (world.has<DynamicLightShadowState>()) {
        world.remove<DynamicLightShadowState>();
    }
    if (world.has<DynamicLightFrameState>()) {
        world.remove<DynamicLightFrameState>();
    }
    if (world.has<CurrentMap>()) {
        world.remove<CurrentMap>();
    }
    if (world.has<PlayerEntity>()) {
        world.get_mut<PlayerEntity>().entity = {};
    }

    if (world.has<DebugUiState>()) {
        DebugUiState& debugUi = world.get_mut<DebugUiState>();
        debugUi.inspectedEntity = {};
        debugUi.entityDetailOpen = false;
    }
}

bool registerMapScene(
    flecs::world& world,
    AssetStore& assets,
    s7_scheme* scheme,
    std::string_view mapName,
    std::string_view reason) {
    auto loaded = loadAndCompileMap(scheme, assets, mapName);
    if (!loaded) {
        TraceLog(LOG_WARNING, "MAP: failed to spawn map '%.*s'", static_cast<int>(mapName.size()), mapName.data());
        return false;
    }

    MapLightmapState lightmapState{};
    lightmapState.available = loaded->hasLightmaps;
    lightmapState.useLightmapLoc = loaded->useLightmapLoc;
    lightmapState.lightmapShader = loaded->lightmapShader;

    world.entity("MapStatic")
        .add<MapOwned>()
        .add<WorldSpace>()
        .set<LocalTransformation>({
            .position = {0.0f, 0.0f, 0.0f},
            .scale = {1.0f, 1.0f, 1.0f},
            .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
        })
        .set<Model3D>({loaded->model, WHITE})
        .set<MapLightmapState>(lightmapState);

    if (loaded->hasLightmaps) {
        world.set<DynamicLightShadowState>(createDynamicLightShadowState(assets));
    }
    {
        DynamicLightFrameState frameState{};
        if (loaded->hasLightmaps && loaded->lightmapShader.id != 0) {
            resolveDynamicLightShaderBindings(loaded->lightmapShader, frameState.bindings);
        }
        world.set<DynamicLightFrameState>(std::move(frameState));
    }

    MapBsp mapBsp{std::move(loaded->bsp)};
    MapFac mapFac{std::move(loaded->fac)};
    Color ambientColor{
        static_cast<unsigned char>(std::clamp(loaded->meta.ambient.x * 255.0f, 0.0f, 255.0f)),
        static_cast<unsigned char>(std::clamp(loaded->meta.ambient.y * 255.0f, 0.0f, 255.0f)),
        static_cast<unsigned char>(std::clamp(loaded->meta.ambient.z * 255.0f, 0.0f, 255.0f)),
        255,
    };
    world.set<MapLighting>(buildMapLighting(
        mapBsp.tree,
        std::move(loaded->rad),
        std::move(loaded->lightmapAtlasImages),
        ambientColor));

    if (world.has<AudioContext>()) {
        AudioContext& audioCtx = world.get_mut<AudioContext>();
        if (audioCtx.world != nullptr) {
            audioCtx.world->clearSteamAudioScene();
            audioCtx.world->setSteamAudioScene(mapFac.fac);
        }
    }

    world.set<MapBsp>(std::move(mapBsp));
    world.set<MapFac>(std::move(mapFac));
    world.set<MapPvs>(MapPvs{std::move(loaded->pvs)});

    if (world.has<PhysicsContext>()) {
        PhysicsWorld* physics = world.get_mut<PhysicsContext>().world;
        if (physics != nullptr) {
            addStaticBrushes(*physics, loaded->brushes);
        }
    }

    const PlayerStart playerStart = spawnMapThings(scheme, world, assets, mapName);

    {
        MapGraphs mapGraphs{};
        if (auto graphs = loadMapGraphs(scheme, assets, mapName)) {
            mapGraphs.document = std::move(*graphs);
        } else {
            TraceLog(
                LOG_WARNING,
                "MAP: graphs.s7 failed for '%.*s'",
                static_cast<int>(mapName.size()),
                mapName.data());
        }
        world.set<MapGraphs>(std::move(mapGraphs));
    }

    CharacterMotor motor{};
    FirstPersonController controller{};
    controller.yaw = playerStart.yaw;
    controller.pitch = -0.05f;
    controller.eyeHeight = motor.eyeHeight;
    controller.moveSpeed = motor.moveSpeed;

    const float cosPitch = std::cos(controller.pitch);
    const Vector3 forward = {
        std::sin(controller.yaw) * cosPitch,
        std::sin(controller.pitch),
        std::cos(controller.yaw) * cosPitch,
    };

    Lens lens{};
    lens.camera.position = {
        playerStart.position.x,
        playerStart.position.y + motor.eyeHeight,
        playerStart.position.z,
    };
    lens.camera.target = Vector3Add(lens.camera.position, forward);
    lens.camera.up = {0.0f, 1.0f, 0.0f};
    lens.camera.fovy = 75.0f;
    lens.camera.projection = CAMERA_PERSPECTIVE;

    flecs::entity player = world.entity("Player")
        .add<MapOwned>()
        .add<PlayerCamera>()
        .add<WorldSpace>()
        .set<Lens>(lens)
        .set<FirstPersonController>(controller)
        .set<CharacterMotor>(motor)
        .set<ViewEyeOffset>(ViewEyeOffset{})
        .set<AudioListener>(AudioListener{})
        .set<CollisionTags>(CollisionTags{{"player"}});
    world.set<PlayerEntity>(PlayerEntity{player});

    if (world.has<PhysicsContext>()) {
        PhysicsWorld* physics = world.get_mut<PhysicsContext>().world;
        if (physics != nullptr) {
            physics->setPlayerId(static_cast<std::uint64_t>(player.id()));
            physics->createPlayerCharacter(
                playerStart.position.x,
                playerStart.position.y,
                playerStart.position.z,
                motor);
        }
    }

    ensureFirstPersonScene(world, player);
    flecs::entity fpRoot = world.lookup("PlayerFp");
    if (fpRoot.is_valid()) {
        fpRoot.add<MapOwned>();
    }
    updateFirstPersonSceneTransforms(world);
    callPrepareFirstPerson(world);
    callOnMapReady(world, mapName, reason);

    world.set<CurrentMap>(CurrentMap{std::string(mapName)});
    return true;
}

void changeMap(
    flecs::world& world,
    AssetStore& assets,
    s7_scheme* scheme,
    std::string_view mapName,
    std::string_view reason) {
    unloadMapScene(world);
    if (registerMapScene(world, assets, scheme, mapName, reason)) {
        enterPlaying(world);
    } else {
        enterMenu(world);
    }
}

}
