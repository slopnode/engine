#include "render/post_process.hpp"

#include <utility>

namespace slopengine {

namespace {

void resolveBuiltinLocations(PostProcessState& state) {
    state.resolutionLoc = -1;
    state.timeLoc = -1;
    state.locationCache.clear();
    if (!IsShaderValid(state.shader)) {
        return;
    }
    state.resolutionLoc = GetShaderLocation(state.shader, "resolution");
    state.timeLoc = GetShaderLocation(state.shader, "time");
}

int locationFor(PostProcessState& state, const std::string& name) {
    const auto it = state.locationCache.find(name);
    if (it != state.locationCache.end()) {
        return it->second;
    }
    const int loc = GetShaderLocation(state.shader, name.c_str());
    state.locationCache.emplace(name, loc);
    return loc;
}

void uploadCustomUniforms(PostProcessState& state) {
    if (!IsShaderValid(state.shader)) {
        return;
    }
    for (const auto& [name, value] : state.uniforms) {
        const int loc = locationFor(state, name);
        if (loc < 0) {
            continue;
        }
        switch (value.kind) {
        case PostUniformKind::Float:
            SetShaderValue(state.shader, loc, value.data, SHADER_UNIFORM_FLOAT);
            break;
        case PostUniformKind::Vec2:
            SetShaderValue(state.shader, loc, value.data, SHADER_UNIFORM_VEC2);
            break;
        case PostUniformKind::Vec3:
            SetShaderValue(state.shader, loc, value.data, SHADER_UNIFORM_VEC3);
            break;
        case PostUniformKind::Vec4:
            SetShaderValue(state.shader, loc, value.data, SHADER_UNIFORM_VEC4);
            break;
        }
    }
}

void drawSceneFullscreen(const PostProcessState& state) {
    const Texture2D& tex = state.scene.texture;
    const Rectangle src{
        0.0f,
        0.0f,
        static_cast<float>(tex.width),
        -static_cast<float>(tex.height),
    };
    DrawTextureRec(tex, src, Vector2{0.0f, 0.0f}, WHITE);
}

} // namespace

PostProcessState::PostProcessState(PostProcessState&& other) noexcept
    : scene(other.scene)
    , sceneW(other.sceneW)
    , sceneH(other.sceneH)
    , shader(other.shader)
    , fragPath(std::move(other.fragPath))
    , enabled(other.enabled)
    , uniforms(std::move(other.uniforms))
    , locationCache(std::move(other.locationCache))
    , resolutionLoc(other.resolutionLoc)
    , timeLoc(other.timeLoc) {
    other.scene = {};
    other.sceneW = 0;
    other.sceneH = 0;
    other.shader = {};
    other.enabled = false;
    other.resolutionLoc = -1;
    other.timeLoc = -1;
}

PostProcessState& PostProcessState::operator=(PostProcessState&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    unload();
    scene = other.scene;
    sceneW = other.sceneW;
    sceneH = other.sceneH;
    shader = other.shader;
    fragPath = std::move(other.fragPath);
    enabled = other.enabled;
    uniforms = std::move(other.uniforms);
    locationCache = std::move(other.locationCache);
    resolutionLoc = other.resolutionLoc;
    timeLoc = other.timeLoc;
    other.scene = {};
    other.sceneW = 0;
    other.sceneH = 0;
    other.shader = {};
    other.enabled = false;
    other.resolutionLoc = -1;
    other.timeLoc = -1;
    return *this;
}

PostProcessState::~PostProcessState() {
    unload();
}

void PostProcessState::unloadScene() {
    if (scene.id != 0) {
        UnloadRenderTexture(scene);
        scene = {};
    }
    sceneW = 0;
    sceneH = 0;
}

void PostProcessState::unloadShader() {
    if (shader.id != 0) {
        UnloadShader(shader);
        shader = {};
    }
    fragPath.clear();
    uniforms.clear();
    locationCache.clear();
    resolutionLoc = -1;
    timeLoc = -1;
}

void PostProcessState::unload() {
    unloadShader();
    unloadScene();
    enabled = false;
}

PostProcessState& ensurePostProcessState(flecs::world& world) {
    if (!world.has<PostProcessState>()) {
        world.set<PostProcessState>({});
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
    state.unloadScene();
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

bool loadPostProcessShader(
    PostProcessState& state,
    AssetStore& assets,
    std::string_view fragPath) {
    if (fragPath.empty() || !assets.hasShader(fragPath) || !assets.hasShader("default/post_vert")) {
        return false;
    }
    const std::string vert = assets.getShaderSource("default/post_vert");
    const std::string frag = assets.getShaderSource(fragPath);
    if (vert.empty() || frag.empty()) {
        return false;
    }
    Shader compiled = LoadShaderFromMemory(vert.c_str(), frag.c_str());
    if (!IsShaderValid(compiled)) {
        if (compiled.id != 0) {
            UnloadShader(compiled);
        }
        return false;
    }
    state.unloadShader();
    state.shader = compiled;
    state.fragPath = std::string(fragPath);
    state.enabled = true;
    resolveBuiltinLocations(state);
    return true;
}

void clearPostProcessShader(PostProcessState& state) {
    state.unloadShader();
    state.enabled = false;
}

void presentPostProcess(PostProcessState& state) {
    if (state.scene.id == 0) {
        return;
    }

    const bool useShader = state.enabled && IsShaderValid(state.shader);
    if (useShader) {
        if (state.resolutionLoc >= 0) {
            const float resolution[2] = {
                static_cast<float>(state.sceneW),
                static_cast<float>(state.sceneH),
            };
            SetShaderValue(state.shader, state.resolutionLoc, resolution, SHADER_UNIFORM_VEC2);
        }
        if (state.timeLoc >= 0) {
            const float time = static_cast<float>(GetTime());
            SetShaderValue(state.shader, state.timeLoc, &time, SHADER_UNIFORM_FLOAT);
        }
        uploadCustomUniforms(state);
        BeginShaderMode(state.shader);
        drawSceneFullscreen(state);
        EndShaderMode();
        return;
    }

    drawSceneFullscreen(state);
}

}
