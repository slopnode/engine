#include "test_assert.hpp"

#include "physics/components.hpp"
#include "physics/physics_world.hpp"
#include "physics/ragdoll_pose.hpp"

#include <cmath>
#include <cstring>
#include <vector>

#include <raymath.h>

namespace slopengine {

namespace {

struct RagdollTestRig {
    std::vector<float> vertices;
    std::vector<unsigned char> boneIndices;
    std::vector<float> boneWeights;
    std::vector<BoneInfo> bones;
    std::vector<Matrix> skin;
    std::vector<Transform> pose;
    Mesh mesh{};
    Model model{};

    void addCube(Vector3 center, Vector3 half, int bone) {
        for (int corner = 0; corner < 8; ++corner) {
            vertices.push_back(center.x + ((corner & 1) != 0 ? half.x : -half.x));
            vertices.push_back(center.y + ((corner & 2) != 0 ? half.y : -half.y));
            vertices.push_back(center.z + ((corner & 4) != 0 ? half.z : -half.z));
            boneIndices.insert(boneIndices.end(), {static_cast<unsigned char>(bone), 0, 0, 0});
            boneWeights.insert(boneWeights.end(), {1.0f, 0.0f, 0.0f, 0.0f});
        }
    }

    void addBone(const char* name, int parent, Vector3 joint) {
        BoneInfo info{};
        std::strncpy(info.name, name, sizeof(info.name) - 1);
        info.parent = parent;
        bones.push_back(info);
        skin.push_back(MatrixIdentity());
        pose.push_back(Transform{joint, QuaternionIdentity(), {1.0f, 1.0f, 1.0f}});
    }

    void finish() {
        mesh.vertexCount = static_cast<int>(vertices.size() / 3);
        mesh.vertices = vertices.data();
        mesh.boneIndices = boneIndices.data();
        mesh.boneWeights = boneWeights.data();
        model.meshCount = 1;
        model.meshes = &mesh;
        model.skeleton.boneCount = static_cast<int>(bones.size());
        model.skeleton.bones = bones.data();
        model.boneMatrices = skin.data();
        model.currentPose = pose.data();
    }
};

bool matricesNear(const Matrix& a, const Matrix& b, float eps = 1e-3f) {
    const float* pa = &a.m0;
    const float* pb = &b.m0;
    for (int i = 0; i < 16; ++i) {
        if (std::fabs(pa[i] - pb[i]) > eps) {
            return false;
        }
    }
    return true;
}

}

void runRagdollTests() {
    RagdollTestRig rig;
    rig.addBone("root", -1, {0.0f, 0.0f, 0.0f});
    rig.addBone("thigh.L", 0, {0.0f, 1.0f, 0.0f});
    rig.addBone("calf.L", 1, {0.0f, 0.5f, 0.0f});
    rig.addCube({0.0f, 0.75f, 0.0f}, {0.08f, 0.25f, 0.08f}, 1);
    rig.addCube({0.0f, 0.25f, 0.0f}, {0.06f, 0.25f, 0.06f}, 2);
    rig.finish();

    const std::vector<BoneHitbox> boxes = buildBoneHitboxes(rig.model);
    const std::vector<std::uint8_t> all(3, 1);
    const Matrix world = MatrixTranslate(2.0f, 0.0f, -3.0f);
    const auto build = buildRagdoll(
        rig.model, boxes, world, all, all, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 2, {0.0f, 0.0f, 4.0f});
    CHECK(build.has_value());
    if (!build) {
        return;
    }
    CHECK(build->parts.size() == 2);
    CHECK(build->parts[0].parent == -1);
    CHECK(build->parts[1].parent == 0);
    CHECK(std::fabs(build->parts[0].center.x - 2.0f) < 1e-4f && std::fabs(build->parts[0].center.y - 0.75f) < 1e-4f);
    CHECK(std::fabs(build->parts[1].pivot.y - 0.5f) < 1e-4f);
    CHECK(std::fabs(build->parts[1].velocity.z - 4.0f) < 1e-4f);
    CHECK(build->rig.boneDriver[0] == 0);
    CHECK(ragdollJointLimit("calf.L").twist < ragdollJointLimit("upperarm.R").twist);

    std::vector<Matrix> start;
    for (const RagdollPartDesc& part : build->parts) {
        start.push_back(MatrixMultiply(
            QuaternionToMatrix(part.rotation), MatrixTranslate(part.center.x, part.center.y, part.center.z)));
    }
    const std::vector<Matrix> before = rig.skin;
    const Matrix entity = applyRagdollPose(build->rig, start, rig.model, nullptr);
    CHECK(matricesNear(entity, world));
    for (std::size_t bone = 0; bone < before.size(); ++bone) {
        CHECK(matricesNear(rig.skin[bone], before[bone]));
    }

    std::vector<Matrix> moved = start;
    for (Matrix& part : moved) {
        part = MatrixMultiply(part, MatrixTranslate(0.0f, 3.0f, 0.0f));
    }
    const Matrix movedEntity = applyRagdollPose(build->rig, moved, rig.model, nullptr);
    CHECK(std::fabs(movedEntity.m13 - 3.0f) < 1e-3f);
    CHECK(matricesNear(rig.skin[1], before[1]));

    moved[1] = MatrixMultiply(start[1], MatrixTranslate(0.0f, 3.0f, 1.0f));
    applyRagdollPose(build->rig, moved, rig.model, nullptr);
    const Vector3 calfVertex = Vector3Transform({0.0f, 0.25f, 0.0f}, rig.skin[2]);
    CHECK(std::fabs(calfVertex.z - 1.0f) < 1e-3f);

    {
        PhysicsWorld physics;
        CharacterMotor motor{};
        physics.createCharacter(99, 20.0f, 0.1f, 20.0f, motor);
        CHECK(physics.createRagdoll(7, build->parts));
        CHECK(physics.hasRagdoll(7));
        for (int i = 0; i < 30; ++i) {
            CharacterStep step{};
            step.id = 99;
            step.motor = &motor;
            physics.step({step});
        }
        std::vector<Matrix> parts;
        CHECK(physics.ragdollPartTransforms(7, parts));
        CHECK(parts.size() == 2);
        if (parts.size() == 2) {
            CHECK(parts[0].m13 < 0.75f - 0.5f);
            const Vector3 thighBottom = Vector3Transform({0.0f, -0.25f, 0.0f}, parts[0]);
            const Vector3 calfTop = Vector3Transform({0.0f, 0.25f, 0.0f}, parts[1]);
            CHECK(Vector3Distance(thighBottom, calfTop) < 0.1f);
        }
        physics.destroyRagdoll(7);
        CHECK(!physics.hasRagdoll(7));
    }
}

}
