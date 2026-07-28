#include "particle_preview.hpp"

#include <raymath.h>

#include <unordered_set>

namespace slopmap {

namespace {

Matrix thingWorldMatrix(const slopengine::Thing& thing) {
    const Vector3 pos = thing.haveAt ? thing.at : Vector3{0.0f, 0.0f, 0.0f};
    const Quaternion rot = QuaternionFromAxisAngle({0.0f, 1.0f, 0.0f}, thing.yaw);
    const Matrix s = MatrixScale(1.0f, 1.0f, 1.0f);
    const Matrix r = QuaternionToMatrix(rot);
    const Matrix t = MatrixTranslate(pos.x, pos.y, pos.z);
    return MatrixMultiply(t, MatrixMultiply(r, s));
}

} // namespace

void syncParticlePreview(
    ParticlePreviewState& state,
    slopengine::AssetStore& assets,
    const std::vector<slopengine::Thing>& things,
    bool restartAll) {
    if (!state.enabled) {
        state.byThingId.clear();
        return;
    }

    std::unordered_set<std::string> live;
    for (const slopengine::Thing& thing : things) {
        if (thing.kind != slopengine::ThingKind::Particle || thing.id.empty() ||
            thing.particleSystem.empty()) {
            continue;
        }
        live.insert(thing.id);
        auto it = state.byThingId.find(thing.id);
        const bool needsInit = it == state.byThingId.end() ||
            it->second.path != thing.particleSystem;
        if (needsInit) {
            slopengine::ParticleSystemInstance instance{};
            if (!slopengine::initParticleSystemInstance(
                    instance, assets, thing.particleSystem, thing.particlePlay)) {
                state.byThingId.erase(thing.id);
                continue;
            }
            state.byThingId[thing.id] = std::move(instance);
            continue;
        }
        if (restartAll && thing.particlePlay) {
            slopengine::resetParticleSystemInstance(it->second);
            it->second.playing = true;
            continue;
        }
        if (thing.particlePlay && !it->second.playing) {
            slopengine::resetParticleSystemInstance(it->second);
            it->second.playing = true;
        } else if (!thing.particlePlay && it->second.playing) {
            slopengine::resetParticleSystemInstance(it->second);
            it->second.playing = false;
        }
    }

    for (auto it = state.byThingId.begin(); it != state.byThingId.end();) {
        if (live.count(it->first) == 0) {
            it = state.byThingId.erase(it);
        } else {
            ++it;
        }
    }
}

void tickParticlePreview(
    ParticlePreviewState& state,
    slopengine::AssetStore& assets,
    const std::vector<slopengine::Thing>& things,
    float dt) {
    if (!state.enabled || dt <= 0.0f) {
        return;
    }
    for (const slopengine::Thing& thing : things) {
        if (thing.kind != slopengine::ThingKind::Particle || thing.id.empty()) {
            continue;
        }
        auto it = state.byThingId.find(thing.id);
        if (it == state.byThingId.end()) {
            continue;
        }
        slopengine::tickParticleSystemInstance(
            it->second, assets, thingWorldMatrix(thing), dt);
    }
}

void drawParticlePreview(
    ParticlePreviewState& state,
    slopengine::AssetStore& assets,
    const std::vector<slopengine::Thing>& things,
    const Camera3D& camera) {
    if (!state.enabled) {
        return;
    }

    std::vector<slopengine::ParticleDrawItem> items;
    items.reserve(256);
    for (const slopengine::Thing& thing : things) {
        if (thing.kind != slopengine::ThingKind::Particle || thing.id.empty()) {
            continue;
        }
        auto it = state.byThingId.find(thing.id);
        if (it == state.byThingId.end()) {
            continue;
        }
        slopengine::appendParticleDrawItems(it->second, assets, camera, items);
    }
    slopengine::drawParticleDrawItems(items, camera);
}

}
