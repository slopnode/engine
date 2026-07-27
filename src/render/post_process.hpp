#pragma once

#include "assets/asset_store.hpp"

#include <flecs.h>
#include <raylib.h>

#include <string>
#include <string_view>
#include <unordered_map>

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

struct PostProcessState {
    RenderTexture2D scene{};
    int sceneW = 0;
    int sceneH = 0;

    Shader shader{};
    std::string fragPath;
    bool enabled = false;

    std::unordered_map<std::string, PostUniformValue> uniforms;
    std::unordered_map<std::string, int> locationCache;
    int resolutionLoc = -1;
    int timeLoc = -1;

    PostProcessState() = default;
    PostProcessState(const PostProcessState&) = delete;
    PostProcessState& operator=(const PostProcessState&) = delete;

    PostProcessState(PostProcessState&& other) noexcept;
    PostProcessState& operator=(PostProcessState&& other) noexcept;
    ~PostProcessState();

    void unloadScene();
    void unloadShader();
    void unload();
};

PostProcessState& ensurePostProcessState(flecs::world& world);

bool ensurePostProcessScene(PostProcessState& state, int width, int height);

bool loadPostProcessShader(PostProcessState& state, AssetStore& assets, std::string_view fragPath);

void clearPostProcessShader(PostProcessState& state);

void presentPostProcess(PostProcessState& state);

}
