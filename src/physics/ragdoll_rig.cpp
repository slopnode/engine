#include "physics/ragdoll_rig.hpp"

#include "assets/skeleton_loader.hpp"
#include "core/sim_clock.hpp"
#include "game/game_state.hpp"
#include "physics/physics_module.hpp"
#include "render/components.hpp"
#include "render/gpu_skinning.hpp"

#include <algorithm>
#include <utility>

namespace slopengine {

bool startEntityRagdoll(
    flecs::entity entity,
    const Model& model,
    const Matrix& world,
    PhysicsWorld& physics,
    const std::vector<BoneHitbox>& boxes,
    const std::vector<std::uint8_t>& physical,
    const std::vector<std::uint8_t>& follow,
    Vector3 velocity,
    Vector3 angularVelocity,
    int hitBone,
    Vector3 hitVelocity,
    float lifetime,
    int maxActive) {
    if (!entity.is_valid()) {
        return false;
    }
    auto build = buildRagdoll(
        model,
        boxes,
        world,
        physical,
        follow,
        velocity,
        angularVelocity,
        hitBone,
        hitVelocity);
    if (!build) {
        return false;
    }

    std::vector<std::pair<float, flecs::entity>> running;
    entity.world().each([&](flecs::entity other, RagdollRig& rig) {
        if (!rig.frozen && physics.hasRagdoll(static_cast<std::uint64_t>(other.id()))) {
            running.emplace_back(rig.age, other);
        }
    });
    std::sort(running.begin(), running.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    const std::size_t keep = static_cast<std::size_t>(std::max(maxActive - 1, 0));
    for (std::size_t i = 0; i + keep < running.size(); ++i) {
        physics.destroyRagdoll(static_cast<std::uint64_t>(running[i].second.id()));
        running[i].second.get_mut<RagdollRig>().frozen = true;
    }

    if (!physics.createRagdoll(static_cast<std::uint64_t>(entity.id()), build->parts)) {
        return false;
    }
    build->rig.lifetime = lifetime;
    entity.set<RagdollRig>(std::move(build->rig));
    return true;
}

void registerRagdollSystems(flecs::world& world) {
    world.component<RagdollRig>();
    world.system("RagdollSync")
        .kind(flecs::OnUpdate)
        .run([](flecs::iter& it) {
            flecs::world world = it.world();
            if (isSimulationPaused(world) || !world.has<PhysicsContext>()) {
                return;
            }
            PhysicsWorld* physics = world.get_mut<PhysicsContext>().world;
            if (physics == nullptr) {
                return;
            }
            const float dt = kSimTickDt;
            std::vector<Matrix> parts;
            world.each([&](flecs::entity entity,
                           RagdollRig& rig,
                           Model3D& model3d,
                           LocalTransformation& local,
                           GlobalTransformation& global) {
                if (rig.frozen) {
                    return;
                }
                const std::uint64_t id = static_cast<std::uint64_t>(entity.id());
                rig.age += dt;
                if (!physics->ragdollPartTransforms(id, parts)) {
                    rig.frozen = true;
                    return;
                }
                const std::uint8_t* hidden = nullptr;
                if (entity.has<HiddenBones>()) {
                    const HiddenBones& bones = entity.get<HiddenBones>();
                    if (bones.mask.size() == static_cast<std::size_t>(model3d.model.skeleton.boneCount)) {
                        hidden = bones.mask.data();
                    }
                }
                const Matrix entityWorld = applyRagdollPose(rig, parts, model3d.model, hidden);
                MatrixDecompose(entityWorld, &local.position, &local.rotation, &local.scale);
                global.matrix = entityWorld;
                if (modelNeedsCpuSkinning(model3d.model)) {
                    updateSkinnedMeshVertexBuffers(model3d.model);
                }
                if (rig.age >= rig.lifetime) {
                    physics->destroyRagdoll(id);
                    rig.frozen = true;
                }
            });
        }).add<SimSystem>().add<CosmeticSystem>();
}

}
