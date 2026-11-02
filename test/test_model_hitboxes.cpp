#include "test_assert.hpp"

#include "render/model_hitboxes.hpp"

#include <cmath>
#include <cstring>
#include <vector>

#include <raymath.h>

namespace slopengine {

namespace {

struct TestRig {
    std::vector<float> vertices;
    std::vector<unsigned char> boneIndices;
    std::vector<float> boneWeights;
    std::vector<BoneInfo> bones;
    std::vector<Matrix> skin;
    Mesh mesh{};
    Model model{};

    void addCube(Vector3 center, float half, int bone) {
        for (int corner = 0; corner < 8; ++corner) {
            vertices.push_back(center.x + ((corner & 1) != 0 ? half : -half));
            vertices.push_back(center.y + ((corner & 2) != 0 ? half : -half));
            vertices.push_back(center.z + ((corner & 4) != 0 ? half : -half));
            boneIndices.insert(boneIndices.end(), {static_cast<unsigned char>(bone), 0, 0, 0});
            boneWeights.insert(boneWeights.end(), {1.0f, 0.0f, 0.0f, 0.0f});
        }
    }

    void addBone(const char* name, int parent) {
        BoneInfo info{};
        std::strncpy(info.name, name, sizeof(info.name) - 1);
        info.parent = parent;
        bones.push_back(info);
        skin.push_back(MatrixIdentity());
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
    }
};

bool near(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) < eps;
}

}

void runModelHitboxTests() {
    TestRig rig;
    rig.addBone("root", -1);
    rig.addBone("upper", 0);
    rig.addBone("lower", 1);
    rig.addCube({0.0f, 0.5f, 0.0f}, 0.2f, 0);
    rig.addCube({0.0f, 1.5f, 0.0f}, 0.1f, 1);
    rig.addCube({0.0f, 2.5f, 0.0f}, 0.1f, 2);
    rig.finish();

    const std::vector<BoneHitbox> boxes = buildBoneHitboxes(rig.model);
    CHECK(boxes.size() == 3);
    CHECK(boxes[1].valid);
    CHECK(near(boxes[1].min.y, 1.4f));
    CHECK(near(boxes[1].max.y, 1.6f));

    const Ray ray{{0.0f, 1.5f, -5.0f}, {0.0f, 0.0f, 1.0f}};
    const auto hit = raycastModelBones(rig.model, boxes, MatrixIdentity(), nullptr, ray, 100.0f);
    CHECK(hit.has_value());
    if (hit) {
        CHECK(hit->bone == 1);
        CHECK(near(hit->distance, 4.9f));
    }
    CHECK(!raycastModelBones(rig.model, boxes, MatrixIdentity(), nullptr, ray, 4.0f).has_value());

    const std::vector<std::uint8_t> hideUpper{0, 1, 0};
    CHECK(!raycastModelBones(rig.model, boxes, MatrixIdentity(), &hideUpper, ray, 100.0f).has_value());

    rig.skin[1] = MatrixTranslate(2.0f, 0.0f, 0.0f);
    const Ray shifted{{2.0f, 1.5f, -5.0f}, {0.0f, 0.0f, 1.0f}};
    const auto moved = raycastModelBones(rig.model, boxes, MatrixTranslate(0.0f, 0.0f, 1.0f), nullptr, shifted, 100.0f);
    CHECK(moved.has_value() && moved->bone == 1 && near(moved->distance, 5.9f));
    rig.skin[1] = MatrixIdentity();

    const std::vector<int> subtree = boneSubtree(rig.model, 1);
    CHECK(subtree.size() == 2 && subtree[0] == 1 && subtree[1] == 2);
    CHECK(boneSubtree(rig.model, 0).size() == 3);

    BoundingBox bounds{};
    CHECK(posedBonesBounds(rig.model, boxes, MatrixIdentity(), subtree, bounds));
    CHECK(near(bounds.min.y, 1.4f) && near(bounds.max.y, 2.6f));

    std::vector<Matrix> globals{MatrixIdentity(), MatrixTranslate(0.0f, 1.0f, 0.0f), MatrixTranslate(0.0f, 2.0f, 0.0f)};
    const std::uint8_t hidden[3] = {0, 1, 1};
    collapseHiddenBones(rig.bones.data(), 3, hidden, globals.data(), rig.skin.data());
    const Vector3 a = Vector3Transform({5.0f, 7.0f, 9.0f}, rig.skin[1]);
    const Vector3 b = Vector3Transform({-3.0f, 2.0f, 1.0f}, rig.skin[2]);
    CHECK(near(a.x, 0.0f) && near(a.y, 1.0f) && near(a.z, 0.0f));
    CHECK(near(b.x, 0.0f) && near(b.y, 1.0f) && near(b.z, 0.0f));
    CHECK(rig.skin[0].m0 == 1.0f);
}

}
