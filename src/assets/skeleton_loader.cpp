#include "assets/skeleton_loader.hpp"

#include <charconv>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <raymath.h>
#include <rlgl.h>

namespace slopengine {

namespace {

constexpr char kBindMagic[] = {'D', 'L', 'K', 'B'};
constexpr std::uint16_t kBindVersion = 1;

std::string_view trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
        value.remove_suffix(1);
    }
    return value;
}

std::optional<std::string> readQuotedField(std::string_view line, std::string_view prefix) {
    const std::size_t prefixPos = line.find(prefix);
    if (prefixPos == std::string_view::npos) {
        return std::nullopt;
    }

    const std::size_t quoteStart = line.find('"', prefixPos + prefix.size());
    if (quoteStart == std::string_view::npos) {
        return std::nullopt;
    }

    const std::size_t quoteEnd = line.find('"', quoteStart + 1);
    if (quoteEnd == std::string_view::npos) {
        return std::nullopt;
    }

    return std::string{line.substr(quoteStart + 1, quoteEnd - quoteStart - 1)};
}

std::optional<int> readIntField(std::string_view line, std::string_view prefix) {
    const std::size_t prefixPos = line.find(prefix);
    if (prefixPos == std::string_view::npos) {
        return std::nullopt;
    }

    std::string_view value = trim(line.substr(prefixPos + prefix.size()));
    int parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr == begin) {
        return std::nullopt;
    }

    return parsed;
}

bool readFloats(std::string_view text, std::size_t count, float* out) {
    std::string_view value = trim(text);
    for (std::size_t index = 0; index < count; ++index) {
        value = trim(value);
        if (value.empty()) {
            return false;
        }

        float parsed = 0.0f;
        const auto* begin = value.data();
        const auto* end = value.data() + value.size();
        const auto result = std::from_chars(begin, end, parsed);
        if (result.ec != std::errc{} || result.ptr == begin) {
            return false;
        }

        out[index] = parsed;
        value.remove_prefix(static_cast<std::size_t>(result.ptr - begin));
    }

    return true;
}

bool parseTransformFields(std::string_view line, Transform& transform) {
    const std::size_t translationPos = line.find("(t ");
    const std::size_t rotationPos = line.find("(r ");
    const std::size_t scalePos = line.find("(s ");
    if (translationPos == std::string_view::npos || rotationPos == std::string_view::npos ||
        scalePos == std::string_view::npos) {
        return false;
    }

    float translation[3] = {};
    float rotation[4] = {};
    float scale[3] = {1.0f, 1.0f, 1.0f};
    if (!readFloats(line.substr(translationPos + 3), 3, translation)) {
        return false;
    }
    if (!readFloats(line.substr(rotationPos + 3), 4, rotation)) {
        return false;
    }
    if (!readFloats(line.substr(scalePos + 3), 3, scale)) {
        return false;
    }

    transform.translation = {translation[0], translation[1], translation[2]};
    transform.rotation = {rotation[0], rotation[1], rotation[2], rotation[3]};
    transform.scale = {scale[0], scale[1], scale[2]};
    return true;
}

Matrix matrixFromPoseTransform(const Transform& transform) {
    return MatrixMultiply(
        MatrixMultiply(
            MatrixScale(transform.scale.x, transform.scale.y, transform.scale.z),
            QuaternionToMatrix(transform.rotation)),
        MatrixTranslate(
            transform.translation.x,
            transform.translation.y,
            transform.translation.z));
}

Matrix readMatrixFromFloats(const float* values) {
    Matrix matrix{};
    matrix.m0 = values[0];
    matrix.m1 = values[1];
    matrix.m2 = values[2];
    matrix.m3 = values[3];
    matrix.m4 = values[4];
    matrix.m5 = values[5];
    matrix.m6 = values[6];
    matrix.m7 = values[7];
    matrix.m8 = values[8];
    matrix.m9 = values[9];
    matrix.m10 = values[10];
    matrix.m11 = values[11];
    matrix.m12 = values[12];
    matrix.m13 = values[13];
    matrix.m14 = values[14];
    matrix.m15 = values[15];
    return matrix;
}

