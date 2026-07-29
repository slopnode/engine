#include "render/render_module.hpp"

#include "assets/asset_services.hpp"
#include "camera/components.hpp"
#include "core/frame_perf.hpp"
#include "core/screenshot.hpp"
#include "game/game_state.hpp"
#include "game/menu_background.hpp"
#include "game/user_settings.hpp"
#include "map/bsp.hpp"
#include "map/light_components.hpp"
#include "render/animation_player.hpp"
#include "render/animation_systems.hpp"
#include "render/components.hpp"
#include "render/dynamic_light.hpp"
#include "render/dynamic_light_shadows.hpp"
#include "render/fx_local_light.hpp"
#include "render/hud.hpp"
#include "render/render_context.hpp"
#include "render/post_process.hpp"
#include "render/render_pass_fp.hpp"
#include "render/render_pass_world.hpp"
#include "render/render_frustum.hpp"
#include "render/sprite_animator.hpp"
#include "particles/particle_module.hpp"
#include "script/first_person_script.hpp"
#include "script/script_context.hpp"
#include "ui/ui_module.hpp"
#include "ui/ui_state.hpp"
#include "rlImGui.h"

#include <filesystem>
#include <string>
#include <vector>

#include <raylib.h>
#include <raymath.h>

namespace slopengine {

namespace {

void registerComponents(flecs::world& world) {
    world.component<LocalTransformation>()
        .add(flecs::With, world.component<GlobalTransformation>());

    world.component<GlobalTransformation>();
    world.component<WorldSpace>();
    world.component<ViewSpace>();
    world.component<ViewCanvas>();
    world.component<HudCanvas>();
    world.component<TitleCanvas>();
    world.component<ViewSprite>();
    world.component<HudDrawList>();
    world.component<HudFontCache>();
    world.component<Lens>();
    world.component<Spin>();
    world.component<Model3D>();
    world.component<SpriteInstance>();
    world.component<SpriteOverlay>();
    world.component<SpriteAnimator>();
    world.component<AnimationPlayer>();
    world.component<AnimationClipFlipTest>();
    world.component<PointLight>();
    world.component<SpotLight>();
    world.component<AreaLight>();
    world.component<SunLight>();
    world.component<AmbientLight>();
    world.component<DynamicLight>();
    world.component<FxLocalLight>();
    world.component<MapOwned>();
    world.component<CurrentMap>();
    world.component<PlayerEntity>();
    world.component<ShaderCavity>()
        .on_remove([](flecs::iter&, size_t, ShaderCavity& shader) {
            if (shader.shader.id != 0) {
                UnloadShader(shader.shader);
            }
            shader.shader = {};
            shader.modelLoc = -1;
            shader.viewLoc = -1;
            shader.projectionLoc = -1;
            shader.resolved = false;
        });
}

void registerRenderSystems(flecs::world& world) {
    world.system("StartDrawing")
        .kind(flecs::PostUpdate)
        .run([](flecs::iter& it) {
            if (it.world().has<FramePerfStats>()) {
                it.world().get_mut<FramePerfStats>().renderMs = 0.0f;
            }
            BeginDrawing();
            ClearBackground(BLACK);
        });

    world.system("UpdateFirstPersonScene")
        .kind(flecs::PostUpdate)
        .run([](flecs::iter& it) {
            if (!isPlaying(it.world())) {
                return;
            }
            updateFirstPersonSceneTransforms(it.world());
        });

    world.system<const Lens>("LensWorld")
        .with<WorldSpace>()
        .kind(flecs::PostUpdate)
        .each([](flecs::iter& it, size_t index, const Lens& lens) {
            flecs::world world = it.world();
            if (!shouldDrawWorld(world)) {
                return;
            }
            flecs::entity eyeEntity = it.entity(index);
            RenderContext& context = world.get_mut<RenderContext>();
            const bool unlit =
                world.has<DebugUiState>() && world.get<DebugUiState>().unlit;

            FirstPersonController controller{};
            ViewEyeOffset eyeOffset{};
            if (eyeEntity.has<FirstPersonController>()) {
                controller = eyeEntity.get<FirstPersonController>();
            }
            if (eyeEntity.has<ViewEyeOffset>()) {
                eyeOffset = eyeEntity.get<ViewEyeOffset>();
            }
            const Camera3D presentCam = presentationCamera(lens, controller, eyeOffset);
            Lens presentLens = lens;
            presentLens.camera = presentCam;

            const float aspect = static_cast<float>(GetRenderWidth()) /
                static_cast<float>(GetRenderHeight() > 0 ? GetRenderHeight() : 1);
            const Frustum frustum = makeFrustumFromCamera(presentCam, aspect);

            const double renderStart = perfNow();

            const GraphicsSettings graphics =
                world.has<UserSettings>() ? world.get<UserSettings>().graphics : GraphicsSettings{};
            const bool enableDynamicLights = graphics.dynamicLights;
            const int maxShadowed =
                graphics.dynamicLightShadows ? kMaxShadowedDynamicLights : 0;

            std::vector<RankedDynamicLight> rankedLights = gatherDynamicLights(
                world,
                lens,
                presentLens,
                frustum,
                unlit,
                enableDynamicLights,
                maxShadowed);
            storeDynamicLightFrameState(world, rankedLights);

            const DynamicLightShadowState* shadowState = nullptr;
            if (maxShadowed > 0 && world.has<DynamicLightShadowState>() &&
                world.has<AssetServices>() && world.get<AssetServices>().store != nullptr) {
                bool needsShadows = false;
                for (const RankedDynamicLight& light : rankedLights) {
                    if (light.shadowSlot >= 0) {
                        needsShadows = true;
                        break;
                    }
                }
                if (needsShadows) {
                    DynamicLightShadowState& shadows = world.get_mut<DynamicLightShadowState>();
                    renderDynamicLightShadows(
                        shadows,
                        rankedLights,
                        world,
                        *world.get<AssetServices>().store);
                    shadowState = &shadows;
                }
            }

            uploadMapDynamicLights(world, rankedLights, unlit, shadowState);

            FxLightFrameState fxLights{};
            buildFxLightFrameState(world, &frustum, unlit, fxLights);
            storeFxLightFrameState(world, std::move(fxLights));

            const bool playing = isPlaying(world);
            PostProcessState* postState = nullptr;
            bool sceneToTexture = false;
            if (playing) {
                postState = &ensurePostProcessState(world);
                sceneToTexture = ensurePostProcessScene(
                    *postState,
                    GetRenderWidth(),
                    GetRenderHeight());
                if (sceneToTexture) {
                    BeginTextureMode(postState->scene);
                    ClearBackground(BLACK);
                }
            }

            BeginMode3D(presentCam);
            drawWorldModels(world, context, lens, frustum, unlit);
            const std::string spriteAimStatus =
                drawWorldSprites(world, context, lens, frustum, unlit);
            if (world.has<AssetServices>() && world.get<AssetServices>().store != nullptr) {
                drawParticleSystems(
                    world, *world.get_mut<AssetServices>().store, presentCam, unlit);
            }
            drawWorldDebugOverlays(world);
            EndMode3D();

            if (playing) {
                if (world.has<AssetServices>() && world.get<AssetServices>().store != nullptr) {
                    BeginMode3D(presentCam);
                    drawMuzzleParticleSystems(
                        world, *world.get_mut<AssetServices>().store, presentCam, unlit);
                    EndMode3D();
                }
                drawFirstPersonPass(world, context, lens, unlit);
                drawViewSprites(world);
                if (sceneToTexture) {
                    EndTextureMode();
                    presentPostProcess(*postState);
                }
                drawHud(world);
                drawSpriteAimHudText(spriteAimStatus);
            }

            if (world.has<FramePerfStats>()) {
                world.get_mut<FramePerfStats>().renderMs += perfElapsedMs(renderStart);
            }
        });

    world.system("MenuTitleOverlay")
        .kind(flecs::PostUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            if (!isMenu(world)) {
                return;
            }
            drawMenuBackgroundImage(world);
            drawMenuTitleCanvas(world);
        });

