#include "render/post_process.hpp"

#include <algorithm>
#include <utility>

namespace slopengine {

namespace {

void resolveBuiltinLocations(PostPass& pass) {
    pass.resolutionLoc = -1;
    pass.timeLoc = -1;
    pass.locationCache.clear();
    if (!IsShaderValid(pass.shader)) {
        return;
    }
    pass.resolutionLoc = GetShaderLocation(pass.shader, "resolution");
    pass.timeLoc = GetShaderLocation(pass.shader, "time");
}

int locationFor(PostPass& pass, const std::string& name) {
    const auto it = pass.locationCache.find(name);
    if (it != pass.locationCache.end()) {
        return it->second;
    }
    const int loc = GetShaderLocation(pass.shader, name.c_str());
    pass.locationCache.emplace(name, loc);
    return loc;
}

void uploadCustomUniforms(PostPass& pass) {
    if (!IsShaderValid(pass.shader)) {
        return;
    }
    for (const auto& [name, value] : pass.uniforms) {
        const int loc = locationFor(pass, name);
        if (loc < 0) {
            continue;
        }
        switch (value.kind) {
        case PostUniformKind::Float:
            SetShaderValue(pass.shader, loc, value.data, SHADER_UNIFORM_FLOAT);
            break;
        case PostUniformKind::Vec2:
            SetShaderValue(pass.shader, loc, value.data, SHADER_UNIFORM_VEC2);
            break;
        case PostUniformKind::Vec3:
            SetShaderValue(pass.shader, loc, value.data, SHADER_UNIFORM_VEC3);
            break;
        case PostUniformKind::Vec4:
            SetShaderValue(pass.shader, loc, value.data, SHADER_UNIFORM_VEC4);
            break;
        }
    }
}

void applyBuiltinUniforms(PostPass& pass, int width, int height) {
    if (pass.resolutionLoc >= 0) {
        const float resolution[2] = {static_cast<float>(width), static_cast<float>(height)};
        SetShaderValue(pass.shader, pass.resolutionLoc, resolution, SHADER_UNIFORM_VEC2);
    }
    if (pass.timeLoc >= 0) {
        const float time = static_cast<float>(GetTime());
        SetShaderValue(pass.shader, pass.timeLoc, &time, SHADER_UNIFORM_FLOAT);
    }
}

void drawFullscreenFlippedY(const Texture2D& tex) {
    const Rectangle src{
        0.0f,
        0.0f,
        static_cast<float>(tex.width),
        -static_cast<float>(tex.height),
    };
    DrawTextureRec(tex, src, Vector2{0.0f, 0.0f}, WHITE);
}

bool ensureScratchPair(RenderTexture2D scratch[2], int& scratchW, int& scratchH, int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (scratch[0].id != 0 && scratch[1].id != 0 && scratchW == width && scratchH == height) {
        return true;
    }
    for (int i = 0; i < 2; ++i) {
        if (scratch[i].id != 0) {
            UnloadRenderTexture(scratch[i]);
        }
        scratch[i] = LoadRenderTexture(width, height);
    }
    if (scratch[0].id == 0 || scratch[1].id == 0) {
        scratchW = 0;
        scratchH = 0;
        return false;
    }
    scratchW = width;
    scratchH = height;
    return true;
}

bool hasActivePasses(const std::vector<PostPass>& passes) {
    for (const auto& pass : passes) {
        if (pass.enabled && IsShaderValid(pass.shader)) {
            return true;
        }
    }
    return false;
}

std::vector<PostPass>& passListFor(PostProcessState& state, PostPassTarget target) {
    switch (target) {
    case PostPassTarget::Scene:
        return state.scenePasses;
    case PostPassTarget::Hud:
        return state.hudPasses;
    case PostPassTarget::Both:
        return state.bothPasses;
    }
    return state.scenePasses;
}

PostPass* findPostPass(PostProcessState& state, PostPassHandle handle) {
    if (handle == 0) {
        return nullptr;
    }
    for (auto* list : {&state.scenePasses, &state.hudPasses, &state.bothPasses}) {
        for (auto& pass : *list) {
            if (pass.handle == handle) {
                return &pass;
            }
        }
    }
    return nullptr;
}

// Runs `passes` starting from `input`, ping-ponging through `scratch`. When
// `lastWritesToScreen` is true, the final active pass renders directly onto
// whatever target is currently bound (the backbuffer) instead of a scratch
// texture; otherwise every pass writes to a scratch texture so the result can
// be held for a later stage. `firstTargetIndex` picks which scratch slot the
// first intermediate write lands in, so callers seeding `input` from
// `scratch[0]` itself (e.g. a composited texture) can start at slot 1 and
// avoid reading and writing the same render texture in one draw.
Texture2D runShaderChain(
    std::vector<PostPass>& passes,
    Texture2D input,
    RenderTexture2D scratch[2],
    int& scratchW,
    int& scratchH,
    int width,
    int height,
    int firstTargetIndex,
    bool lastWritesToScreen,
    Color clearColor) {
    std::vector<PostPass*> active;
    active.reserve(passes.size());
    for (auto& pass : passes) {
        if (pass.enabled && IsShaderValid(pass.shader)) {
            active.push_back(&pass);
        }
    }
    if (active.empty()) {
        if (lastWritesToScreen) {
            drawFullscreenFlippedY(input);
        }
        return input;
    }

    const size_t scratchWrites = lastWritesToScreen ? active.size() - 1 : active.size();
    if (scratchWrites > 0 && !ensureScratchPair(scratch, scratchW, scratchH, width, height)) {
        if (lastWritesToScreen) {
            drawFullscreenFlippedY(input);
        }
        return input;
    }

    Texture2D current = input;
    int idx = firstTargetIndex;
    for (size_t i = 0; i < active.size(); ++i) {
        PostPass& pass = *active[i];
        const bool isFinal = (i + 1 == active.size());
        applyBuiltinUniforms(pass, width, height);
        uploadCustomUniforms(pass);
        if (isFinal && lastWritesToScreen) {
            BeginShaderMode(pass.shader);
            drawFullscreenFlippedY(current);
            EndShaderMode();
            return current;
        }
        RenderTexture2D& target = scratch[idx];
        BeginTextureMode(target);
        ClearBackground(clearColor);
        BeginShaderMode(pass.shader);
        drawFullscreenFlippedY(current);
        EndShaderMode();
        EndTextureMode();
        current = target.texture;
        idx = 1 - idx;
    }
    return current;
}

} // namespace

PostProcessState::PostProcessState(PostProcessState&& other) noexcept
    : scene(std::exchange(other.scene, RenderTexture2D{}))
    , sceneW(std::exchange(other.sceneW, 0))
    , sceneH(std::exchange(other.sceneH, 0))
    , hud(std::exchange(other.hud, RenderTexture2D{}))
    , hudW(std::exchange(other.hudW, 0))
    , hudH(std::exchange(other.hudH, 0))
    , sceneScratch{
          std::exchange(other.sceneScratch[0], RenderTexture2D{}),
          std::exchange(other.sceneScratch[1], RenderTexture2D{})}
    , sceneScratchW(std::exchange(other.sceneScratchW, 0))
    , sceneScratchH(std::exchange(other.sceneScratchH, 0))
    , hudScratch{
          std::exchange(other.hudScratch[0], RenderTexture2D{}),
          std::exchange(other.hudScratch[1], RenderTexture2D{})}
    , hudScratchW(std::exchange(other.hudScratchW, 0))
    , hudScratchH(std::exchange(other.hudScratchH, 0))
    , bothScratch{
          std::exchange(other.bothScratch[0], RenderTexture2D{}),
          std::exchange(other.bothScratch[1], RenderTexture2D{})}
    , bothScratchW(std::exchange(other.bothScratchW, 0))
    , bothScratchH(std::exchange(other.bothScratchH, 0))
    , scenePasses(std::move(other.scenePasses))
    , hudPasses(std::move(other.hudPasses))
    , bothPasses(std::move(other.bothPasses))
    , nextHandle(other.nextHandle) {
}

PostProcessState& PostProcessState::operator=(PostProcessState&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    unload();
    scene = std::exchange(other.scene, RenderTexture2D{});
    sceneW = std::exchange(other.sceneW, 0);
    sceneH = std::exchange(other.sceneH, 0);
    hud = std::exchange(other.hud, RenderTexture2D{});
    hudW = std::exchange(other.hudW, 0);
    hudH = std::exchange(other.hudH, 0);
    sceneScratch[0] = std::exchange(other.sceneScratch[0], RenderTexture2D{});
    sceneScratch[1] = std::exchange(other.sceneScratch[1], RenderTexture2D{});
    sceneScratchW = std::exchange(other.sceneScratchW, 0);
    sceneScratchH = std::exchange(other.sceneScratchH, 0);
    hudScratch[0] = std::exchange(other.hudScratch[0], RenderTexture2D{});
    hudScratch[1] = std::exchange(other.hudScratch[1], RenderTexture2D{});
    hudScratchW = std::exchange(other.hudScratchW, 0);
    hudScratchH = std::exchange(other.hudScratchH, 0);
    bothScratch[0] = std::exchange(other.bothScratch[0], RenderTexture2D{});
    bothScratch[1] = std::exchange(other.bothScratch[1], RenderTexture2D{});
    bothScratchW = std::exchange(other.bothScratchW, 0);
    bothScratchH = std::exchange(other.bothScratchH, 0);
    scenePasses = std::move(other.scenePasses);
    hudPasses = std::move(other.hudPasses);
    bothPasses = std::move(other.bothPasses);
    nextHandle = other.nextHandle;
    return *this;
}

PostProcessState::~PostProcessState() {
    unload();
}

void PostProcessState::unload() {
    for (auto* list : {&scenePasses, &hudPasses, &bothPasses}) {
        for (auto& pass : *list) {
            if (pass.shader.id != 0) {
                UnloadShader(pass.shader);
            }
        }
        list->clear();
    }

    if (scene.id != 0) {
        UnloadRenderTexture(scene);
    }
    scene = {};
    sceneW = 0;
    sceneH = 0;

    if (hud.id != 0) {
        UnloadRenderTexture(hud);
    }
    hud = {};
    hudW = 0;
    hudH = 0;

    for (auto& tex : sceneScratch) {
        if (tex.id != 0) {
            UnloadRenderTexture(tex);
        }
        tex = {};
    }
    sceneScratchW = 0;
    sceneScratchH = 0;

    for (auto& tex : hudScratch) {
        if (tex.id != 0) {
            UnloadRenderTexture(tex);
        }
        tex = {};
    }
    hudScratchW = 0;
    hudScratchH = 0;

    for (auto& tex : bothScratch) {
        if (tex.id != 0) {
            UnloadRenderTexture(tex);
        }
        tex = {};
    }
    bothScratchW = 0;
    bothScratchH = 0;
}

PostProcessState& ensurePostProcessState(flecs::world& world) {
    if (PostProcessState* existing = world.try_get_mut<PostProcessState>()) {
        return *existing;
    }
    const bool suspended = world.is_deferred();
    if (suspended) {
        world.defer_suspend();
    }
    world.set<PostProcessState>({});
    if (suspended) {
        world.defer_resume();
    }
    return world.get_mut<PostProcessState>();
}

bool ensurePostProcessScene(PostProcessState& state, int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (state.scene.id != 0 && state.sceneW == width && state.sceneH == height) {
        return true;
    }
    if (state.scene.id != 0) {
        UnloadRenderTexture(state.scene);
    }
    state.scene = LoadRenderTexture(width, height);
    if (state.scene.id == 0) {
        state.sceneW = 0;
        state.sceneH = 0;
        return false;
    }
    state.sceneW = width;
    state.sceneH = height;
    return true;
}

bool ensurePostProcessHud(PostProcessState& state, int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (state.hud.id != 0 && state.hudW == width && state.hudH == height) {
        return true;
    }
    if (state.hud.id != 0) {
        UnloadRenderTexture(state.hud);
    }
    state.hud = LoadRenderTexture(width, height);
    if (state.hud.id == 0) {
        state.hudW = 0;
        state.hudH = 0;
        return false;
    }
    state.hudW = width;
    state.hudH = height;
    return true;
}

bool postProcessNeedsCompositePipeline(const PostProcessState& state) {
    return hasActivePasses(state.hudPasses) || hasActivePasses(state.bothPasses);
}

PostPassHandle pushPostShader(
    PostProcessState& state,
    AssetStore& assets,
    PostPassTarget target,
    std::string_view fragPath) {
    if (fragPath.empty() || !assets.hasShader(fragPath) || !assets.hasShader("default/post_vert")) {
        return 0;
    }
    const std::string vert = assets.getShaderSource("default/post_vert");
    const std::string frag = assets.getShaderSource(fragPath);
    if (vert.empty() || frag.empty()) {
        return 0;
    }
    Shader compiled = LoadShaderFromMemory(vert.c_str(), frag.c_str());
    if (!IsShaderValid(compiled)) {
        if (compiled.id != 0) {
            UnloadShader(compiled);
        }
        return 0;
    }

    PostPass pass{};
    pass.handle = state.nextHandle++;
    pass.shader = compiled;
    pass.fragPath = std::string(fragPath);
    pass.enabled = true;
    resolveBuiltinLocations(pass);

    std::vector<PostPass>& list = passListFor(state, target);
    list.push_back(std::move(pass));
    return list.back().handle;
}

bool removePostShader(PostProcessState& state, PostPassHandle handle) {
    if (handle == 0) {
        return false;
    }
    for (auto* list : {&state.scenePasses, &state.hudPasses, &state.bothPasses}) {
        const auto it = std::find_if(
            list->begin(), list->end(), [&](const PostPass& pass) { return pass.handle == handle; });
        if (it != list->end()) {
            if (it->shader.id != 0) {
                UnloadShader(it->shader);
            }
            list->erase(it);
            return true;
        }
    }
    return false;
}

bool clearPostShaders(PostProcessState& state, PostPassTarget target) {
    std::vector<PostPass>& list = passListFor(state, target);
    for (auto& pass : list) {
        if (pass.shader.id != 0) {
            UnloadShader(pass.shader);
        }
    }
    list.clear();
    return true;
}

bool setPostPassEnabled(PostProcessState& state, PostPassHandle handle, bool enabled) {
    PostPass* pass = findPostPass(state, handle);
    if (pass == nullptr) {
        return false;
    }
    pass->enabled = enabled;
    return true;
}

bool setPostPassUniform(
    PostProcessState& state,
    PostPassHandle handle,
    std::string_view name,
    const PostUniformValue& value) {
    if (name.empty()) {
        return false;
    }
    PostPass* pass = findPostPass(state, handle);
    if (pass == nullptr) {
        return false;
    }
    pass->uniforms[std::string(name)] = value;
    return true;
}

void presentPostProcessSceneOnly(PostProcessState& state) {
    if (state.scene.id == 0) {
        return;
    }
    runShaderChain(
        state.scenePasses,
        state.scene.texture,
        state.sceneScratch,
        state.sceneScratchW,
        state.sceneScratchH,
        state.sceneW,
        state.sceneH,
        /*firstTargetIndex=*/0,
        /*lastWritesToScreen=*/true,
        BLACK);
}

void beginHudCapture(PostProcessState& state) {
    if (!ensurePostProcessHud(state, state.sceneW, state.sceneH)) {
        return;
    }
    BeginTextureMode(state.hud);
    ClearBackground(Color{0, 0, 0, 0});
}

void endHudCapture(PostProcessState& state) {
    if (state.hud.id != 0) {
        EndTextureMode();
    }
}

void presentPostProcessComposite(PostProcessState& state) {
    if (state.scene.id == 0) {
        return;
    }

    const Texture2D sceneFinal = runShaderChain(
        state.scenePasses,
        state.scene.texture,
        state.sceneScratch,
        state.sceneScratchW,
        state.sceneScratchH,
        state.sceneW,
        state.sceneH,
        0,
        false,
        BLACK);

    const bool hasHud = state.hud.id != 0;
    const Texture2D hudFinal = hasHud
        ? runShaderChain(
              state.hudPasses,
              state.hud.texture,
              state.hudScratch,
              state.hudScratchW,
              state.hudScratchH,
              state.hudW,
              state.hudH,
              0,
              false,
              Color{0, 0, 0, 0})
        : Texture2D{};

    const auto drawComposite = [&]() {
        drawFullscreenFlippedY(sceneFinal);
        if (hasHud) {
            BeginBlendMode(BLEND_ALPHA);
            drawFullscreenFlippedY(hudFinal);
            EndBlendMode();
        }
    };

    if (!hasActivePasses(state.bothPasses)) {
        drawComposite();
        return;
    }

    if (!ensureScratchPair(state.bothScratch, state.bothScratchW, state.bothScratchH, state.sceneW, state.sceneH)) {
        drawComposite();
        return;
    }

    BeginTextureMode(state.bothScratch[0]);
    ClearBackground(BLACK);
    drawComposite();
    EndTextureMode();

    runShaderChain(
        state.bothPasses,
        state.bothScratch[0].texture,
        state.bothScratch,
        state.bothScratchW,
        state.bothScratchH,
        state.sceneW,
        state.sceneH,
        /*firstTargetIndex=*/1,
        /*lastWritesToScreen=*/true,
        BLACK);
}

}