Transform transformFromMatrix(const Matrix& matrix) {
    Transform transform{};
    MatrixDecompose(matrix, &transform.translation, &transform.rotation, &transform.scale);
    return transform;
}

Matrix interpolatePoseMatrices(const Matrix& from, const Matrix& to, float blend) {
    if (blend <= 0.0f) {
        return from;
    }
    if (blend >= 1.0f) {
        return to;
    }

    const Transform fromTransform = transformFromMatrix(from);
    const Transform toTransform = transformFromMatrix(to);
    Transform blended{};
    blended.translation =
        Vector3Lerp(fromTransform.translation, toTransform.translation, blend);
    blended.rotation = QuaternionSlerp(fromTransform.rotation, toTransform.rotation, blend);
    blended.scale = Vector3Lerp(fromTransform.scale, toTransform.scale, blend);
    return matrixFromPoseTransform(blended);
}

Matrix skinMatrixForGpu(const Matrix& bindMatrix, const Matrix& currentMatrix) {
    return MatrixMultiply(MatrixInvert(bindMatrix), currentMatrix);
}

const std::vector<Matrix>* resolveBindMatrices(
    const std::vector<Matrix>* bindGlobalMatrices,
    const std::vector<std::vector<Matrix>>* matrixKeyframes,
    int boneCount,
    std::vector<Matrix>& fallbackBind) {
    if (bindGlobalMatrices != nullptr &&
        bindGlobalMatrices->size() == static_cast<std::size_t>(boneCount)) {
        return bindGlobalMatrices;
    }

    if (matrixKeyframes != nullptr && !matrixKeyframes->empty() &&
        (*matrixKeyframes)[0].size() == static_cast<std::size_t>(boneCount)) {
        fallbackBind = (*matrixKeyframes)[0];
        return &fallbackBind;
    }

    return nullptr;
}