    world.system("ImGuiOverlay")
        .kind(flecs::PostUpdate)
        .run([](flecs::iter& it) {
            FramePerfStats* perf =
                it.world().has<FramePerfStats>() ? &it.world().get_mut<FramePerfStats>() : nullptr;
            if (perf != nullptr) {
                perf->frameMs = GetFrameTime() * 1000.0f;
            }

            prepareUiInput(it.world());
            rlImGuiBegin();
            const double uiStart = perfNow();
            drawUi(it.world());
            if (perf != nullptr) {
                perf->uiMs = perfElapsedMs(uiStart);
                perf->pushSample();
            }
            rlImGuiEnd();
        });

    world.system("EndDrawing")
        .kind(flecs::PostUpdate)
        .run([](flecs::iter& it) {
            EndDrawing();
            ScreenshotRequest& request = it.world().get_mut<ScreenshotRequest>();
            if (!request.pending) {
                return;
            }
            request.pending = false;
            std::filesystem::path path;
            std::string error;
            ConsoleState& console = it.world().get_mut<ConsoleState>();
            if (saveScreenshotPng(path, error)) {
                const std::string message = "Screenshot saved: " + path.string();
                TraceLog(LOG_INFO, "SCREENSHOT: %s", path.string().c_str());
                console.log.push_back(message);
                if (console.log.size() > 200) {
                    console.log.erase(console.log.begin());
                }
            } else {
                TraceLog(LOG_WARNING, "SCREENSHOT: %s", error.c_str());
                console.log.push_back("Screenshot failed: " + error);
                if (console.log.size() > 200) {
                    console.log.erase(console.log.begin());
                }
            }
        });
}

} // namespace

void registerRenderModule(
    flecs::world& world,
    AssetStore& assets,
    const AppConfig& config,
    s7_scheme* scheme) {
    registerComponents(world);
    world.component<GameState>();

    world.set<AssetServices>(AssetServices{&assets});
    world.set<ScriptContext>(ScriptContext{scheme});
    world.set<FirstPersonViewShader>(createFirstPersonViewShader(assets));
    world.set<RenderContext>({
        world.query_builder<Model3D, GlobalTransformation>()
            .with<WorldSpace>()
            .build(),
        world.query_builder<Model3D, GlobalTransformation>()
            .with<ViewSpace>()
            .without<WorldSpace>()
            .build(),
        world.query_builder<SpriteInstance, GlobalTransformation>()
            .with<WorldSpace>()
            .without<ViewSprite>()
            .build(),
        world.query_builder<Model3D, GlobalTransformation, AnimationPlayer>()
            .with<WorldSpace>()
            .build(),
    });
    world.set<PlayerEntity>({});
    world.set<PostProcessState>({});

    registerSpinSystem(world);
    registerSchemeTickSystem(world);
    registerAnimationSystems(world);
    registerAnimationClipFlipTestSystem(world);
    registerTransformSystems(world);
    registerSpriteAnimatorSystem(world);
    registerRenderSystems(world);

    (void)config;
}

}
