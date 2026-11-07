#include "render/animation_systems.hpp"

#include "assets/anim_loader.hpp"
#include "assets/asset_services.hpp"
#include "assets/skeleton_loader.hpp"
#include "core/frame_counters.hpp"
#include "core/sim_clock.hpp"
#include "game/game_state.hpp"
#include "render/animation_player.hpp"
#include "render/attachment_path.hpp"
#include "render/bone_attachment.hpp"
#include "render/components.hpp"
#include "render/gpu_skinning.hpp"
#include "render/model_hitboxes.hpp"
#include "render/transform.hpp"
#include "script/hook_registry.hpp"
#include "script/scheme_call.hpp"
#include "script/script_context.hpp"
#include "script/script_scope.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <raylib.h>
#include <raymath.h>

namespace slopengine {

void registerSpinSystem(flecs::world& world) {
    world.system<LocalTransformation, Spin>("ApplySpin")
        .kind(flecs::OnUpdate)
        .each([](flecs::iter& it, size_t, LocalTransformation& local, Spin& spin) {
            if (isSimulationPaused(it.world())) {
                return;
            }
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
            if (!isPlaying(world) || isSimulationPaused(world)) {
                return;
            }
            if (!world.has<ScriptContext>() || world.get<ScriptContext>().scheme == nullptr) {
                return;
            }
            callHook1Real(
                world.get<ScriptContext>().scheme,
                "tick",
                static_cast<double>(kSimTickDt),
                ScriptScope::World);
        }).add<SimSystem>();

    world.system("CallSchemePresent")
        .kind(flecs::OnUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            if (!isPlaying(world) || isSimulationPaused(world)) {
                return;
            }
            if (!world.has<ScriptContext>() || world.get<ScriptContext>().scheme == nullptr) {
                return;
            }
            callHook1Real(
                world.get<ScriptContext>().scheme,
                "present",
                static_cast<double>(it.delta_time()),
                ScriptScope::Present);
        });
}

namespace {

constexpr std::uint32_t kUnseenPoseInterval = 8;
constexpr std::uint32_t kFarPoseInterval = 2;
constexpr float kFarPoseDistance = 24.0f;

std::uint32_t poseUpdateInterval(flecs::entity entity, const Model3D& model3d) {
    if (!entity.has<WorldSpace>() || entity.has<ViewSpace>()) {
        return 1;
    }
    const std::uint32_t serial = renderFrameSerial();
    if (model3d.drawnFrame + 1 < serial) {
        return kUnseenPoseInterval;
    }
    return model3d.viewDistance > kFarPoseDistance ? kFarPoseInterval : 1;
}

void fireAnimEvents(
    flecs::world world,
    flecs::entity entity,
    const std::string& clipName,
    const std::vector<const AnimEvent*>& events) {
    if (!world.has<ScriptContext>() || world.get<ScriptContext>().scheme == nullptr) {
        return;
    }
    const std::string source = attachmentEventSource(entity);
    if (source.empty()) {
        static bool warnedAnonymous = false;
        if (!warnedAnonymous) {
            TraceLog(
                LOG_WARNING,
                "Animation event fired on unnamed entity (clip '%s'); (on-anim-event) skipped",
                clipName.c_str());
            warnedAnonymous = true;
        }
        return;
    }
    s7_scheme* scheme = world.get<ScriptContext>().scheme;
    for (const AnimEvent* event : events) {
        callHook3String(scheme, "on-anim-event", source, event->name, clipName, ScriptScope::World);
    }
}

const BoneScale* resolveBoneScales(BoneScales& scales, const Model& model) {
    const int boneCount = model.skeleton.boneCount;
    if (boneCount <= 0 || model.skeleton.bones == nullptr) {
        return nullptr;
    }
    if (scales.dirty || scales.resolvedFor != model.skeleton.bones ||
        scales.resolved.size() != static_cast<std::size_t>(boneCount)) {
        scales.resolved.assign(static_cast<std::size_t>(boneCount), BoneScale{});
        for (const auto& [name, scale] : scales.named) {
            const int boneIndex = findModelBoneIndex(model, name);
            if (boneIndex < 0) {
                TraceLog(LOG_WARNING, "RIG: bone scale names unknown bone '%s'", name.c_str());
                continue;
            }
            scales.resolved[static_cast<std::size_t>(boneIndex)] = scale;
        }
        scales.resolvedFor = model.skeleton.bones;
    }
    return scales.resolved.data();
}

struct ResolvedClip {
    const AnimClip* meta = nullptr;
    const ModelAnimation* clip = nullptr;
    const std::vector<Matrix>* bindMatrices = nullptr;
    const AnimClipMatrices* matrixClip = nullptr;
    float duration = 0.0f;
};

ResolvedClip resolveClip(flecs::world world, const AnimationPlayer& player) {
    ResolvedClip out{};
    if (player.clipName.empty() || player.animBankPath.empty()) {
        return out;
    }
    AssetServices& services = world.get_mut<AssetServices>();
    if (services.store == nullptr) {
        return out;
    }
    const AnimBank* bank = services.store->getAnimBank(player.animBankPath);
    if (bank == nullptr) {
        return out;
    }
    const auto clipIndex = bank->clipIndexByName.find(player.clipName);
    if (clipIndex == bank->clipIndexByName.end()) {
        return out;
    }
    const std::size_t index = clipIndex->second;
    if (index >= bank->clips.size() || index >= bank->clipMeta.size()) {
        return out;
    }
    const AnimClip& clipMeta = bank->clipMeta[index];
    const ModelAnimation& clip = bank->clips[index];
    if (clip.keyframeCount <= 0 || clipMeta.fps <= 0.0f) {
        return out;
    }

    float clipDuration = clipMeta.duration;
    if (clipDuration <= 0.0f) {
        const int lastFrameIndex = clip.keyframeCount - 1;
        clipDuration = static_cast<float>(lastFrameIndex > 0 ? lastFrameIndex : 1) / clipMeta.fps;
    }

    std::string skeletonPath = bank->skeletonId;
    if (skeletonPath.empty()) {
        const auto slash = player.animBankPath.find_last_of('/');
        skeletonPath = slash == std::string::npos ? player.animBankPath
                                                  : player.animBankPath.substr(0, slash);
    }
    out.meta = &clipMeta;
    out.clip = &clip;
    out.bindMatrices = services.store->getSkeletonBindMatrices(skeletonPath);
    out.matrixClip = index < bank->matrixClips.size() ? &bank->matrixClips[index] : nullptr;
    out.duration = clipDuration;
    return out;
}

} // namespace