template<typename T>
bool readScalar(std::span<const std::byte> data, std::size_t& offset, T& value) {
    if (offset + sizeof(T) > data.size()) {
        return false;
    }
    std::memcpy(&value, data.data() + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

} // namespace

void globalizePoseFromParentJoints(const BoneInfo* bones, int boneCount, Transform* transforms) {
    for (int index = 0; index < boneCount; ++index) {
        const int parent = bones[index].parent;
        if (parent < 0) {
            continue;
        }
        if (parent >= boneCount || parent >= index) {
            continue;
        }

        transforms[index].rotation =
            QuaternionMultiply(transforms[parent].rotation, transforms[index].rotation);
        transforms[index].scale = Vector3Multiply(transforms[index].scale, transforms[parent].scale);
        transforms[index].translation =
            Vector3Multiply(transforms[index].translation, transforms[parent].scale);
        transforms[index].translation =
            Vector3RotateByQuaternion(transforms[index].translation, transforms[parent].rotation);
        transforms[index].translation =
            Vector3Add(transforms[index].translation, transforms[parent].translation);
    }
}

void globalizePoseFromParentJoints(const SkeletonAsset& skeleton, Transform* transforms) {
    for (std::size_t index = 0; index < skeleton.bones.size(); ++index) {
        const int parent = skeleton.bones[index].parent;
        if (parent < 0) {
            continue;
        }
        if (static_cast<std::size_t>(parent) >= skeleton.bones.size() || parent >= static_cast<int>(index)) {
            continue;
        }

        transforms[index].rotation =
            QuaternionMultiply(transforms[parent].rotation, transforms[index].rotation);
        transforms[index].scale = Vector3Multiply(transforms[index].scale, transforms[parent].scale);
        transforms[index].translation =
            Vector3Multiply(transforms[index].translation, transforms[parent].scale);
        transforms[index].translation =
            Vector3RotateByQuaternion(transforms[index].translation, transforms[parent].rotation);
        transforms[index].translation =
            Vector3Add(transforms[index].translation, transforms[parent].translation);
    }
}

bool loadSkeletonBindMatrices(std::span<const std::byte> data, std::vector<Matrix>& bindMatrices) {
    bindMatrices.clear();
    if (data.size() < 4 + sizeof(std::uint16_t) + sizeof(std::uint32_t)) {
        return false;
    }
    if (std::memcmp(data.data(), kBindMagic, 4) != 0) {
        return false;
    }

    std::size_t offset = 4;
    std::uint16_t version = 0;
    std::uint32_t boneCount = 0;
    if (!readScalar(data, offset, version) || version != kBindVersion) {
        return false;
    }
    if (!readScalar(data, offset, boneCount) || boneCount == 0) {
        return false;
    }

    const std::size_t expectedBytes = boneCount * 16 * sizeof(float);
    if (offset + expectedBytes > data.size()) {
        return false;
    }

    bindMatrices.resize(boneCount);
    for (std::uint32_t bone = 0; bone < boneCount; ++bone) {
        float values[16] = {};
        for (float& value : values) {
            std::memcpy(&value, data.data() + offset, sizeof(float));
            offset += sizeof(float);
        }
        bindMatrices[bone] = readMatrixFromFloats(values);
    }

    return true;
}

bool parseSkeletonAsset(std::string_view source, SkeletonAsset& asset) {
    asset = {};

    std::size_t lineStart = 0;
    while (lineStart <= source.size()) {
        const std::size_t lineEnd = source.find('\n', lineStart);
        const std::string_view line = trim(source.substr(
            lineStart,
            lineEnd == std::string_view::npos ? std::string_view::npos : lineEnd - lineStart));

        if (lineEnd == std::string_view::npos && line.empty()) {
            break;
        }

        if (auto skeletonId = readQuotedField(line, "(id ")) {
            asset.id = *skeletonId;
        } else if (auto version = readIntField(line, "(version ")) {
            asset.version = *version;
        } else if (line.find("(name ") != std::string_view::npos) {
            SkeletonBone bone{};
            if (auto name = readQuotedField(line, "(name ")) {
                bone.name = *name;
            }
            if (auto parent = readIntField(line, "parent ")) {
                bone.parent = *parent;
            }
            if (!parseTransformFields(line, bone.bindPose)) {
                return false;
            }
            asset.bones.push_back(bone);
        }

        if (lineEnd == std::string_view::npos) {
            break;
        }
        lineStart = lineEnd + 1;
    }

    return !asset.bones.empty();
}

void applySkeletonToModel(const SkeletonAsset& asset, Model& model) {
    if (asset.bones.empty()) {
        return;
    }

    if (model.skeleton.bones != nullptr) {
        RL_FREE(model.skeleton.bones);
        model.skeleton.bones = nullptr;
    }
    if (model.skeleton.bindPose != nullptr) {
        RL_FREE(model.skeleton.bindPose);
        model.skeleton.bindPose = nullptr;
    }
    if (model.boneMatrices != nullptr) {
        RL_FREE(model.boneMatrices);
        model.boneMatrices = nullptr;
    }
    if (model.currentPose != nullptr) {
        RL_FREE(model.currentPose);
        model.currentPose = nullptr;
    }

    const int boneCount = static_cast<int>(asset.bones.size());
    model.skeleton.boneCount = boneCount;
    model.skeleton.bones = static_cast<BoneInfo*>(RL_CALLOC(boneCount, sizeof(BoneInfo)));
    model.skeleton.bindPose = static_cast<Transform*>(RL_CALLOC(boneCount, sizeof(Transform)));
    model.boneMatrices = static_cast<Matrix*>(RL_CALLOC(boneCount, sizeof(Matrix)));
    model.currentPose = static_cast<Transform*>(RL_CALLOC(boneCount, sizeof(Transform)));

    for (int index = 0; index < boneCount; ++index) {
        const SkeletonBone& sourceBone = asset.bones[static_cast<std::size_t>(index)];
        std::memset(model.skeleton.bones[index].name, 0, sizeof(model.skeleton.bones[index].name));
        std::strncpy(
            model.skeleton.bones[index].name,
            sourceBone.name.c_str(),
            sizeof(model.skeleton.bones[index].name) - 1);
        model.skeleton.bones[index].parent = sourceBone.parent;
        model.skeleton.bindPose[index] = sourceBone.bindPose;
        model.boneMatrices[index] = MatrixIdentity();
        model.currentPose[index] = sourceBone.bindPose;
    }

    globalizePoseFromParentJoints(model.skeleton.bones, boneCount, model.skeleton.bindPose);
    for (int index = 0; index < boneCount; ++index) {
        model.currentPose[index] = model.skeleton.bindPose[index];
    }
}

void applyBindPoseFromGlobalMatrices(Model& model, const std::vector<Matrix>& bindMatrices) {
    if (model.skeleton.bindPose == nullptr || model.currentPose == nullptr) {
        return;
    }

    const int boneCount = model.skeleton.boneCount;
    if (boneCount <= 0 ||
        bindMatrices.size() != static_cast<std::size_t>(boneCount)) {
        return;
    }

    for (int index = 0; index < boneCount; ++index) {
        const Transform bindTransform = transformFromMatrix(bindMatrices[static_cast<std::size_t>(index)]);
        model.skeleton.bindPose[index] = bindTransform;
        model.currentPose[index] = bindTransform;
    }
}

Model cloneGeoModelInstance(const Model& source) {
    Model instance = source;

    if (source.materialCount > 0 && source.materials != nullptr) {
        instance.materials = static_cast<Material*>(RL_CALLOC(source.materialCount, sizeof(Material)));
        for (int index = 0; index < source.materialCount; ++index) {
            instance.materials[index] = source.materials[index];
        }
    }

    if (source.skeleton.boneCount <= 0 || source.skeleton.bones == nullptr) {
        return instance;
    }

    const int boneCount = source.skeleton.boneCount;
    instance.skeleton.bones = static_cast<BoneInfo*>(RL_CALLOC(boneCount, sizeof(BoneInfo)));
    instance.skeleton.bindPose = static_cast<Transform*>(RL_CALLOC(boneCount, sizeof(Transform)));
    instance.currentPose = static_cast<Transform*>(RL_CALLOC(boneCount, sizeof(Transform)));
    instance.boneMatrices = static_cast<Matrix*>(RL_CALLOC(boneCount, sizeof(Matrix)));

    for (int index = 0; index < boneCount; ++index) {
        instance.skeleton.bones[index] = source.skeleton.bones[index];
        instance.skeleton.bindPose[index] = source.skeleton.bindPose[index];
        instance.currentPose[index] = source.skeleton.bindPose[index];
        instance.boneMatrices[index] = MatrixIdentity();
    }

    return instance;
}

void unloadClonedGeoModelInstance(Model& model) {
    if (model.materials != nullptr) {
        RL_FREE(model.materials);
    }
    RL_FREE(model.skeleton.bones);
    RL_FREE(model.skeleton.bindPose);
    RL_FREE(model.currentPose);
    RL_FREE(model.boneMatrices);
    model = {};
}

void allocateModelSkinningBuffers(Model& model) {
    for (int meshIndex = 0; meshIndex < model.meshCount; ++meshIndex) {
        Mesh& mesh = model.meshes[meshIndex];
        if (mesh.boneWeights == nullptr || mesh.boneIndices == nullptr || mesh.vertices == nullptr) {
            continue;
        }

        if (mesh.animVertices == nullptr) {
            mesh.animVertices =
                static_cast<float*>(RL_CALLOC(mesh.vertexCount * 3, sizeof(float)));
            std::memcpy(mesh.animVertices, mesh.vertices, mesh.vertexCount * 3 * sizeof(float));
        }

        if (mesh.normals != nullptr && mesh.animNormals == nullptr) {
            mesh.animNormals =
                static_cast<float*>(RL_CALLOC(mesh.vertexCount * 3, sizeof(float)));
            std::memcpy(mesh.animNormals, mesh.normals, mesh.vertexCount * 3 * sizeof(float));
        }
    }
}

void updateSkinnedMeshVertexBuffers(Model& model) {
    if (model.boneMatrices == nullptr) {
        return;
    }

    for (int meshIndex = 0; meshIndex < model.meshCount; ++meshIndex) {
        Mesh& mesh = model.meshes[meshIndex];
        if (mesh.boneWeights == nullptr || mesh.boneIndices == nullptr || mesh.animVertices == nullptr) {
            continue;
        }

        const int vertexValuesCount = mesh.vertexCount * 3;
        int boneCounter = 0;
        bool bufferUpdateRequired = false;

        for (int vertexOffset = 0; vertexOffset < vertexValuesCount; vertexOffset += 3) {
            mesh.animVertices[vertexOffset] = 0.0f;
            mesh.animVertices[vertexOffset + 1] = 0.0f;
            mesh.animVertices[vertexOffset + 2] = 0.0f;
            if (mesh.animNormals != nullptr) {
                mesh.animNormals[vertexOffset] = 0.0f;
                mesh.animNormals[vertexOffset + 1] = 0.0f;
                mesh.animNormals[vertexOffset + 2] = 0.0f;
            }

            for (int influence = 0; influence < 4; ++influence, ++boneCounter) {
                const float boneWeight = mesh.boneWeights[boneCounter];
                if (boneWeight == 0.0f) {
                    continue;
                }

                const int boneIndex = mesh.boneIndices[boneCounter];
                Vector3 skinnedVertex = {
                    mesh.vertices[vertexOffset],
                    mesh.vertices[vertexOffset + 1],
                    mesh.vertices[vertexOffset + 2],
                };
                skinnedVertex = Vector3Transform(skinnedVertex, model.boneMatrices[boneIndex]);
                mesh.animVertices[vertexOffset] += skinnedVertex.x * boneWeight;
                mesh.animVertices[vertexOffset + 1] += skinnedVertex.y * boneWeight;
                mesh.animVertices[vertexOffset + 2] += skinnedVertex.z * boneWeight;
                bufferUpdateRequired = true;

                if (mesh.normals != nullptr && mesh.animNormals != nullptr) {
                    Vector3 skinnedNormal = {
                        mesh.normals[vertexOffset],
                        mesh.normals[vertexOffset + 1],
                        mesh.normals[vertexOffset + 2],
                    };
                    skinnedNormal = Vector3Transform(
                        skinnedNormal,
                        MatrixTranspose(MatrixInvert(model.boneMatrices[boneIndex])));
                    mesh.animNormals[vertexOffset] += skinnedNormal.x * boneWeight;
                    mesh.animNormals[vertexOffset + 1] += skinnedNormal.y * boneWeight;
                    mesh.animNormals[vertexOffset + 2] += skinnedNormal.z * boneWeight;
                }
            }
        }

        if (!bufferUpdateRequired || mesh.vboId == nullptr) {
            continue;
        }

        rlUpdateVertexBuffer(
            mesh.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION],
            mesh.animVertices,
            mesh.vertexCount * 3 * sizeof(float),
            0);
        if (mesh.normals != nullptr && mesh.animNormals != nullptr) {
            rlUpdateVertexBuffer(
                mesh.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL],
                mesh.animNormals,
                mesh.vertexCount * 3 * sizeof(float),
                0);
        }
    }
}

