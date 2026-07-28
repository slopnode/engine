#pragma once

#include "assets/asset_store.hpp"
#include "particles/components.hpp"

#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include <raylib.h>

namespace slopengine {

struct ParticleRayHit {
    Vector3 point{};
    Vector3 normal{0.0f, 1.0f, 0.0f};
};

using ParticleRaycastFn =
    std::function<std::optional<ParticleRayHit>(Vector3 origin, Vector3 direction, float distance)>;

struct ParticleDrawItem {
    Vector3 position{};
    float size = 0.25f;
    Color color = WHITE;
    const Texture2D* texture = nullptr;
    Rectangle source{};
    float distSq = 0.0f;
    ParticleBlendMode blend = ParticleBlendMode::Alpha;
    bool unlit = false;
};

bool initParticleSystemInstance(
    ParticleSystemInstance& instance,
    AssetStore& assets,
    std::string_view path,
    bool playing = true);

void resetParticleSystemInstance(ParticleSystemInstance& instance);

void tickParticleSystemInstance(
    ParticleSystemInstance& instance,
    AssetStore& assets,
    const Matrix& worldMatrix,
    float dt,
    const ParticleRaycastFn& raycast = {});

void appendParticleDrawItems(
    const ParticleSystemInstance& instance,
    AssetStore& assets,
    const Camera3D& camera,
    std::vector<ParticleDrawItem>& out);

void drawParticleDrawItems(
    const std::vector<ParticleDrawItem>& items,
    const Camera3D& camera);

}
