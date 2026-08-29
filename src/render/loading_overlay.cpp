#include "render/loading_overlay.hpp"

#include "game/loading_session.hpp"

#include <raylib.h>

#include <string>

namespace slopengine {

namespace {

Shader g_crossfadeShader{};
int g_crossfadeSnapshotLoc = -1;
int g_crossfadeMixLoc = -1;
bool g_crossfadeShaderAttempted = false;

void ensureCrossfadeShader(AssetStore& assets) {
    if (g_crossfadeShaderAttempted) {
        return;
    }
    g_crossfadeShaderAttempted = true;
    if (!assets.hasShader("default/post_vert") || !assets.hasShader("default/loading_crossfade_frag")) {
        return;
    }
    const std::string vert = assets.getShaderSource("default/post_vert");
    const std::string frag = assets.getShaderSource("default/loading_crossfade_frag");
    if (vert.empty() || frag.empty()) {
        return;
    }
    Shader compiled = LoadShaderFromMemory(vert.c_str(), frag.c_str());
    if (!IsShaderValid(compiled)) {
        if (compiled.id != 0) {
            UnloadShader(compiled);
        }
        return;
    }
    g_crossfadeShader = compiled;
    g_crossfadeSnapshotLoc = GetShaderLocation(g_crossfadeShader, "snapshotTexture");
    g_crossfadeMixLoc = GetShaderLocation(g_crossfadeShader, "mixT");
}

}

void registerLoadingOverlayModule(flecs::world& world) {
    world.system("LoadingOverlay")
        .kind(flecs::PostUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            if (!hasActiveLoadingSession(world)) {
                return;
            }
            const LoadingSession& session = world.get<LoadingSession>();
            if (session.phase != LoadingPhase::Stages) {
                return;
            }

            const float screenW = static_cast<float>(GetScreenWidth());
            const float screenH = static_cast<float>(GetScreenHeight());
            if (session.snapshotTexture.id != 0) {
                const Rectangle src{
                    0.0f,
                    0.0f,
                    static_cast<float>(session.snapshotTexture.width),
                    static_cast<float>(session.snapshotTexture.height),
                };
                const Rectangle dst{0.0f, 0.0f, screenW, screenH};
                DrawTexturePro(session.snapshotTexture, src, dst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
            }

            const int fontSize = 20;
            const int textW = MeasureText(session.stageLabel.c_str(), fontSize);
            DrawText(
                session.stageLabel.c_str(),
                (GetScreenWidth() - textW) / 2,
                GetScreenHeight() - 48,
                fontSize,
                WHITE);
        });
}

void presentLoadingCrossfade(
    PostProcessState& postState,
    AssetStore& assets,
    Texture2D snapshot,
    float mixT) {
    ensureCrossfadeShader(assets);
    if (!IsShaderValid(g_crossfadeShader) || g_crossfadeSnapshotLoc < 0 || g_crossfadeMixLoc < 0 ||
        snapshot.id == 0) {
        presentPostProcessSceneOnly(postState);
        return;
    }

    const float clamped = mixT < 0.0f ? 0.0f : (mixT > 1.0f ? 1.0f : mixT);
    BeginShaderMode(g_crossfadeShader);
    SetShaderValueTexture(g_crossfadeShader, g_crossfadeSnapshotLoc, snapshot);
    SetShaderValue(g_crossfadeShader, g_crossfadeMixLoc, &clamped, SHADER_UNIFORM_FLOAT);
    const Rectangle src{
        0.0f,
        0.0f,
        static_cast<float>(postState.scene.texture.width),
        -static_cast<float>(postState.scene.texture.height),
    };
    DrawTextureRec(postState.scene.texture, src, Vector2{0.0f, 0.0f}, WHITE);
    EndShaderMode();
}

}
