#include "render/render_module.hpp"

#include "assets/asset_services.hpp"
#include "camera/components.hpp"
#include "core/screenshot.hpp"
#include "game/game_state.hpp"
#include "map/bsp.hpp"
#include "map/light_components.hpp"
#include "render/animation_player.hpp"
#include "render/animation_systems.hpp"
#include "render/components.hpp"
#include "render/dynamic_light.hpp"
#include "render/hud.hpp"
#include "render/render_context.hpp"
#include "render/render_pass_fp.hpp"
#include "render/render_pass_world.hpp"
#include "render/sprite_animator.hpp"
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
    world.component<ViewSprite>();
    world.component<HudDrawList>();
    world.component<HudFontCache>();
    world.component<Lens>();
    world.component<Spin>();
    world.component<Model3D>();
    world.component<SpriteInstance>();
    world.component<SpriteAnimator>();
    world.component<AnimationPlayer>();
    world.component<AnimationClipFlipTest>();
    world.component<PointLight>();
    world.component<SpotLight>();
    world.component<AreaLight>();
    world.component<SunLight>();
    world.component<DynamicLight>();
    world.component<MapOwned>();
    world.component<CurrentMap>();
    world.component<ShaderCavity>()
        .on_remove([](flecs::iter&, size_t, ShaderCavity& shader) {
            if (shader.shader.id != 0) {
                UnloadShader(shader.shader);
            }
            shader.shader = {};
        });
}

void registerRenderSystems(flecs::world& world) {
    world.system("StartDrawing")
        .kind(flecs::PostUpdate)
        .run([](flecs::iter&) {
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
            if (!isPlaying(world)) {
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

            std::vector<RankedDynamicLight> rankedLights =
                gatherDynamicLights(world, lens, presentLens, unlit);
            storeDynamicLightFrameState(world, rankedLights);

            BeginMode3D(presentCam);
            drawWorldModels(world, context, lens, rankedLights, unlit);
            const std::string spriteAimStatus =
                drawWorldSprites(world, context, lens, unlit);
            drawWorldDebugOverlays(world);
            EndMode3D();

            drawFirstPersonPass(world, context, lens, unlit);
            drawViewSpritesAndHud(world);
            drawSpriteAimHudText(spriteAimStatus);
        });

    world.system("ImGuiOverlay")
        .kind(flecs::PostUpdate)
        .run([](flecs::iter& it) {
            prepareUiInput(it.world());
            rlImGuiBegin();
            drawUi(it.world());
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
        world.query<Model3D, GlobalTransformation>(),
        world.query<SpriteInstance, GlobalTransformation>(),
    });

    registerSpinSystem(world);
    registerSchemeTickSystem(world);
    registerAnimationSystems(world);
    registerAnimationClipFlipTestSystem(world);
    registerSpriteAnimatorSystem(world);
    registerTransformSystems(world);
    registerRenderSystems(world);

    (void)config;
}

}
