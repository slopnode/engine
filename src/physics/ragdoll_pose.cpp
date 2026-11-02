#include "physics/ragdoll_pose.hpp"

#include <algorithm>
#include <cmath>

namespace slopengine {

namespace {

Matrix poseMatrix(const Transform& pose) {
    return MatrixMultiply(
        MatrixMultiply(MatrixScale(pose.scale.x, pose.scale.y, pose.scale.z), QuaternionToMatrix(pose.rotation)),
        MatrixTranslate(pose.translation.x, pose.translation.y, pose.translation.z));
}

Transform poseFromMatrix(const Matrix& matrix) {
    Transform pose{};
    MatrixDecompose(matrix, &pose.translation, &pose.rotation, &pose.scale);
    return pose;
}

bool contains(std::string_view name, std::string_view part) {
    return name.find(part) != std::string_view::npos;
}

int nearestAncestor(const BoneInfo* bones, int bone, const std::vector<int>& partOfBone) {
    for (int parent = bones[bone].parent; parent >= 0; parent = bones[parent].parent) {
        if (partOfBone[static_cast<std::size_t>(parent)] >= 0) {
            return partOfBone[static_cast<std::size_t>(parent)];
        }
    }
    return -1;
}

}

RagdollJointLimit ragdollJointLimit(std::string_view name) {
    if (contains(name, "head") || contains(name, "neck")) {
        return {0.6f, 0.6f};
    }
    if (contains(name, "spine") || contains(name, "chest")) {
        return {0.35f, 0.25f};
    }
    if (contains(name, "upperarm") || contains(name, "upper_arm") || contains(name, "shoulder")) {
        return {1.3f, 0.8f};
    }
    if (contains(name, "forearm")) {
        return {1.2f, 0.3f};
    }
    if (contains(name, "hand") || contains(name, "f_") || contains(name, "thumb")) {
        return {0.7f, 0.4f};
    }
    if (contains(name, "thigh")) {
        return {1.0f, 0.3f};
    }
    if (contains(name, "calf") || contains(name, "shin")) {
        return {1.2f, 0.2f};
    }
    if (contains(name, "foot") || contains(name, "toe")) {
        return {0.6f, 0.3f};
    }
    return {0.5f, 0.3f};
}

std::optional<RagdollBuild> buildRagdoll(
    const Model& model,
    const std::vector<BoneHitbox>& boxes,
    const Matrix& world,
    const std::vector<std::uint8_t>& physical,
    const std::vector<std::uint8_t>& follow,
    Vector3 velocity,
    Vector3 angularVelocity,
    int hitBone,
    Vector3 hitVelocity) {
    const int boneCount = model.skeleton.boneCount;
    const BoneInfo* bones = model.skeleton.bones;
    if (boneCount <= 0 || bones == nullptr || model.boneMatrices == nullptr || model.currentPose == nullptr ||
        physical.size() != static_cast<std::size_t>(boneCount) ||
        follow.size() != static_cast<std::size_t>(boneCount) || boxes.size() < static_cast<std::size_t>(boneCount)) {
        return std::nullopt;
    }

    std::vector<int> depth(static_cast<std::size_t>(boneCount), 0);
    for (int bone = 0; bone < boneCount; ++bone) {
        for (int parent = bones[bone].parent; parent >= 0 && depth[static_cast<std::size_t>(bone)] < boneCount;
             parent = bones[parent].parent) {
            ++depth[static_cast<std::size_t>(bone)];
        }
    }
    std::vector<int> order(static_cast<std::size_t>(boneCount));
    for (int bone = 0; bone < boneCount; ++bone) {
        order[static_cast<std::size_t>(bone)] = bone;
    }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return depth[static_cast<std::size_t>(a)] < depth[static_cast<std::size_t>(b)];
    });

    std::vector<Matrix> skinWorld(static_cast<std::size_t>(boneCount));
    std::vector<Matrix> poseWorld(static_cast<std::size_t>(boneCount));
    for (int bone = 0; bone < boneCount; ++bone) {
        skinWorld[static_cast<std::size_t>(bone)] = MatrixMultiply(model.boneMatrices[bone], world);
        poseWorld[static_cast<std::size_t>(bone)] = MatrixMultiply(poseMatrix(model.currentPose[bone]), world);
    }

    RagdollBuild build;
    std::vector<int> partOfBone(static_cast<std::size_t>(boneCount), -1);
    std::vector<Matrix> partStart;
    for (int bone : order) {
        const BoneHitbox& box = boxes[static_cast<std::size_t>(bone)];
        if (physical[static_cast<std::size_t>(bone)] == 0 || !box.valid) {
            continue;
        }
        Vector3 translation{};
        Quaternion rotation{};
        Vector3 scale{};
        MatrixDecompose(skinWorld[static_cast<std::size_t>(bone)], &translation, &rotation, &scale);
        if (!std::isfinite(scale.x) || std::fabs(scale.x * scale.y * scale.z) < 1e-6f) {
            continue;
        }
        RagdollPartDesc part{};
        part.parent = nearestAncestor(bones, bone, partOfBone);
        const Vector3 boxCenter = Vector3Scale(Vector3Add(box.min, box.max), 0.5f);
        part.center = Vector3Transform(boxCenter, skinWorld[static_cast<std::size_t>(bone)]);
        part.rotation = QuaternionNormalize(rotation);
        const Vector3 half = Vector3Scale(Vector3Subtract(box.max, box.min), 0.5f);
        part.halfExtents = {half.x * std::fabs(scale.x), half.y * std::fabs(scale.y), half.z * std::fabs(scale.z)};
        const Matrix& joint = poseWorld[static_cast<std::size_t>(bone)];
        part.pivot = {joint.m12, joint.m13, joint.m14};
        Vector3 axis = Vector3Subtract(part.center, part.pivot);
        part.twistAxis = Vector3LengthSqr(axis) > 1e-8f ? Vector3Normalize(axis)
                                                        : Vector3RotateByQuaternion({0.0f, 1.0f, 0.0f}, part.rotation);
        const RagdollJointLimit limit = ragdollJointLimit(bones[bone].name);
        part.swing = limit.swing;
        part.twist = limit.twist;
        part.velocity = velocity;
        part.angularVelocity = angularVelocity;
        partOfBone[static_cast<std::size_t>(bone)] = static_cast<int>(build.parts.size());
        build.parts.push_back(part);
        build.rig.partBone.push_back(bone);
        partStart.push_back(MatrixMultiply(
            QuaternionToMatrix(part.rotation), MatrixTranslate(part.center.x, part.center.y, part.center.z)));
    }
    if (build.parts.empty()) {
        return std::nullopt;
    }

    if (hitBone >= 0 && hitBone < boneCount) {
        int hitPart = partOfBone[static_cast<std::size_t>(hitBone)];
        if (hitPart < 0) {
            hitPart = nearestAncestor(bones, hitBone, partOfBone);
        }
        if (hitPart >= 0) {
            RagdollPartDesc& part = build.parts[static_cast<std::size_t>(hitPart)];
            part.velocity = Vector3Add(part.velocity, hitVelocity);
        }
    }

    RagdollRig& rig = build.rig;
    rig.rootPart = 0;
    rig.boneDriver.assign(static_cast<std::size_t>(boneCount), -1);
    rig.skinOffset.assign(static_cast<std::size_t>(boneCount), MatrixIdentity());
    rig.poseOffset.assign(static_cast<std::size_t>(boneCount), MatrixIdentity());
    for (int bone = 0; bone < boneCount; ++bone) {
        if (follow[static_cast<std::size_t>(bone)] == 0) {
            continue;
        }
        int driver = partOfBone[static_cast<std::size_t>(bone)];
        if (driver < 0) {
            driver = nearestAncestor(bones, bone, partOfBone);
        }
        if (driver < 0) {
            driver = rig.rootPart;
        }
        const Matrix inverseStart = MatrixInvert(partStart[static_cast<std::size_t>(driver)]);
        rig.boneDriver[static_cast<std::size_t>(bone)] = driver;
        rig.skinOffset[static_cast<std::size_t>(bone)] =
            MatrixMultiply(skinWorld[static_cast<std::size_t>(bone)], inverseStart);
        rig.poseOffset[static_cast<std::size_t>(bone)] =
            MatrixMultiply(poseWorld[static_cast<std::size_t>(bone)], inverseStart);
    }
    rig.entityOffset = MatrixMultiply(world, MatrixInvert(partStart[static_cast<std::size_t>(rig.rootPart)]));
    return build;
}

