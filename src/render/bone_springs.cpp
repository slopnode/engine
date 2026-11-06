#include "render/bone_springs.hpp"

#include "game/game_state.hpp"
#include "render/bone_attachment.hpp"
#include "render/components.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <raymath.h>

namespace slopengine {

namespace {

Matrix poseMatrix(const Transform& pose) {
    return MatrixMultiply(
        MatrixMultiply(MatrixScale(pose.scale.x, pose.scale.y, pose.scale.z), QuaternionToMatrix(pose.rotation)),
        MatrixTranslate(pose.translation.x, pose.translation.y, pose.translation.z));
}

void resolveBones(BoneSprings& springs, const Model& model) {
    if (springs.resolvedFor == model.skeleton.bones && springs.states.size() == springs.specs.size()) {
        return;
    }
    springs.states.assign(springs.specs.size(), BoneSpringState{});
    for (std::size_t i = 0; i < springs.specs.size(); ++i) {
        springs.states[i].boneIndex = findModelBoneIndex(model, springs.specs[i].bone);
    }
    springs.resolvedFor = model.skeleton.bones;
}

ShaderParams& ensureShaderParams(flecs::entity entity) {
    if (!entity.has<ShaderParams>()) {
        entity.set<ShaderParams>({});
    }
    return entity.get_mut<ShaderParams>();
}

void writeParam(ShaderParams& params, const BoneSpringSpec& spec, BoneSpringState& state, Vector4 value) {
    if (state.paramIndex < 0 || static_cast<std::size_t>(state.paramIndex) >= params.values.size() ||
        params.values[static_cast<std::size_t>(state.paramIndex)].first != spec.param) {
        auto existing = std::find_if(params.values.begin(), params.values.end(), [&](const auto& entry) {
            return entry.first == spec.param;
        });
        if (existing == params.values.end()) {
            params.values.emplace_back(spec.param, value);
            state.paramIndex = static_cast<int>(params.values.size()) - 1;
            return;
        }
        state.paramIndex = static_cast<int>(existing - params.values.begin());
    }
    params.values[static_cast<std::size_t>(state.paramIndex)].second = value;
}

void clearSprings(flecs::entity entity, BoneSprings& springs) {
    ShaderParams& params = ensureShaderParams(entity);
    for (std::size_t i = 0; i < springs.specs.size() && i < springs.states.size(); ++i) {
        BoneSpringState& state = springs.states[i];
        writeParam(params, springs.specs[i], state, Vector4{0.0f, 0.0f, 0.0f, 0.0f});
        const int boneIndex = state.boneIndex;
        const int paramIndex = state.paramIndex;
        state = BoneSpringState{};
        state.boneIndex = boneIndex;
        state.paramIndex = paramIndex;
    }
    springs.cleared = true;
}

void stepSpring(const BoneSpringSpec& spec, const BoneSpringTuning& tuning, BoneSpringState& state, float dt) {
    const float k = spec.stiffness;
    const float c = spec.damping;
    const float g = spec.gain;
    Vector3 o = state.offset;
    Vector3 s = state.velocity;
    float t = dt;
    while (t > 1.0e-6f) {
        const float h = std::min(t, tuning.step);
        s.x += h * (-k * o.x - c * s.x - g * state.accel.x);
        s.y += h * (-k * o.y - c * s.y - g * state.accel.y);
        s.z += h * (-k * o.z - c * s.z - g * state.accel.z);
        o.x += s.x * h;
        o.y += s.y * h;
        o.z += s.z * h;
        const float len = std::sqrt(o.x * o.x + o.y * o.y + o.z * o.z);
        if (len > spec.limit && len > 0.0f) {
            const float scale = spec.limit / len;
            o.x *= scale;
            o.y *= scale;
            o.z *= scale;
        }
        t -= h;
    }
    state.offset = o;
    state.velocity = s;
}

void sampleBone(const BoneSpringTuning& tuning, BoneSpringState& state, Vector3 pos, float dt) {
    const float inv = 1.0f / dt;
    const float blend = std::min(1.0f, dt * tuning.accelSmoothing);
    const Vector3 vel{
        (pos.x - state.lastPos.x) * inv, (pos.y - state.lastPos.y) * inv, (pos.z - state.lastPos.z) * inv};
    state.frames += 1;
    if (state.frames > 2) {
        state.accel.x += blend * ((vel.x - state.lastVel.x) * inv - state.accel.x);
        state.accel.y += blend * ((vel.y - state.lastVel.y) * inv - state.accel.y);
        state.accel.z += blend * ((vel.z - state.lastVel.z) * inv - state.accel.z);
    }
    state.lastPos = pos;
    state.lastVel = vel;
}

}

void registerBoneSpringSystem(flecs::world& world) {
    world.system<Model3D, BoneSprings>("BoneSpringStep")
        .kind(flecs::OnUpdate)
        .each([](flecs::iter& it, size_t index, Model3D& model3d, BoneSprings& springs) {
            if (isSimulationPaused(it.world())) {
                return;
            }
            flecs::entity entity = it.entity(index);
            if (!springs.active) {
                if (!springs.cleared) {
                    clearSprings(entity, springs);
                }
                return;
            }
            const float frameDt = GetFrameTime();
            if (frameDt <= 0.0f || !entity.has<GlobalTransformation>() || !entity.has<LocalTransformation>()) {
                return;
            }
            const Model& model = model3d.model;
            if (model.skeleton.boneCount <= 0 || model.currentPose == nullptr) {
                return;
            }
            resolveBones(springs, model);
            const float dt = std::min(frameDt, springs.tuning.maxDt);
            const Matrix& global = entity.get<GlobalTransformation>().matrix;
            const float yaw = QuaternionToEuler(entity.get<LocalTransformation>().rotation).y;
            const float cy = std::cos(yaw);
            const float sy = std::sin(yaw);
            ShaderParams& params = ensureShaderParams(entity);
            springs.cleared = false;
            for (std::size_t i = 0; i < springs.specs.size(); ++i) {
                const BoneSpringSpec& spec = springs.specs[i];
                BoneSpringState& state = springs.states[i];
                if (state.boneIndex < 0 || state.boneIndex >= model.skeleton.boneCount) {
                    continue;
                }
                const Matrix joint = MatrixMultiply(poseMatrix(model.currentPose[state.boneIndex]), global);
                sampleBone(springs.tuning, state, Vector3{joint.m12, joint.m13, joint.m14}, dt);
                if (state.frames > 2) {
                    stepSpring(spec, springs.tuning, state, dt);
                }
                const Vector3 o = state.offset;
                const Vector3 s = state.velocity;
                writeParam(
                    params,
                    spec,
                    state,
                    Vector4{
                        o.x * cy - o.z * sy,
                        o.y,
                        o.x * sy + o.z * cy,
                        std::sqrt(s.x * s.x + s.y * s.y + s.z * s.z)});
                        }
        });
}

}
