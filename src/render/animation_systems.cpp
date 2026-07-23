#include "render/animation_systems.hpp"

#include "assets/anim_loader.hpp"
#include "assets/asset_services.hpp"
#include "assets/skeleton_loader.hpp"
#include "game/game_state.hpp"
#include "render/animation_player.hpp"
#include "render/components.hpp"
#include "render/transform.hpp"
#include "script/scheme_call.hpp"
#include "script/script_context.hpp"
#include "script/script_scope.hpp"

#include <cmath>
#include <string>

#include <raylib.h>
#include <raymath.h>

namespace slopengine {

void registerSpinSystem(flecs::world& world) {
    world.system<LocalTransformation, Spin>("ApplySpin")
        .kind(flecs::OnUpdate)
        .each([](LocalTransformation& local, Spin& spin) {
            const Vector3 axis = Vector3Normalize(spin.axis);
            const float angle = spin.speed * GetFrameTime();
            const Quaternion delta = QuaternionFromAxisAngle(axis, angle);
            local.rotation = QuaternionMultiply(local.rotation, delta);
        });
}

void registerSchemeTickSystem(flecs::world& world) {
    world.system("CallSchemeTick")
        .kind(flecs::OnUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            if (!isPlaying(world)) {
                return;
            }
            if (!world.has<ScriptContext>() || world.get<ScriptContext>().scheme == nullptr) {
                return;
            }
            tryCallSchemeProc1Real(
                world.get<ScriptContext>().scheme,
                "tick",
                static_cast<double>(GetFrameTime()),
                ScriptScope::World);
        });
}

void registerAnimationSystems(flecs::world& world) {
    world.system<Model3D, AnimationPlayer>("AdvanceAnimationPlayer")
        .kind(flecs::OnUpdate)
        .each([](flecs::iter& it, size_t, Model3D& model3d, AnimationPlayer& player) {
            const bool startedThisFrame = player.justStarted;
            player.justStarted = false;
            player.justFinished = false;

            if (!player.playing || player.clipName.empty() || player.animBankPath.empty()) {
                return;
            }

            AssetServices& services = it.world().get_mut<AssetServices>();
            if (services.store == nullptr) {
                return;
            }

            const AnimBank* bank = services.store->getAnimBank(player.animBankPath);
            if (bank == nullptr) {
                return;
            }

            const auto clipIndex = bank->clipIndexByName.find(player.clipName);
            if (clipIndex == bank->clipIndexByName.end()) {
                return;
            }

            const std::size_t index = clipIndex->second;
            if (index >= bank->clips.size() || index >= bank->clipMeta.size()) {
                return;
            }

            const AnimClip& clipMeta = bank->clipMeta[index];
            const ModelAnimation& clip = bank->clips[index];
            if (clip.keyframeCount <= 0 || clipMeta.fps <= 0.0f) {
                return;
            }

            float clipDuration = clipMeta.duration;
            if (clipDuration <= 0.0f) {
                const int lastFrameIndex = clip.keyframeCount - 1;
                clipDuration =
                    static_cast<float>(lastFrameIndex > 0 ? lastFrameIndex : 1) / clipMeta.fps;
            }

            std::string skeletonPath = bank->skeletonId;
            if (skeletonPath.empty() && !player.animBankPath.empty()) {
                const auto slash = player.animBankPath.find_last_of('/');
                skeletonPath = slash == std::string::npos ? player.animBankPath
                                                          : player.animBankPath.substr(0, slash);
            }
            const std::vector<Matrix>* bindMatrices = services.store->getSkeletonBindMatrices(skeletonPath);
            const std::vector<std::vector<Matrix>>* matrixKeyframes = nullptr;
            if (index < bank->matrixClips.size() && !bank->matrixClips[index].keyframes.empty()) {
                matrixKeyframes = &bank->matrixClips[index].keyframes;
            }

            if (startedThisFrame && model3d.model.boneMatrices != nullptr) {
                updateRiggedModelAnimation(
                    model3d.model,
                    clip,
                    0.0f,
                    bindMatrices,
                    matrixKeyframes);
            }

            player.time += GetFrameTime() * player.speed;
            if (clipDuration > 0.0f && player.time >= clipDuration) {
                if (player.loop) {
                    player.time = std::fmod(player.time, clipDuration);
                } else {
                    player.time = clipDuration;
                    player.playing = false;
                    player.justFinished = true;
                }
            }

            const float frame = player.time * clipMeta.fps;
            updateRiggedModelAnimation(
                model3d.model,
                clip,
                frame,
                bindMatrices,
                matrixKeyframes);
        });
}

void registerAnimationClipFlipTestSystem(flecs::world& world) {
    world.system<AnimationPlayer, AnimationClipFlipTest>("AnimationClipFlipTest")
        .kind(flecs::PostUpdate)
        .each([](AnimationPlayer& player, AnimationClipFlipTest& controller) {
            if (!player.justFinished) {
                return;
            }

            if (player.clipName == controller.clipA) {
                player.play(controller.clipB, false);
            } else {
                player.play(controller.clipA, false);
            }
        });
}

void registerTransformSystems(flecs::world& world) {
    world.system("UpdateGlobalTransforms")
        .kind(flecs::OnUpdate)
        .run([](flecs::iter& it) {
            it.world().each([](flecs::entity entity) {
                if (entity.has(flecs::ChildOf)) {
                    return;
                }
                if (!entity.has<LocalTransformation>() || !entity.has<GlobalTransformation>()) {
                    return;
                }
                updateTransform(
                    entity,
                    entity.get_mut<LocalTransformation>(),
                    entity.get_mut<GlobalTransformation>());
            });
        });
}

}