Matrix applyRagdollPose(
    const RagdollRig& rig,
    const std::vector<Matrix>& partWorld,
    Model& model,
    const std::uint8_t* hidden) {
    const int boneCount = model.skeleton.boneCount;
    if (partWorld.empty() || static_cast<std::size_t>(rig.rootPart) >= partWorld.size() ||
        rig.boneDriver.size() != static_cast<std::size_t>(boneCount)) {
        return MatrixIdentity();
    }
    const Matrix entityWorld = MatrixMultiply(rig.entityOffset, partWorld[static_cast<std::size_t>(rig.rootPart)]);
    const Matrix toEntity = MatrixInvert(entityWorld);
    std::vector<Matrix> globals(static_cast<std::size_t>(boneCount));
    for (int bone = 0; bone < boneCount; ++bone) {
        const int driver = rig.boneDriver[static_cast<std::size_t>(bone)];
        if (driver < 0 || static_cast<std::size_t>(driver) >= partWorld.size()) {
            globals[static_cast<std::size_t>(bone)] = poseMatrix(model.currentPose[bone]);
            continue;
        }
        const Matrix& part = partWorld[static_cast<std::size_t>(driver)];
        model.boneMatrices[bone] =
            MatrixMultiply(MatrixMultiply(rig.skinOffset[static_cast<std::size_t>(bone)], part), toEntity);
        const Matrix pose = MatrixMultiply(MatrixMultiply(rig.poseOffset[static_cast<std::size_t>(bone)], part), toEntity);
        model.currentPose[bone] = poseFromMatrix(pose);
        globals[static_cast<std::size_t>(bone)] = pose;
    }
    collapseHiddenBones(model.skeleton.bones, boneCount, hidden, globals.data(), model.boneMatrices);
    return entityWorld;
}

}
