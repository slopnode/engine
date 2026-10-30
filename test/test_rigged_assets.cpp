#include "test_assert.hpp"

#include "assets/anim_loader.hpp"
#include "assets/geo_loader.hpp"
#include "assets/rigged_assets.hpp"
#include "assets/skeleton_loader.hpp"
#include "render/bone_attachment.hpp"

#include <raylib.h>
#include <raymath.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace slopengine {

namespace {

constexpr float kPi = 3.14159265358979323846f;

struct ByteWriter {
    std::vector<std::byte> bytes;

    template <typename T>
    void put(const T& value) {
        const std::size_t offset = bytes.size();
        bytes.resize(offset + sizeof(T));
        std::memcpy(bytes.data() + offset, &value, sizeof(T));
    }

    void magic(const char* text) {
        for (int i = 0; i < 4; ++i) {
            put(static_cast<std::uint8_t>(text[i]));
        }
    }

    void matrix(const Matrix& m) {
        const float16 values = MatrixToFloatV(m);
        for (float value : values.v) {
            put(value);
        }
    }

    std::span<const std::byte> view() const {
        return {bytes.data(), bytes.size()};
    }
};

bool near(float a, float b, float tolerance = 1e-4f) {
    return std::fabs(a - b) <= tolerance;
}

const char* kSkeletonSource =
    "(skeleton\n"
    "  (id \"rig\")\n"
    "  (version 1)\n"
    "  (bones\n"
    "   (\n"
    "    (name \"root\" parent -1 bind (t 0 0 0) (r 0 0 0 1) (s 1 1 1))\n"
    "    (name \"child\" parent 0 bind (t 0 1 0) (r 0 0 0 1) (s 1 1 1))\n"
    "   )))\n";

const char* kGeoSource =
    "(geo\n"
    "  (vertices implicit)\n"
    "  (weights implicit)\n"
    "  (skeleton \"rig\")\n"
    "  (primitives\n"
    "   (\n"
    "    (name \"0\"\n"
    "     material \"rig/skin\"\n"
    "     vertex-offset 0\n"
    "     vertex-count 3\n"
    "     index-offset 0\n"
    "     index-count 3\n"
    "    )\n"
    "   ))\n"
    ")\n";

const char* kAnimSource =
    "(anim\n"
    "  (skeleton \"rig\")\n"
    "  (clips\n"
    "   (\n"
    "    (name \"wave\"\n"
    "     fps 10\n"
    "     duration 0.1\n"
    "     event 0 \"start\"\n"
    "     event 1 \"hit\"\n"
    "     tracks \"rig_wave.tracks\")\n"
    "    (name \"still\"\n"
    "     fps 24\n"
    "     duration 0)\n"
    "   ))\n"
    ")\n";

Matrix childFrameOne() {
    return MatrixMultiply(MatrixRotateZ(kPi * 0.5f), MatrixTranslate(0.0f, 1.0f, 0.0f));
}

ByteWriter buildVertFile() {
    ByteWriter writer;
    writer.magic("DLKV");
    writer.put<std::uint16_t>(1);
    writer.put<std::uint32_t>(3);
    writer.put<std::uint32_t>(3);
    writer.put<std::uint16_t>(3);
    const float positions[] = {0.0f, 2.0f, 0.0f, 1.0f, 2.0f, 0.0f, 0.0f, 3.0f, 0.0f};
    for (float value : positions) {
        writer.put(value);
    }
    const float normals[] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    for (float value : normals) {
        writer.put(value);
    }
    const float uvs[] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    for (float value : uvs) {
        writer.put(value);
    }
    for (std::uint32_t index = 0; index < 3; ++index) {
        writer.put(index);
    }
    return writer;
}

ByteWriter buildWeightsFile() {
    ByteWriter writer;
    writer.magic("DLKW");
    writer.put<std::uint16_t>(1);
    writer.put<std::uint32_t>(3);
    for (int vertex = 0; vertex < 3; ++vertex) {
        const std::uint8_t joints[4] = {1, 0, 0, 0};
        const float weights[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        for (std::uint8_t joint : joints) {
            writer.put(joint);
        }
        for (float weight : weights) {
            writer.put(weight);
        }
    }
    return writer;
}

ByteWriter buildBindFile() {
    ByteWriter writer;
    writer.magic("DLKB");
    writer.put<std::uint16_t>(1);
    writer.put<std::uint32_t>(2);
    writer.matrix(MatrixIdentity());
    writer.matrix(MatrixTranslate(0.0f, 1.0f, 0.0f));
    return writer;
}

ByteWriter buildTracksFile(std::uint32_t boneCount = 2) {
    ByteWriter writer;
    writer.magic("DLKT");
    writer.put<std::uint16_t>(2);
    writer.put<std::uint32_t>(boneCount);
    writer.put<std::uint32_t>(2);
    for (std::uint32_t frame = 0; frame < 2; ++frame) {
        for (std::uint32_t bone = 0; bone < boneCount; ++bone) {
            if (bone == 1 && frame == 1) {
                writer.matrix(childFrameOne());
            } else if (bone == 1) {
                writer.matrix(MatrixTranslate(0.0f, 1.0f, 0.0f));
            } else {
                writer.matrix(MatrixIdentity());
            }
        }
    }
    return writer;
}

Model buildTestModel(const VertBuffer& buffer, const std::vector<VertexWeights>& weights) {
    Model model{};
    model.meshCount = 1;
    model.materialCount = 1;
    model.meshes = static_cast<Mesh*>(RL_CALLOC(1, sizeof(Mesh)));
    model.materials = static_cast<Material*>(RL_CALLOC(1, sizeof(Material)));
    model.meshMaterial = static_cast<int*>(RL_CALLOC(1, sizeof(int)));
    model.transform = MatrixIdentity();

    Mesh& mesh = model.meshes[0];
    mesh.vertexCount = static_cast<int>(buffer.positions.size());
    mesh.triangleCount = static_cast<int>(buffer.indices.size() / 3);
    mesh.vertices = static_cast<float*>(RL_MALLOC(buffer.positions.size() * 3 * sizeof(float)));
    mesh.normals = static_cast<float*>(RL_MALLOC(buffer.normals.size() * 3 * sizeof(float)));
    std::memcpy(mesh.vertices, buffer.positions.data(), buffer.positions.size() * 3 * sizeof(float));
    std::memcpy(mesh.normals, buffer.normals.data(), buffer.normals.size() * 3 * sizeof(float));
    mesh.boneCount = 2;
    mesh.boneIndices = static_cast<unsigned char*>(RL_MALLOC(weights.size() * 4));
    mesh.boneWeights = static_cast<float*>(RL_MALLOC(weights.size() * 4 * sizeof(float)));
    for (std::size_t vertex = 0; vertex < weights.size(); ++vertex) {
        for (int influence = 0; influence < 4; ++influence) {
            mesh.boneIndices[vertex * 4 + influence] = weights[vertex].jointIndices[influence];
            mesh.boneWeights[vertex * 4 + influence] = weights[vertex].jointWeights[influence];
        }
    }
    return model;
}

void freeTestModel(Model& model) {
    Mesh& mesh = model.meshes[0];
    RL_FREE(mesh.vertices);
    RL_FREE(mesh.normals);
    RL_FREE(mesh.boneIndices);
    RL_FREE(mesh.boneWeights);
    RL_FREE(mesh.animVertices);
    RL_FREE(mesh.animNormals);
    RL_FREE(model.meshes);
    RL_FREE(model.materials);
    RL_FREE(model.meshMaterial);
    RL_FREE(model.skeleton.bones);
    RL_FREE(model.skeleton.bindPose);
    RL_FREE(model.currentPose);
    RL_FREE(model.boneMatrices);
    model = {};
}

} // namespace

void runRiggedAssetTests() {
    SkeletonAsset skeleton{};
    {
        CHECK(parseSkeletonAsset(kSkeletonSource, skeleton));
        CHECK_EQ(skeleton.id, std::string("rig"));
        CHECK_EQ(skeleton.version, 1);
        CHECK_EQ(skeleton.bones.size(), std::size_t{2});
        CHECK_EQ(skeleton.bones[0].name, std::string("root"));
        CHECK_EQ(skeleton.bones[0].parent, -1);
        CHECK_EQ(skeleton.bones[1].name, std::string("child"));
        CHECK_EQ(skeleton.bones[1].parent, 0);
        CHECK(near(skeleton.bones[1].bindPose.translation.y, 1.0f));
        CHECK(near(skeleton.bones[1].bindPose.rotation.w, 1.0f));
        CHECK(near(skeleton.bones[1].bindPose.scale.x, 1.0f));

        SkeletonAsset empty{};
        CHECK_FALSE(parseSkeletonAsset("(skeleton (id \"x\") (bones ()))", empty));
    }

    GeoAsset geo{};
    {
        CHECK(parseGeoAsset(kGeoSource, geo));
        CHECK_EQ(geo.skeletonId, std::string("rig"));
        CHECK(geo.verticesImplicit);
        CHECK(geo.weightsImplicit);
        CHECK_EQ(geo.primitives.size(), std::size_t{1});
        CHECK_EQ(geo.primitives[0].material, std::string("rig/skin"));
        CHECK_EQ(geo.primitives[0].vertexCount, std::size_t{3});
        CHECK_EQ(geo.primitives[0].indexCount, std::size_t{3});

        GeoAsset staticGeo{};
        CHECK(parseGeoAsset(
            "(geo\n"
            "  (vertices implicit)\n"
            "  (primitives\n"
            "   (\n"
            "    (name \"0\"\n"
            "     material \"m\"\n"
            "     vertex-offset 0\n"
            "     vertex-count 4\n"
            "     index-offset 0\n"
            "     index-count 6)\n"
            "   )))\n",
            staticGeo));
        CHECK(staticGeo.skeletonId.empty());
        CHECK_FALSE(staticGeo.weightsImplicit);
    }

    AnimAsset anim{};
    {
        CHECK(parseAnimAsset(kAnimSource, anim));
        CHECK_EQ(anim.skeletonId, std::string("rig"));
        CHECK_EQ(anim.clips.size(), std::size_t{2});
        CHECK_EQ(anim.clips[0].name, std::string("wave"));
        CHECK(near(anim.clips[0].fps, 10.0f));
        CHECK(near(anim.clips[0].duration, 0.1f));
        CHECK_FALSE(anim.clips[0].tracksImplicit);
        CHECK_EQ(anim.clips[0].tracksFile, std::string("rig_wave.tracks"));
        CHECK_EQ(anim.clips[0].events.size(), std::size_t{2});
        CHECK_EQ(anim.clips[0].events[0].frame, 0);
        CHECK_EQ(anim.clips[0].events[0].name, std::string("start"));
        CHECK_EQ(anim.clips[0].events[1].frame, 1);
        CHECK_EQ(anim.clips[0].events[1].name, std::string("hit"));
        CHECK_EQ(anim.clips[1].name, std::string("still"));
        CHECK(anim.clips[1].tracksImplicit);
        CHECK(anim.clips[1].events.empty());

        AnimAsset broken{};
        CHECK_FALSE(parseAnimAsset("(anim (clips (\n(name \"a\"\n event x \"bad\")\n)))", broken));
    }

    {
        AnimClip clip{};
        clip.events = {{0, "start"}, {3, "mid"}, {7, "end"}};
        std::vector<const AnimEvent*> fired;

        collectAnimEvents(clip, -1.0f, 0.0f, false, fired);
        CHECK_EQ(fired.size(), std::size_t{1});
        CHECK_EQ(fired[0]->name, std::string("start"));

        fired.clear();
        collectAnimEvents(clip, 0.0f, 2.9f, false, fired);
        CHECK(fired.empty());

        fired.clear();
        collectAnimEvents(clip, 2.9f, 3.0f, false, fired);
        CHECK_EQ(fired.size(), std::size_t{1});
        CHECK_EQ(fired[0]->name, std::string("mid"));

        fired.clear();
        collectAnimEvents(clip, 6.5f, 0.4f, true, fired);
        CHECK_EQ(fired.size(), std::size_t{2});
        CHECK_EQ(fired[0]->name, std::string("start"));
        CHECK_EQ(fired[1]->name, std::string("end"));

        fired.clear();
        collectAnimEvents(clip, 2.0f, 7.0f, false, fired);
        CHECK_EQ(fired.size(), std::size_t{2});
    }

    VertBuffer buffer{};
    {
        const ByteWriter vert = buildVertFile();
        CHECK(loadVertBuffer(vert.view(), buffer));
        CHECK_EQ(buffer.positions.size(), std::size_t{3});
        CHECK_EQ(buffer.normals.size(), std::size_t{3});
        CHECK_EQ(buffer.texcoords.size(), std::size_t{3});
        CHECK_EQ(buffer.indices.size(), std::size_t{3});
        CHECK(near(buffer.positions[2].y, 3.0f));
        CHECK(near(buffer.texcoords[1].x, 1.0f));
        CHECK_EQ(buffer.indices[2], std::uint32_t{2});

        VertBuffer bad{};
        ByteWriter truncated = buildVertFile();
        truncated.bytes.resize(truncated.bytes.size() - 8);
        CHECK_FALSE(loadVertBuffer(truncated.view(), bad));
    }

    std::vector<VertexWeights> weights{};
    {
        const ByteWriter file = buildWeightsFile();
        CHECK(loadWeightsBuffer(file.view(), weights));
        CHECK_EQ(weights.size(), std::size_t{3});
        CHECK_EQ(static_cast<int>(weights[0].jointIndices[0]), 1);
        CHECK(near(weights[0].jointWeights[0], 1.0f));
        CHECK(near(weights[0].jointWeights[1], 0.0f));
    }

    std::vector<Matrix> bindMatrices{};
    {
        const ByteWriter file = buildBindFile();
        CHECK(loadSkeletonBindMatrices(file.view(), bindMatrices));
        CHECK_EQ(bindMatrices.size(), std::size_t{2});
        CHECK(near(bindMatrices[1].m13, 1.0f));
        CHECK(near(bindMatrices[1].m0, 1.0f));
    }

    ModelAnimation clip{};
    AnimClipMatrices matrices{};
    {
        const ByteWriter file = buildTracksFile();
        CHECK(loadTracksToModelAnimation(file.view(), skeleton, clip, &matrices));
        CHECK_EQ(clip.boneCount, 2);
        CHECK_EQ(clip.keyframeCount, 2);
        CHECK_EQ(matrices.keyframes.size(), std::size_t{2});
        CHECK(near(matrices.keyframes[1][1].m13, 1.0f));
        CHECK(near(clip.keyframePoses[0][1].translation.y, 1.0f));
        const Vector3 axisX = Vector3RotateByQuaternion({1.0f, 0.0f, 0.0f}, clip.keyframePoses[1][1].rotation);
        CHECK(near(axisX.x, 0.0f));
        CHECK(near(axisX.y, 1.0f));

        ModelAnimation mismatched{};
        const ByteWriter wrongBones = buildTracksFile(3);
        CHECK_FALSE(loadTracksToModelAnimation(wrongBones.view(), skeleton, mismatched, nullptr));
    }

    {
        Model model = buildTestModel(buffer, weights);
        applySkeletonToModel(skeleton, model);
        CHECK_EQ(model.skeleton.boneCount, 2);
        CHECK(model.skeleton.bones != nullptr);
        CHECK_EQ(model.skeleton.bones[1].parent, 0);
        CHECK_EQ(findModelBoneIndex(model, "child"), 1);
        CHECK_EQ(findModelBoneIndex(model, "root"), 0);
        CHECK_EQ(findModelBoneIndex(model, "chil"), -1);
        CHECK_EQ(findModelBoneIndex(model, "children"), -1);
        applyBindPoseFromGlobalMatrices(model, bindMatrices);
        CHECK(near(model.skeleton.bindPose[1].translation.y, 1.0f));
        allocateModelSkinningBuffers(model);
        CHECK(model.meshes[0].animVertices != nullptr);

        updateRiggedModelAnimation(model, clip, 0.0f, &bindMatrices, &matrices.keyframes);
        const float* skinned = model.meshes[0].animVertices;
        CHECK(near(skinned[0], 0.0f));
        CHECK(near(skinned[1], 2.0f));
        CHECK(near(skinned[2], 0.0f));

        updateRiggedModelAnimation(model, clip, 1.0f, &bindMatrices, &matrices.keyframes);
        CHECK(near(skinned[0], -1.0f));
        CHECK(near(skinned[1], 1.0f));
        CHECK(near(skinned[2], 0.0f));
        CHECK(near(skinned[3], -1.0f));
        CHECK(near(skinned[4], 2.0f));
        CHECK(near(skinned[6], -2.0f));
        CHECK(near(skinned[7], 1.0f));
        const float* normals = model.meshes[0].animNormals;
        CHECK(near(normals[2], 1.0f));
        CHECK(near(model.currentPose[1].translation.y, 1.0f));

        updateRiggedModelAnimation(model, clip, 0.5f, &bindMatrices, &matrices.keyframes);
        CHECK(skinned[0] < 0.0f);
        CHECK(skinned[0] > -1.0f);
        CHECK(skinned[1] > 1.0f);
        CHECK(skinned[1] < 2.0f);

        updateRiggedModelAnimation(model, clip, 1.0f, &bindMatrices, &matrices.keyframes);
        BoneAttachment attachment{};
        attachment.bone = "child";
        attachment.offset = {0.0f, 1.0f, 0.0f};
        attachment.scale = {0.5f, 0.5f, 0.5f};
        const Transform held = composeBoneAttachment(model.currentPose[1], attachment);
        CHECK(near(held.translation.x, -1.0f));
        CHECK(near(held.translation.y, 1.0f));
        CHECK(near(held.translation.z, 0.0f));
        CHECK(near(held.scale.x, 0.5f));
        const Vector3 heldUp = Vector3RotateByQuaternion({0.0f, 1.0f, 0.0f}, held.rotation);
        CHECK(near(heldUp.x, -1.0f));
        CHECK(near(heldUp.y, 0.0f));

        attachment.rotation = QuaternionFromAxisAngle({0.0f, 0.0f, 1.0f}, -kPi * 0.5f);
        const Transform unrotated = composeBoneAttachment(model.currentPose[1], attachment);
        const Vector3 up2 = Vector3RotateByQuaternion({0.0f, 1.0f, 0.0f}, unrotated.rotation);
        CHECK(near(up2.x, 0.0f));
        CHECK(near(up2.y, 1.0f));

        freeTestModel(model);
    }

    unloadModelAnimation(clip);
}

}
