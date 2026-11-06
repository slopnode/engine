#include "render/render_module.hpp"

#include "assets/asset_services.hpp"
#include "camera/camera_path_player.hpp"
#include "camera/components.hpp"
#include "core/frame_perf.hpp"
#include "core/screenshot.hpp"
#include "game/game_state.hpp"
#include "game/loading_session.hpp"
#include "game/menu_background.hpp"
#include "game/user_settings.hpp"
#include "map/bsp.hpp"
#include "map/light_components.hpp"
#include "render/animation_player.hpp"
#include "render/bone_attachment.hpp"
#include "render/model_hitboxes.hpp"
#include "render/animation_systems.hpp"
#include "render/components.hpp"
#include "render/material_anim_types.hpp"
#include "render/dynamic_light.hpp"
#include "render/dynamic_light_shadows.hpp"
#include "render/fx_local_light.hpp"
#include "render/gpu_timer.hpp"
#include "render/system_perf.hpp"
#include "render/model_lighting.hpp"
#include "render/hud.hpp"
#include "render/loading_overlay.hpp"
#include "render/render_context.hpp"
#include "render/material_anim.hpp"
#include "render/post_process.hpp"
#include "render/render_pass_extra_eye.hpp"
#include "render/render_pass_fp.hpp"
#include "render/render_pass_world.hpp"
#include "render/render_frustum.hpp"
#include "render/skybox_world.hpp"
#include "render/sprite_animator.hpp"
#include "particles/particle_module.hpp"
#include "fx/trail.hpp"
#include "fx/tracer.hpp"
#include "script/first_person_script.hpp"
#include "script/script_context.hpp"
#include "ui/ui_module.hpp"
#include "ui/ui_state.hpp"
#include "rlImGui.h"