void sampleTickPose(flecs::entity entity) {
    AnimationPlayer* player = entity.try_get_mut<AnimationPlayer>();
    Model3D* model3d = entity.try_get_mut<Model3D>();
    if (player == nullptr || model3d == nullptr || model3d->model.boneMatrices == nullptr) {
        return;
    }
    flecs::world world = entity.world();
    const std::uint64_t tick = world.has<SimClock>() ? world.get<SimClock>().tick : 0;
    if (tick != 0 && player->poseSampledTick == tick) {
        return;
    }
    const ResolvedClip resolved = resolveClip(world, *player);
    if (resolved.meta == nullptr) {
        return;
    }
    BoneScales* boneScales = entity.try_get_mut<BoneScales>();
    const HiddenBones* hiddenBones = entity.try_get<HiddenBones>();
    const BoneScale* scales =
        boneScales != nullptr ? resolveBoneScales(*boneScales, model3d->model) : nullptr;
    const std::uint8_t* hidden = hiddenBones != nullptr &&
            hiddenBones->mask.size() == static_cast<std::size_t>(model3d->model.skeleton.boneCount)
        ? hiddenBones->mask.data()
        : nullptr;
    updateRiggedModelAnimation(
        model3d->model,
        *resolved.clip,
        player->time * resolved.meta->fps,
        resolved.bindMatrices,
        resolved.matrixClip,
        !modelUsesGpuSkinning(model3d->model),
        scales,
        hidden);
    player->poseSampledTick = tick;
    player->poseStale = true;
}

namespace {

void advanceAnimationClock(flecs::world world, flecs::entity entity, AnimationPlayer& player, float dt) {
    player.justFinished = false;
    if (!player.playing) {
        return;
    }
    const ResolvedClip resolved = resolveClip(world, player);
    if (resolved.meta == nullptr) {
        return;
    }

    player.prevTime = player.time;
    player.time += dt * player.speed;
    player.wrappedLastTick = false;
    if (resolved.duration > 0.0f && player.time >= resolved.duration) {
        if (player.loop) {
            player.time = std::fmod(player.time, resolved.duration);
            player.wrappedLastTick = true;
        } else {
            player.time = resolved.duration;
            player.playing = false;
            player.justFinished = true;
        }
    }
    player.poseStale = true;

    const float frame = player.time * resolved.meta->fps;
    if (!resolved.meta->events.empty()) {
        std::vector<const AnimEvent*> fired;
        collectAnimEvents(*resolved.meta, player.lastFrame, frame, player.wrappedLastTick, fired);
        if (!fired.empty()) {
            fireAnimEvents(world, entity, resolved.meta->name, fired);
        }
    }
    player.lastFrame = frame;
}

}