void updateRiggedModelAnimation(
    Model& model,
    const ModelAnimation& anim,
    float frame,
    const std::vector<Matrix>* bindGlobalMatrices,
    const std::vector<std::vector<Matrix>>* matrixKeyframes) {
    if (model.boneMatrices == nullptr || model.skeleton.bones == nullptr || model.currentPose == nullptr ||
        anim.keyframePoses == nullptr || anim.keyframeCount <= 0) {
        return;
    }

    int currentFrame = static_cast<int>(frame);
    int nextFrame = currentFrame + 1;
    float blend = frame - static_cast<float>(currentFrame);
    blend = Clamp(blend, 0.0f, 1.0f);
    if (currentFrame >= anim.keyframeCount) {
        currentFrame = currentFrame % anim.keyframeCount;
    }
    if (nextFrame >= anim.keyframeCount) {
        nextFrame = nextFrame % anim.keyframeCount;
    }

    const int boneCount = model.skeleton.boneCount;
    const bool hasMatrixKeyframes = matrixKeyframes != nullptr && !matrixKeyframes->empty() &&
        static_cast<int>(matrixKeyframes->size()) >= anim.keyframeCount &&
        (*matrixKeyframes)[0].size() == static_cast<std::size_t>(boneCount);

    std::vector<Matrix> fallbackBind;
    const std::vector<Matrix>* bindMatrices =
        resolveBindMatrices(bindGlobalMatrices, matrixKeyframes, boneCount, fallbackBind);

    if (hasMatrixKeyframes && bindMatrices == nullptr) {
        TraceLog(LOG_WARNING, "RIG: matrix animation tracks require a .bind file");
        return;
    }

    if (hasMatrixKeyframes && bindMatrices != nullptr) {
        static bool loggedMatrixSkinning = false;
        if (!loggedMatrixSkinning) {
            TraceLog(
                LOG_INFO,
                "RIG: using matrix skinning (%s bind source)",
                bindGlobalMatrices != nullptr ? ".bind file" : "clip frame 0");
            loggedMatrixSkinning = true;
        }
        for (int boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
            const Matrix& bindMatrix = (*bindMatrices)[static_cast<std::size_t>(boneIndex)];
            const Matrix& fromMatrix =
                (*matrixKeyframes)[static_cast<std::size_t>(currentFrame)][static_cast<std::size_t>(boneIndex)];
            const Matrix& toMatrix =
                (*matrixKeyframes)[static_cast<std::size_t>(nextFrame)][static_cast<std::size_t>(boneIndex)];
            const Matrix currentMatrix = interpolatePoseMatrices(fromMatrix, toMatrix, blend);
            model.currentPose[boneIndex] = transformFromMatrix(currentMatrix);
            model.boneMatrices[boneIndex] = skinMatrixForGpu(bindMatrix, currentMatrix);
        }
        updateSkinnedMeshVertexBuffers(model);
        return;
    }

    for (int boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
        model.currentPose[boneIndex].translation = Vector3Lerp(
            anim.keyframePoses[currentFrame][boneIndex].translation,
            anim.keyframePoses[nextFrame][boneIndex].translation,
            blend);
        model.currentPose[boneIndex].rotation = QuaternionSlerp(
            anim.keyframePoses[currentFrame][boneIndex].rotation,
            anim.keyframePoses[nextFrame][boneIndex].rotation,
            blend);
        model.currentPose[boneIndex].scale = Vector3Lerp(
            anim.keyframePoses[currentFrame][boneIndex].scale,
            anim.keyframePoses[nextFrame][boneIndex].scale,
            blend);

        const Matrix bindPoseMatrix = matrixFromPoseTransform(model.skeleton.bindPose[boneIndex]);
        const Matrix currentPoseMatrix = matrixFromPoseTransform(model.currentPose[boneIndex]);
        model.boneMatrices[boneIndex] =
            MatrixMultiply(MatrixInvert(bindPoseMatrix), currentPoseMatrix);
    }

    updateSkinnedMeshVertexBuffers(model);
}

}
