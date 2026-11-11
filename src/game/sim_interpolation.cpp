#include "game/sim_interpolation.hpp"

#include "camera/camera_math.hpp"
#include "camera/components.hpp"
#include "core/sim_clock.hpp"
#include "physics/components.hpp"
#include "physics/motored_body.hpp"
#include "physics/rigid_mover.hpp"
#include "render/transform.hpp"

#include <raymath.h>

namespace slopengine {

namespace {

bool sameVec2(Vector2 a, Vector2 b) {
    return a.x == b.x && a.y == b.y;
}

bool sameVec3(Vector3 a, Vector3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool sameVec4(Vector4 a, Vector4 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

bool jumped(Vector3 from, Vector3 to) {
    return Vector3DistanceSqr(from, to) > kSimPoseSnapDistance * kSimPoseSnapDistance;
}

Vector4 eyeOffsetValue(const ViewEyeOffset& offset) {
    return {offset.x, offset.y, offset.z, offset.roll};
}

void writeEyeOffset(ViewEyeOffset& offset, Vector4 value) {
    offset.x = value.x;
    offset.y = value.y;
    offset.z = value.z;
    offset.roll = value.w;
}

Vector3 modelOffsetValue(const ViewModelOffset& offset) {
    return {offset.x, offset.y, offset.z};
}

void writeModelOffset(ViewModelOffset& offset, Vector3 value) {
    offset.x = value.x;
    offset.y = value.y;
    offset.z = value.z;
}

void addMissingPose(flecs::entity entity, const LocalTransformation& local) {
    if (!entity.has<SimPose>()) {
        entity.set<SimPose>({local, local, local});
    }
}

flecs::entity firstPersonEye(const flecs::world& world) {
    const flecs::entity player = world.lookup("Player");
    if (!player.is_valid() || !player.has<Lens>() || !player.has<CharacterMotor>() ||
        !player.has<FirstPersonController>()) {
        return flecs::entity{};
    }
    return player;
}

void restorePoses(flecs::world& world) {
    world.each([](flecs::entity entity, SimPose& pose, LocalTransformation& local, GlobalTransformation& global) {
        const bool spins = entity.has<Spin>();
        if (!sameSimPose(local, pose.presented, !spins)) {
            pose.cur = local;
        }
        local.position = pose.cur.position;
        local.scale = pose.cur.scale;
        if (!spins) {
            local.rotation = pose.cur.rotation;
        }
        pose.prev = pose.cur;
        pose.presented = local;
        if (!entity.parent().is_valid()) {
            updateTransform(entity, local, global);
        }
    });
}

void restoreEye(flecs::world& world) {
    const flecs::entity player = firstPersonEye(world);
    if (!player.is_valid() || !player.has<SimEyePose>()) {
        return;
    }
    SimEyePose& eye = player.get_mut<SimEyePose>();
    Lens& lens = player.get_mut<Lens>();
    if (!sameVec3(lens.camera.position, eye.presented)) {
        eye.cur = lens.camera.position;
    }
    const Vector3 look = Vector3Subtract(lens.camera.target, lens.camera.position);
    lens.camera.position = eye.cur;
    lens.camera.target = Vector3Add(eye.cur, look);
    eye.prev = eye.cur;
    eye.presented = eye.cur;
}

void restoreViewOffsets(flecs::world& world) {
    const flecs::entity player = world.lookup("Player");
    if (!player.is_valid() || !player.has<SimViewOffsets>()) {
        return;
    }
    SimViewOffsets& view = player.get_mut<SimViewOffsets>();
    if (ViewEyeOffset* eye = player.try_get_mut<ViewEyeOffset>()) {
        if (!sameVec4(eyeOffsetValue(*eye), view.presentedEye)) {
            view.curEye = eyeOffsetValue(*eye);
        }
        writeEyeOffset(*eye, view.curEye);
    }
    if (ViewModelOffset* model = player.try_get_mut<ViewModelOffset>()) {
        if (!sameVec3(modelOffsetValue(*model), view.presentedModel)) {
            view.curModel = modelOffsetValue(*model);
        }
        writeModelOffset(*model, view.curModel);
    }
    view.prevEye = view.curEye;
    view.prevModel = view.curModel;
    view.presentedEye = view.curEye;
    view.presentedModel = view.curModel;
}

void restoreSpriteOffsets(flecs::world& world) {
    world.each([](SimSpriteOffset& offset, ViewSprite& sprite) {
        const Vector2 current{sprite.offsetX, sprite.offsetY};
        if (!sameVec2(current, offset.presented)) {
            offset.cur = current;
        }
        sprite.offsetX = offset.cur.x;
        sprite.offsetY = offset.cur.y;
        offset.prev = offset.cur;
        offset.presented = offset.cur;
    });
}

void capturePoses(flecs::world& world) {
    world.each([](SimPose& pose, const LocalTransformation& local) {
        pose.cur = local;
        if (jumped(pose.prev.position, pose.cur.position)) {
            pose.prev = pose.cur;
        }
        pose.presented = local;
    });
    world.each([](flecs::entity entity, Actor, const LocalTransformation& local) {
        addMissingPose(entity, local);
    });
    world.each([](flecs::entity entity, MotoredBody&, const LocalTransformation& local) {
        addMissingPose(entity, local);
    });
    world.each([](flecs::entity entity, RigidMover&, const LocalTransformation& local) {
        addMissingPose(entity, local);
    });
    world.each([](flecs::entity entity, DebrisBody&, const LocalTransformation& local) {
        addMissingPose(entity, local);
    });
}

void captureEye(flecs::world& world) {
    const flecs::entity player = firstPersonEye(world);
    if (!player.is_valid()) {
        return;
    }
    const Vector3 position = player.get<Lens>().camera.position;
    if (SimEyePose* eye = player.try_get_mut<SimEyePose>()) {
        eye->cur = position;
        if (jumped(eye->prev, eye->cur)) {
            eye->prev = eye->cur;
        }
        eye->presented = position;
    } else {
        player.set<SimEyePose>({position, position, position});
    }
}

void captureViewOffsets(flecs::world& world) {
    const flecs::entity player = world.lookup("Player");
    if (!player.is_valid() || (!player.has<ViewEyeOffset>() && !player.has<ViewModelOffset>())) {
        return;
    }
    const Vector4 eye = player.has<ViewEyeOffset>() ? eyeOffsetValue(player.get<ViewEyeOffset>()) : Vector4{};
    const Vector3 model =
        player.has<ViewModelOffset>() ? modelOffsetValue(player.get<ViewModelOffset>()) : Vector3{};
    if (SimViewOffsets* view = player.try_get_mut<SimViewOffsets>()) {
        view->curEye = eye;
        view->curModel = model;
        view->presentedEye = eye;
        view->presentedModel = model;
    } else {
        player.set<SimViewOffsets>({eye, eye, eye, model, model, model});
    }
}

void captureSpriteOffsets(flecs::world& world) {
    world.each([](flecs::entity entity, const ViewSprite& sprite) {
        const Vector2 current{sprite.offsetX, sprite.offsetY};
        if (SimSpriteOffset* offset = entity.try_get_mut<SimSpriteOffset>()) {
            offset->cur = current;
            offset->presented = current;
        } else {
            entity.set<SimSpriteOffset>({current, current, current});
        }
    });
}

void blendPoses(flecs::world& world, float alpha) {
    world.each([alpha](flecs::entity entity, SimPose& pose, LocalTransformation& local) {
        const LocalTransformation blended = blendSimPose(pose, alpha);
        local.position = blended.position;
        local.scale = blended.scale;
        if (!entity.has<Spin>()) {
            local.rotation = blended.rotation;
        }
        pose.presented = local;
    });
}

void blendEye(flecs::world& world, float alpha) {
    const flecs::entity player = firstPersonEye(world);
    if (!player.is_valid() || !player.has<SimEyePose>()) {
        return;
    }
    SimEyePose& eye = player.get_mut<SimEyePose>();
    const FirstPersonController& controller = player.get<FirstPersonController>();
    Lens& lens = player.get_mut<Lens>();
    lens.camera.position = Vector3Lerp(eye.prev, eye.cur, eye.ownAlpha >= 0.0f ? eye.ownAlpha : alpha);
    lens.camera.target = Vector3Add(
        lens.camera.position, forwardFromYawPitch(controller.yaw, controller.pitch));
    lens.camera.up = {0.0f, 1.0f, 0.0f};
    eye.presented = lens.camera.position;
}

void blendViewOffsets(flecs::world& world, float alpha) {
    const flecs::entity player = world.lookup("Player");
    if (!player.is_valid() || !player.has<SimViewOffsets>()) {
        return;
    }
    SimViewOffsets& view = player.get_mut<SimViewOffsets>();
    if (ViewEyeOffset* eye = player.try_get_mut<ViewEyeOffset>()) {
        view.presentedEye = Vector4Lerp(view.prevEye, view.curEye, alpha);
        writeEyeOffset(*eye, view.presentedEye);
    }
    if (ViewModelOffset* model = player.try_get_mut<ViewModelOffset>()) {
        view.presentedModel = Vector3Lerp(view.prevModel, view.curModel, alpha);
        writeModelOffset(*model, view.presentedModel);
    }
}

void blendSpriteOffsets(flecs::world& world, float alpha) {
    world.each([alpha](SimSpriteOffset& offset, ViewSprite& sprite) {
        offset.presented = Vector2Lerp(offset.prev, offset.cur, alpha);
        sprite.offsetX = offset.presented.x;
        sprite.offsetY = offset.presented.y;
    });
}

} // namespace

LocalTransformation blendSimPose(const SimPose& pose, float alpha) {
    LocalTransformation out = pose.cur;
    out.position = Vector3Lerp(pose.prev.position, pose.cur.position, alpha);
    out.rotation = QuaternionSlerp(pose.prev.rotation, pose.cur.rotation, alpha);
    out.scale = Vector3Lerp(pose.prev.scale, pose.cur.scale, alpha);
    return out;
}

bool sameSimPose(const LocalTransformation& a, const LocalTransformation& b, bool compareRotation) {
    if (!sameVec3(a.position, b.position) || !sameVec3(a.scale, b.scale)) {
        return false;
    }
    return !compareRotation || (a.rotation.x == b.rotation.x && a.rotation.y == b.rotation.y &&
                                a.rotation.z == b.rotation.z && a.rotation.w == b.rotation.w);
}

void presentSimEye(flecs::world& world) {
    if (world.has<SimClock>()) {
        blendEye(world, simBlendAlpha(world.get<SimClock>()));
    }
}

void registerSimInterpolation(flecs::world& world) {
    world.component<SimPose>();
    world.component<SimEyePose>();
    world.component<SimViewOffsets>();
    world.component<SimSpriteOffset>();

    world.system("RestoreSimPoses")
        .kind(flecs::PreUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            restorePoses(world);
            restoreEye(world);
            restoreViewOffsets(world);
            restoreSpriteOffsets(world);
        }).add<SimSystem>();

    world.system("CaptureSimPoses")
        .kind(flecs::PostUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            capturePoses(world);
            captureEye(world);
            captureViewOffsets(world);
            captureSpriteOffsets(world);
        }).add<SimSystem>();

    world.system("InterpolateSimPoses")
        .kind(flecs::OnUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            const float alpha = simBlendAlpha(world.get<SimClock>());
            blendPoses(world, alpha);
            blendEye(world, alpha);
            blendViewOffsets(world, alpha);
            blendSpriteOffsets(world, alpha);
        });
}

}
