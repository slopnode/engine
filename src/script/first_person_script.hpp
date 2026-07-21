#pragma once

#include "camera/components.hpp"
#include "render/components.hpp"

#include <flecs.h>
#include <raylib.h>
#include <raymath.h>

struct s7_scheme;

namespace slopengine {

class AssetStore;

/** Compiled viewmodel shader used when FP shading is enabled. */
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

/** Binds fp-* Scheme primitives for first-person presentation. */
void bindFirstPersonApi(flecs::world& world, s7_scheme* scheme);

/** Ensures Player has a ViewSpace stage with weapon / emission sockets. */
void ensureFirstPersonScene(flecs::world& world, flecs::entity player);

/** Calls (prepare-first-person) when defined in Scheme. */
void callPrepareFirstPerson(flecs::world& world);

/** Updates Local/Global transforms under the FP stage. */
void updateFirstPersonSceneTransforms(flecs::world world);

/** Loads package default/viewmodel_* into a FirstPersonViewShader. */
FirstPersonViewShader createFirstPersonViewShader(AssetStore& assets);

/** View-space to world matrix from the player Lens. */
Matrix viewToWorldMatrix(const Lens& lens);
Vector3 viewToWorldPoint(const Lens& lens, Vector3 viewPoint);
Vector3 viewToWorldDirection(const Lens& lens, Vector3 viewDirection);

/** Raw Lens + view-space offset → camera used for world draw (not aim). */
Camera3D presentationCamera(
    const Lens& lens,
    const FirstPersonController& controller,
    const ViewEyeOffset& offset);

}