void registerAnimationSystems(flecs::world& world) {
    world.system<Model3D, AnimationPlayer>("AdvanceAnimationClock")
        .kind(flecs::OnUpdate)
        .each([](flecs::iter& it, size_t index_, Model3D&, AnimationPlayer& player) {
            if (isSimulationPaused(it.world())) {
                return;
            }
            advanceAnimationClock(it.world(), it.entity(index_), player, kSimTickDt);
        }).add<SimSystem>();

    world.system<Model3D, AnimationPlayer>("AdvanceRemoteViewAnimationClock")
        .with<ViewSpace>()
        .kind(flecs::OnUpdate)
        .each([](flecs::iter& it, size_t index_, Model3D&, AnimationPlayer& player) {
            flecs::world world = it.world();
            if (!world.has<SimRemote>() || isSimulationPaused(world)) {
                return;
            }
            advanceAnimationClock(world, it.entity(index_), player, it.delta_time());
            player.prevTime = player.time;
            player.wrappedLastTick = false;
        });

    world.system<Model3D, AnimationPlayer>("SampleAnimationPose")
        .kind(flecs::OnUpdate)
        .each([](flecs::iter& it, size_t index_, Model3D& model3d, AnimationPlayer& player) {
            if (isSimulationPaused(it.world())) {
                return;
            }

            const bool startedThisFrame = player.justStarted;
            player.justStarted = false;

            flecs::entity entity = it.entity(index_);
            BoneScales* boneScales = entity.has<BoneScales>() ? &entity.get_mut<BoneScales>() : nullptr;
            HiddenBones* hiddenBones = entity.has<HiddenBones>() ? &entity.get_mut<HiddenBones>() : nullptr;
            const bool rescaled = (boneScales != nullptr && boneScales->dirty) ||
                (hiddenBones != nullptr && hiddenBones->dirty);
            if (!player.playing && !player.poseStale && !rescaled && !startedThisFrame) {
                return;
            }
            const ResolvedClip resolved = resolveClip(it.world(), player);
            if (resolved.meta == nullptr) {
                return;
            }
            const BoneScale* scales =
                boneScales != nullptr ? resolveBoneScales(*boneScales, model3d.model) : nullptr;
            const std::uint8_t* hidden = hiddenBones != nullptr &&
                    hiddenBones->mask.size() == static_cast<std::size_t>(model3d.model.skeleton.boneCount)
                ? hiddenBones->mask.data()
                : nullptr;

            if (startedThisFrame && model3d.model.boneMatrices != nullptr) {
                updateRiggedModelAnimation(
                    model3d.model,
                    *resolved.clip,
                    0.0f,
                    resolved.bindMatrices,
                    resolved.matrixClip,
                    !modelUsesGpuSkinning(model3d.model),
                    scales,
                    hidden);
            }

            const float alpha = simBlendAlpha(it.world().get<SimClock>());
            const float renderTime = player.playing
                ? blendAnimationTime(player.prevTime, player.time, resolved.duration, player.wrappedLastTick, alpha)
                : player.time;
            const float frame = renderTime * resolved.meta->fps;
            const std::uint32_t interval = poseUpdateInterval(entity, model3d);
            const bool poseDue = startedThisFrame || rescaled || interval <= 1 ||
                (renderFrameSerial() + static_cast<std::uint32_t>(entity.id())) % interval == 0;
            if (!poseDue) {
                return;
            }
            const PoseScaleResult scaleResult = updateRiggedModelAnimation(
                model3d.model,
                *resolved.clip,
                frame,
                resolved.bindMatrices,
                resolved.matrixClip,
                !modelUsesGpuSkinning(model3d.model),
                scales,
                hidden);
            model3d.boundsSpread = scaleResult.spread;
            model3d.boundsLift = scaleResult.lift;
            if (boneScales != nullptr) {
                boneScales->dirty = false;
            }
            if (hiddenBones != nullptr) {
                hiddenBones->dirty = false;
            }
            player.poseStale = false;
        });

    world.system<BoneAttachment, LocalTransformation>("ApplyBoneAttachments")
        .kind(flecs::OnUpdate)
        .each([](flecs::entity entity, BoneAttachment& attachment, LocalTransformation& local) {
            flecs::entity parent = entity.parent();
            if (!parent.is_valid() || !parent.has<Model3D>()) {
                return;
            }
            const Model& model = parent.get<Model3D>().model;
            if (model.currentPose == nullptr || model.skeleton.boneCount <= 0) {
                return;
            }
            if (attachment.boneIndex < 0 || attachment.boneIndex >= static_cast<int>(model.skeleton.boneCount)) {
                attachment.boneIndex = findModelBoneIndex(model, attachment.bone);
                if (attachment.boneIndex < 0) {
                    return;
                }
            }
            const Transform pose = composeBoneAttachment(model.currentPose[attachment.boneIndex], attachment);
            local.position = pose.translation;
            local.rotation = pose.rotation;
            local.scale = pose.scale;
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
        }).add<SimSystem>();
}

namespace {

void updateRootTransforms(flecs::iter& it) {
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
}

} // namespace

void registerTransformSystems(flecs::world& world) {
    world.system("UpdateGlobalTransformsSim")
        .kind(flecs::OnUpdate)
        .run(updateRootTransforms)
        .add<SimSystem>();
    world.system("UpdateGlobalTransforms")
        .kind(flecs::OnUpdate)
        .run(updateRootTransforms);
}

}
