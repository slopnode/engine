#pragma once

#include "assets/asset_store.hpp"

#include <cstdint>
#include <flecs.h>
#include <raylib.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace slopengine {

enum class PostUniformKind {
    Float,
    Vec2,
    Vec3,
    Vec4,
};

struct PostUniformValue {
    PostUniformKind kind = PostUniformKind::Float;
    float data[4]{};
};

enum class PostPassTarget {
    Scene,
    Hud,
    Both,
};

using PostPassHandle = std::uint64_t;

struct PostPass {
    PostPassHandle handle = 0;
    Shader shader{};
    std::string fragPath;
    bool enabled = true;

    std::unordered_map<std::string, PostUniformValue> uniforms;
    std::unordered_map<std::string, int> locationCache;
    int resolutionLoc = -1;
    int timeLoc = -1;
};

struct PostProcessState {
    RenderTexture2D scene{};
    int sceneW = 0;
    int sceneH = 0;

    RenderTexture2D hud{};
    int hudW = 0;
    int hudH = 0;

    RenderTexture2D sceneScratch[2]{};
    int sceneScratchW = 0;
    int sceneScratchH = 0;

    RenderTexture2D hudScratch[2]{};
    int hudScratchW = 0;
    int hudScratchH = 0;

    RenderTexture2D bothScratch[2]{};
    int bothScratchW = 0;
    int bothScratchH = 0;

    std::vector<PostPass> scenePasses;
    std::vector<PostPass> hudPasses;
    std::vector<PostPass> bothPasses;

    PostPassHandle nextHandle = 1;

    PostProcessState() = default;
    PostProcessState(const PostProcessState&) = delete;
    PostProcessState& operator=(const PostProcessState&) = delete;

    PostProcessState(PostProcessState&& other) noexcept;
    PostProcessState& operator=(PostProcessState&& other) noexcept;
    ~PostProcessState();

    void unload();
};

PostProcessState& ensurePostProcessState(flecs::world& world);

bool ensurePostProcessScene(PostProcessState& state, int width, int height);
bool ensurePostProcessHud(PostProcessState& state, int width, int height);

// True when the Hud or Both stacks have at least one active pass, meaning the
// full capture-composite pipeline is needed instead of the scene-only fast path.
bool postProcessNeedsCompositePipeline(const PostProcessState& state);

// Pushes a new shader pass onto the given target's stack. Returns a handle
// (0 on failure) used to address the pass with the functions below.
PostPassHandle pushPostShader(
    PostProcessState& state,
    AssetStore& assets,
    PostPassTarget target,
    std::string_view fragPath);

bool removePostShader(PostProcessState& state, PostPassHandle handle);
bool clearPostShaders(PostProcessState& state, PostPassTarget target);

bool setPostPassEnabled(PostProcessState& state, PostPassHandle handle, bool enabled);
bool setPostPassUniform(
    PostProcessState& state,
    PostPassHandle handle,
    std::string_view name,
    const PostUniformValue& value);

// Fast path used when only the Scene stack is active: runs it straight onto
// the backbuffer (or passes the scene through unshaded if the stack is empty).
void presentPostProcessSceneOnly(PostProcessState& state);

// Full path used when the Hud or Both stacks are active. Caller renders HUD
// content between beginHudCapture()/endHudCapture(), then calls
// presentPostProcessComposite() to run Scene -> Hud -> composite -> Both -> backbuffer.
void beginHudCapture(PostProcessState& state);
void endHudCapture(PostProcessState& state);
void presentPostProcessComposite(PostProcessState& state);

}