#include <cmath>
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
    world.component<ViewGeoRoot>();
    world.component<ViewCanvas>();
    world.component<HudCanvas>();
    world.component<TitleCanvas>();
    world.component<ViewSprite>();
    world.component<HudDrawList>();
    world.component<HudFontCache>();
    world.component<Lens>();
    world.component<ExtraEye>()
        .on_remove([](flecs::iter&, size_t, ExtraEye& eye) {
            if (eye.target.id != 0) {
                UnloadRenderTexture(eye.target);
            }
            eye.target = {};
        });
    world.component<FpViewOverride>();
    world.component<Spin>();
    world.component<Model3D>();
    world.component<BoneScales>();
    world.component<ModelHitboxes>();
    world.component<HiddenBones>();
    world.component<GeoSource>();
    world.component<SpriteInstance>();
    world.component<SpriteOverlay>();
    world.component<SpriteAnimator>();
    world.component<AnimationPlayer>();
    world.component<BoneAttachment>();
    world.component<AnimationClipFlipTest>();
    world.component<MaterialAnimTargets>();
    world.component<MaterialAnimClocks>();
    world.component<PointLight>();
    world.component<SpotLight>();
    world.component<AreaLight>();
    world.component<SunLight>();
    world.component<AmbientLight>();
    world.component<DynamicLight>();
    world.component<FxLocalLight>();
    world.component<ModelLightingState>();
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
            FramePerfStats* perf =
                it.world().has<FramePerfStats>() ? &it.world().get_mut<FramePerfStats>() : nullptr;
            if (perf != nullptr) {
                perf->renderMs = 0.0f;
                for (float& ms : perf->cpuPassMs) {
                    ms = 0.0f;
                }
            }
            resetFrameCounters();
            BeginDrawing();
            gpuTimerFrameBegin(perf);
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
            const DebugUiState* debugUiState =
                world.has<DebugUiState>() ? &world.get<DebugUiState>() : nullptr;
            const bool unlit = debugUiState != nullptr && debugUiState->unlit;
            const bool useFreeCam =
                debugUiState != nullptr && debugUiState->freeCamera && world.has<FreeFlyCamera>();

            Camera3D presentCam;
            if (useFreeCam) {
                const FreeFlyCamera& fly = world.get<FreeFlyCamera>();
                const float cosPitch = std::cos(fly.pitch);
                const Vector3 forward = Vector3Normalize({
                    std::sin(fly.yaw) * cosPitch,
                    std::sin(fly.pitch),
                    std::cos(fly.yaw) * cosPitch,
                });
                presentCam = lens.camera;
                presentCam.position = fly.position;
                presentCam.target = Vector3Add(fly.position, forward);
                presentCam.up = {0.0f, 1.0f, 0.0f};
            } else {
                FirstPersonController controller{};
                ViewEyeOffset eyeOffset{};
                if (eyeEntity.has<FirstPersonController>()) {
                    controller = eyeEntity.get<FirstPersonController>();
                }
                if (eyeEntity.has<ViewEyeOffset>()) {
                    eyeOffset = eyeEntity.get<ViewEyeOffset>();
                }
                presentCam = presentationCamera(lens, controller, eyeOffset);
            }
            const bool usePathCam = !useFreeCam && cameraPathApplyToCamera(world, presentCam);
            Lens presentLens = lens;
            presentLens.camera = presentCam;
            const Lens& worldLens = (useFreeCam || usePathCam) ? presentLens : lens;

            const float aspect = static_cast<float>(GetRenderWidth()) /
                static_cast<float>(GetRenderHeight() > 0 ? GetRenderHeight() : 1);
            const Frustum frustum = makeFrustumFromCamera(presentCam, aspect);

            const double renderStart = perfNow();
            double cpuMarkStart = renderStart;
            FramePerfStats* perf =
                world.has<FramePerfStats>() ? &world.get_mut<FramePerfStats>() : nullptr;
            const auto cpuMark = [&](GpuPass pass) {
                const double now = perfNow();
                if (perf != nullptr) {
                    perf->cpuPassMs[static_cast<int>(pass)] +=
                        static_cast<float>((now - cpuMarkStart) * 1000.0);
                }
                cpuMarkStart = now;
            };

            const GraphicsSettings graphics =
                world.has<UserSettings>() ? world.get<UserSettings>().graphics : GraphicsSettings{};
            const bool enableDynamicLights = graphics.dynamicLights;
            const int maxUnshadowed =
                enableDynamicLights ? graphics.maxDynamicLights : 0;
            const int maxShadowed =
                (enableDynamicLights && graphics.dynamicLightShadows)
                    ? graphics.maxShadowedDynamicLights
                    : 0;

            std::vector<RankedDynamicLight> rankedLights = gatherDynamicLights(
                world,
                worldLens,
                presentLens,
                frustum,
                unlit,
                enableDynamicLights,
                maxUnshadowed,
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
            if (world.has<DynamicLightFrameState>()) {
                world.get_mut<DynamicLightFrameState>().shadowMapsActive = shadowState != nullptr;
            }

            uploadMapDynamicLights(world, rankedLights, unlit, shadowState);
            gpuTimerMark(GpuPass::Shadow);
            cpuMark(GpuPass::Shadow);

            FxLightFrameState fxLights{};
            buildFxLightFrameState(world, &frustum, unlit, fxLights);
            storeFxLightFrameState(world, std::move(fxLights));

            const bool crossfading = isLoadingCrossfadeActive(world);
            const bool playing = isPlaying(world) || crossfading;
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
            AssetStore* assetStore =
                world.has<AssetServices>() ? world.get<AssetServices>().store : nullptr;
            SkyboxShaderState* skyShaderState = nullptr;
            const SkyboxSettings* skySettings = findActiveSkybox(world);
            if (skySettings != nullptr && assetStore != nullptr) {
                skyShaderState = &ensureSkyboxShaders(*assetStore);
                drawSkyboxBackground(presentCam, *assetStore, *skyShaderState, *skySettings);
            }
            drawWorldModels(world, context, worldLens, frustum, unlit);
            if (skySettings != nullptr && assetStore != nullptr && skyShaderState != nullptr) {
                drawSkyMaterialFaces(world, presentCam, *assetStore, *skyShaderState, *skySettings);
            }
            gpuTimerMark(GpuPass::World);
            cpuMark(GpuPass::World);
            const std::string spriteAimStatus =
                drawWorldTransparentPass(world, context, worldLens, frustum, unlit);
            gpuTimerMark(GpuPass::Transparent);
            cpuMark(GpuPass::Transparent);
            if (world.has<AssetServices>() && world.get<AssetServices>().store != nullptr) {
                drawParticleSystems(
                    world, *world.get_mut<AssetServices>().store, presentCam, unlit);
                drawTrailEffects(world, *world.get_mut<AssetServices>().store, presentCam, unlit);
            }
            drawTracers(world, presentCam);
            drawWorldDebugOverlays(world);
            EndMode3D();
            gpuTimerMark(GpuPass::Effects);
            cpuMark(GpuPass::Effects);

            if (playing) {
                const bool hideFp =
                    (debugUiState != nullptr && debugUiState->hideFpScene) || useFreeCam ||
                    usePathCam ||
                    (world.has<FpViewOverride>() && world.get<FpViewOverride>().hideWeapon);
                const bool hideHud = debugUiState != nullptr && debugUiState->hideHud;

                if (!hideFp && world.has<AssetServices>() &&
                    world.get<AssetServices>().store != nullptr) {
                    BeginMode3D(presentCam);
                    drawMuzzleParticleSystems(
                        world, *world.get_mut<AssetServices>().store, presentCam, unlit);
                    EndMode3D();
                }
                if (!hideFp) {
                    drawFirstPersonPass(world, context, lens, unlit);
                    drawViewSprites(world, unlit);
                }
                gpuTimerMark(GpuPass::FirstPerson);
                cpuMark(GpuPass::FirstPerson);
                if (sceneToTexture) {
                    EndTextureMode();
                    if (crossfading && assetStore != nullptr) {
                        const LoadingSession& session = world.get<LoadingSession>();
                        presentLoadingCrossfade(
                            *postState,
                            *assetStore,
                            session.snapshotTexture,
                            session.crossfadeElapsedSeconds / session.crossfadeDurationSeconds);
                    } else if (crossfading) {
                        presentPostProcessSceneOnly(*postState);
                    } else if (!hideHud && postProcessNeedsCompositePipeline(*postState)) {
                        beginHudCapture(*postState);
                        drawHud(world);
                        drawSpriteAimHudText(spriteAimStatus);
                        endHudCapture(*postState);
                        presentPostProcessComposite(*postState);
                    } else {
                        presentPostProcessSceneOnly(*postState);
                        if (!hideHud) {
                            drawHud(world);
                            drawSpriteAimHudText(spriteAimStatus);
                        }
                    }
                }
                gpuTimerMark(GpuPass::Post);
                cpuMark(GpuPass::Post);
            }

            if (perf != nullptr) {
                perf->renderMs += perfElapsedMs(renderStart);
            }
        });

    registerExtraEyeSystems(world);

    world.system("MenuTitleOverlay")
        .kind(flecs::PostUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            if (!isMenu(world) || hasActiveLoadingSession(world)) {
                return;
            }
            drawMenuBackgroundImage(world);
            drawMenuTitleCanvas(world);
        });

    registerLoadingOverlayModule(world);

    world.system("ImGuiOverlay")
        .kind(flecs::PostUpdate)
        .run([](flecs::iter& it) {
            FramePerfStats* perf =
                it.world().has<FramePerfStats>() ? &it.world().get_mut<FramePerfStats>() : nullptr;
            if (perf != nullptr) {
                perf->frameMs = GetFrameTime() * 1000.0f;
            }

            if (perf != nullptr) {
                perf->counters = frameCounters();
            }
            if (it.world().has<SystemPerfState>()) {
                flecs::world world = it.world();
                SystemPerfState& systemPerf = world.get_mut<SystemPerfState>();
                ++systemPerf.framesSinceSample;
                const double now = perfNow();
                if (now - systemPerf.lastSampleTime >= 0.5) {
                    systemPerf.lastSampleTime = now;
                    sampleSystemPerf(world, systemPerf, 12);
                }
            }
            prepareUiInput(it.world());
            rlImGuiBegin();
            const double uiStart = perfNow();
            if (!hasActiveLoadingSession(it.world())) {
                drawUi(it.world());
            }
            if (perf != nullptr) {
                perf->uiMs = perfElapsedMs(uiStart);
                perf->pushSample();
                if (it.world().has<PerfLogState>()) {
                    PerfLogState& log = it.world().get_mut<PerfLogState>();
                    const int framesBefore = log.frames;
                    perfLogSample(log, *perf);
                    if (log.frames < framesBefore && it.world().has<SystemPerfState>()) {
                        std::string line = "PERF: systems";
                        for (const SystemTime& time : it.world().get<SystemPerfState>().top) {
                            line += ' ' + time.name + ' ' + TextFormat("%.2f", time.msPerFrame);
                        }
                        TraceLog(LOG_INFO, "%s", line.c_str());
                    }
                }
            }
            rlImGuiEnd();
            gpuTimerMark(GpuPass::Ui);
        });

    world.system("EndDrawing")
        .kind(flecs::PostUpdate)
        .run([](flecs::iter& it) {
            const double swapStart = perfNow();
            EndDrawing();
            if (it.world().has<FramePerfStats>()) {
                it.world().get_mut<FramePerfStats>().swapMs = perfElapsedMs(swapStart);
            }
            ScreenshotRequest& request = it.world().get_mut<ScreenshotRequest>();
            if (!request.pending) {
                return;
            }
            request.pending = false;
            std::filesystem::path path;
            std::string error;
            if (saveScreenshotPng(path, error)) {
                TraceLog(LOG_INFO, "SCREENSHOT: Screenshot saved: %s", path.string().c_str());
            } else {
                TraceLog(LOG_WARNING, "SCREENSHOT: Screenshot failed: %s", error.c_str());
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
    world.set<ModelLightingState>({});
    world.set<FpViewOverride>({});
    world.set<PerfLogState>(PerfLogState{.enabled = config.perfLog});
    {
        SystemPerfState systemPerf{};
        if (config.perfLog || config.debug) {
            enableSystemPerf(world, systemPerf);
        }
        world.set<SystemPerfState>(std::move(systemPerf));
    }

    registerSpinSystem(world);
    registerSchemeTickSystem(world);
    registerAnimationSystems(world);
    registerAnimationClipFlipTestSystem(world);
    registerTransformSystems(world);
    registerSpriteAnimatorSystem(world);
    registerMaterialAnimSystem(world);
    registerRenderSystems(world);
}

}
