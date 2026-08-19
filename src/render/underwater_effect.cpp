#include "render/underwater_effect.hpp"

#include "assets/asset_services.hpp"
#include "audio/audio_module.hpp"
#include "map/water_volumes.hpp"
#include "render/components.hpp"
#include "render/post_process.hpp"
#include "render/render_context.hpp"

#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <cmath>

namespace slopengine {

namespace {

// Vertical distance above a volume's top surface over which the effect eases
// in, so crossing the waterline fades instead of popping.
constexpr float kFadeBand = 0.3f;

// How quickly the displayed tint/wobble/vignette/amount catch up to the
// selected volume's target values (1/seconds). Smooths over any residual
// switch between adjacent/overlapping volumes instead of popping.
constexpr float kSmoothRate = 10.0f;

constexpr float kContainEpsilon = 0.02f;

struct UnderwaterEffectState {
    PostPassHandle passHandle = 0;
    bool initialized = false;
    Vector3 tint{};
    float wobble = 0.0f;
    float vignette = 0.0f;
    float amount = 0.0f;
};

// True when point is inside every non-top face's half-space — i.e. within
// the brush's real footprint and above its floor, ignoring the open top so
// the vertical fade band above the surface still applies.
bool insideWaterBoundary(Vector3 point, const std::vector<BrushFace>& faces, float epsilon) {
    if (faces.empty()) {
        return false;
    }
    for (const BrushFace& face : faces) {
        if (face.vertices.empty()) {
            return false;
        }
        const Vector3 toPoint = Vector3Subtract(point, face.vertices[0]);
        if (Vector3DotProduct(toPoint, face.normal) > epsilon) {
            return false;
        }
    }
    return true;
}

const WaterVolume* findWaterVolume(const MapWaterVolumes& volumes, Vector3 eye, float& outAmount) {
    outAmount = 0.0f;
    const WaterVolume* active = nullptr;
    for (const WaterVolume& volume : volumes.volumes) {
        // Cheap reject before the precise (and slightly more expensive) face test.
        if (eye.x < volume.mins.x - kFadeBand || eye.x > volume.maxs.x + kFadeBand) {
            continue;
        }
        if (eye.z < volume.mins.z - kFadeBand || eye.z > volume.maxs.z + kFadeBand) {
            continue;
        }
        if (eye.y < volume.mins.y - kFadeBand || eye.y > volume.maxs.y + kFadeBand) {
            continue;
        }
        if (!insideWaterBoundary(eye, volume.boundaryFaces, kContainEpsilon)) {
            continue;
        }
        const float t = std::clamp((volume.maxs.y + kFadeBand - eye.y) / kFadeBand, 0.0f, 1.0f);
        if (t > outAmount) {
            outAmount = t;
            active = &volume;
        }
    }
    return active;
}

void setFloatUniform(PostProcessState& post, PostPassHandle handle, std::string_view name, float x) {
    PostUniformValue value{};
    value.kind = PostUniformKind::Float;
    value.data[0] = x;
    setPostPassUniform(post, handle, name, value);
}

void setVec3Uniform(PostProcessState& post, PostPassHandle handle, std::string_view name, Vector3 v) {
    PostUniformValue value{};
    value.kind = PostUniformKind::Vec3;
    value.data[0] = v.x;
    value.data[1] = v.y;
    value.data[2] = v.z;
    setPostPassUniform(post, handle, name, value);
}

} // namespace

void registerUnderwaterEffectModule(flecs::world& world) {
    world.set<UnderwaterEffectState>({});

    world.system("UnderwaterEffect")
        .kind(flecs::OnUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            UnderwaterEffectState& state = world.get_mut<UnderwaterEffectState>();

            Vector3 targetTint = state.tint;
            float targetWobble = state.wobble;
            float targetVignette = state.vignette;
            float targetAmount = 0.0f;

            if (world.has<PlayerEntity>() && world.has<MapWaterVolumes>()) {
                const flecs::entity player = world.get<PlayerEntity>().entity;
                if (player.is_valid() && player.has<Lens>()) {
                    const Vector3 eye = player.get<Lens>().camera.position;
                    float amount = 0.0f;
                    if (const WaterVolume* volume =
                            findWaterVolume(world.get<MapWaterVolumes>(), eye, amount)) {
                        targetTint = volume->water.tint;
                        targetWobble = volume->water.wobble;
                        targetVignette = volume->water.vignette;
                        targetAmount = amount;
                    }
                }
            }

            // Smooth toward the target each frame — this absorbs any residual
            // switch between volumes (adjacent brushes, camera bob at the
            // surface) as a quick blend instead of an instant color pop.
            const float blend = state.initialized
                ? (1.0f - std::exp(-kSmoothRate * GetFrameTime()))
                : 1.0f;
            state.tint = Vector3Lerp(state.tint, targetTint, blend);
            state.wobble += (targetWobble - state.wobble) * blend;
            state.vignette += (targetVignette - state.vignette) * blend;
            state.amount += (targetAmount - state.amount) * blend;
            state.initialized = true;

            if (world.has<AudioContext>()) {
                const AudioContext& audioCtx = world.get<AudioContext>();
                if (audioCtx.world != nullptr) {
                    audioCtx.world->setUnderwaterMuffle(state.amount);
                }
            }

            constexpr float kInactiveThreshold = 0.001f;
            if (state.passHandle == 0 && state.amount < kInactiveThreshold) {
                return;
            }
            if (!world.has<AssetServices>() || world.get<AssetServices>().store == nullptr) {
                return;
            }

            PostProcessState& post = ensurePostProcessState(world);
            if (state.passHandle == 0) {
                state.passHandle = pushPostShader(
                    post,
                    *world.get<AssetServices>().store,
                    PostPassTarget::Scene,
                    "default/underwater_frag");
                if (state.passHandle == 0) {
                    return;
                }
            }

            setPostPassEnabled(post, state.passHandle, state.amount >= kInactiveThreshold);
            setVec3Uniform(post, state.passHandle, "tint", state.tint);
            setFloatUniform(post, state.passHandle, "wobble", state.wobble);
            setFloatUniform(post, state.passHandle, "vignette", state.vignette);
            setFloatUniform(post, state.passHandle, "amount", state.amount);
        });
}

}
