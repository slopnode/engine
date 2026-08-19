#include "render/underwater_effect.hpp"

#include "assets/asset_services.hpp"
#include "audio/audio_module.hpp"
#include "map/water_volumes.hpp"
#include "render/components.hpp"
#include "render/post_process.hpp"
#include "render/render_context.hpp"

#include <raylib.h>

#include <algorithm>

namespace slopengine {

namespace {

// Vertical distance above a volume's top surface over which the effect eases
// in, so crossing the waterline fades instead of popping.
constexpr float kFadeBand = 0.3f;

struct UnderwaterEffectState {
    PostPassHandle passHandle = 0;
};

const WaterVolume* findWaterVolume(const MapWaterVolumes& volumes, Vector3 eye, float& outAmount) {
    outAmount = 0.0f;
    const WaterVolume* active = nullptr;
    for (const WaterVolume& volume : volumes.volumes) {
        if (eye.x < volume.mins.x || eye.x > volume.maxs.x) {
            continue;
        }
        if (eye.z < volume.mins.z || eye.z > volume.maxs.z) {
            continue;
        }
        if (eye.y < volume.mins.y - kFadeBand || eye.y > volume.maxs.y + kFadeBand) {
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
            if (!world.has<PlayerEntity>() || !world.has<MapWaterVolumes>()) {
                return;
            }
            const flecs::entity player = world.get<PlayerEntity>().entity;
            if (!player.is_valid() || !player.has<Lens>()) {
                return;
            }
            const Vector3 eye = player.get<Lens>().camera.position;

            float amount = 0.0f;
            const WaterVolume* volume = findWaterVolume(world.get<MapWaterVolumes>(), eye, amount);

            if (world.has<AudioContext>()) {
                const AudioContext& audioCtx = world.get<AudioContext>();
                if (audioCtx.world != nullptr) {
                    audioCtx.world->setUnderwaterMuffle(amount);
                }
            }

            UnderwaterEffectState& state = world.get_mut<UnderwaterEffectState>();

            if (volume == nullptr) {
                if (state.passHandle != 0) {
                    setPostPassEnabled(ensurePostProcessState(world), state.passHandle, false);
                }
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

            setPostPassEnabled(post, state.passHandle, true);
            setVec3Uniform(post, state.passHandle, "tint", volume->water.tint);
            setFloatUniform(post, state.passHandle, "wobble", volume->water.wobble);
            setFloatUniform(post, state.passHandle, "vignette", volume->water.vignette);
            setFloatUniform(post, state.passHandle, "amount", amount);
        });
}

}
