#include "render/render_pass_extra_eye.hpp"

#include "camera/components.hpp"
#include "game/game_state.hpp"
#include "game/menu_background.hpp"
#include "render/components.hpp"
#include "render/render_context.hpp"
#include "render/render_frustum.hpp"
#include "render/render_pass_world.hpp"
#include "ui/ui_state.hpp"

#include <raylib.h>

namespace slopengine {

namespace {

bool ensureExtraEyeTarget(ExtraEye& eye) {
    if (eye.width <= 0 || eye.height <= 0) {
        return false;
    }
    if (eye.target.id != 0 && eye.target.texture.width == eye.width &&
        eye.target.texture.height == eye.height) {
        return true;
    }
    if (eye.target.id != 0) {
        UnloadRenderTexture(eye.target);
        eye.target = {};
    }
    eye.target = LoadRenderTexture(eye.width, eye.height);
    return eye.target.id != 0;
}

} // namespace

void registerExtraEyeSystems(flecs::world& world) {
    world.system("ExtraEyeRender")
        .kind(flecs::PostUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            if (!shouldDrawWorld(world) || !isPlaying(world)) {
                return;
            }
            flecs::entity player = world.lookup("Player");
            if (!player.is_valid() || !player.has<Lens>() || !player.has<ExtraEye>()) {
                return;
            }
            ExtraEye& eye = player.get_mut<ExtraEye>();
            if (!eye.active) {
                return;
            }
            if (!ensureExtraEyeTarget(eye)) {
                TraceLog(LOG_WARNING, "SCOPE-DEBUG: ExtraEyeRender: ensureExtraEyeTarget failed w=%d h=%d",
                         eye.width, eye.height);
                return;
            }
            TraceLog(LOG_INFO, "SCOPE-DEBUG: ExtraEyeRender: rendering target id=%u %dx%d",
                     eye.target.id, eye.width, eye.height);

            const Lens& lens = player.get<Lens>();
            eye.camera.position = lens.camera.position;
            eye.camera.target = lens.camera.target;
            eye.camera.up = lens.camera.up;
            eye.camera.fovy = eye.fovy;
            eye.camera.projection = CAMERA_PERSPECTIVE;

            Lens eyeLens{};
            eyeLens.camera = eye.camera;

            const float aspect = static_cast<float>(eye.width) / static_cast<float>(eye.height);
            const Frustum frustum = makeFrustumFromCamera(eye.camera, aspect);

            RenderContext& context = world.get_mut<RenderContext>();
            const bool unlit = world.has<DebugUiState>() && world.get<DebugUiState>().unlit;

            BeginTextureMode(eye.target);
            ClearBackground(BLACK);
            BeginMode3D(eye.camera);
            drawWorldModels(world, context, eyeLens, frustum, unlit);
            drawWorldTransparentPass(world, context, eyeLens, frustum, unlit);
            EndMode3D();
            EndTextureMode();
        });
}

}
