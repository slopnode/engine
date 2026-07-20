#pragma once

#include "render/components.hpp"

#include <flecs.h>
#include <raylib.h>
#include <raymath.h>

struct s7_scheme;

namespace slopengine {

class AssetStore;

struct FirstPersonViewShader {
    Shader shader{};
    int matModelLoc = -1;
    int probeRgbLoc = -1;
    int ambientLoc = -1;
    int keyDirLoc = -1;
    int keyStrengthLoc = -1;
    int rimStrengthLoc = -1;

    FirstPersonViewShader() = default;
    FirstPersonViewShader(const FirstPersonViewShader&) = delete;
    FirstPersonViewShader& operator=(const FirstPersonViewShader&) = delete;

    FirstPersonViewShader(FirstPersonViewShader&& other) noexcept
        : shader(other.shader)
        , matModelLoc(other.matModelLoc)
        , probeRgbLoc(other.probeRgbLoc)
        , ambientLoc(other.ambientLoc)
        , keyDirLoc(other.keyDirLoc)
        , keyStrengthLoc(other.keyStrengthLoc)
        , rimStrengthLoc(other.rimStrengthLoc) {
        other.shader = {};
    }

    FirstPersonViewShader& operator=(FirstPersonViewShader&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        unload();
        shader = other.shader;
        matModelLoc = other.matModelLoc;
        probeRgbLoc = other.probeRgbLoc;
        ambientLoc = other.ambientLoc;
        keyDirLoc = other.keyDirLoc;
        keyStrengthLoc = other.keyStrengthLoc;
        rimStrengthLoc = other.rimStrengthLoc;
        other.shader = {};
        return *this;
    }

    ~FirstPersonViewShader() {
        unload();
    }

    void unload() {
        if (shader.id != 0) {
            UnloadShader(shader);
            shader = {};
        }
    }

    bool valid() const {
        return shader.id != 0;
    }
};

void bindFirstPersonApi(flecs::world& world, s7_scheme* scheme);
void ensureFirstPersonScene(flecs::world& world, flecs::entity player);
void callPrepareFirstPerson(flecs::world& world);
void updateFirstPersonSceneTransforms(flecs::world world);
FirstPersonViewShader createFirstPersonViewShader(AssetStore& assets);

Matrix viewToWorldMatrix(const Lens& lens);
Vector3 viewToWorldPoint(const Lens& lens, Vector3 viewPoint);
Vector3 viewToWorldDirection(const Lens& lens, Vector3 viewDirection);

}
