#include "assets/skeleton_loader.hpp"

#include "assets/anim_loader.hpp"
#include "render/gpu_ready.hpp"
#include "render/model_hitboxes.hpp"

#include <algorithm>
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

bool parentsPrecedeChildren(const BoneInfo* bones, int boneCount) {
    for (int boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
        if (bones[boneIndex].parent >= boneIndex) {
            return false;
        }
    }
    return true;
}

/** Blends two global-space pose frames in parent-local space and rebuilds the chain, so a
 *  bone keeps its length while its parent swings instead of cutting the chord between the
 *  two keyframes. */
void interpolatePoseFrames(
    const BoneInfo* bones,
    int boneCount,
    const Matrix* from,
    const Matrix* to,
    float blend,
    std::vector<Matrix>& out,
    const Transform* fromLocalPose = nullptr,
    const Transform* toLocalPose = nullptr) {
    out.resize(static_cast<std::size_t>(boneCount));
    if (blend <= 0.0f || blend >= 1.0f || !parentsPrecedeChildren(bones, boneCount)) {
        for (int boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
            out[static_cast<std::size_t>(boneIndex)] =
                interpolatePoseMatrices(from[boneIndex], to[boneIndex], blend);
        }
        return;
    }
    if (fromLocalPose != nullptr && toLocalPose != nullptr) {
        for (int boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
            const Transform& a = fromLocalPose[boneIndex];
            const Transform& b = toLocalPose[boneIndex];
            Transform blended{};
            blended.translation = Vector3Lerp(a.translation, b.translation, blend);
            blended.rotation = QuaternionSlerp(a.rotation, b.rotation, blend);
            blended.scale = Vector3Lerp(a.scale, b.scale, blend);
            const Matrix local = matrixFromPoseTransform(blended);
            const int parent = bones[boneIndex].parent;
            out[static_cast<std::size_t>(boneIndex)] =
                parent >= 0 ? MatrixMultiply(local, out[static_cast<std::size_t>(parent)]) : local;
        }
        return;
    }
    for (int boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
        const int parent = bones[boneIndex].parent;
        Matrix fromLocal = from[boneIndex];
        Matrix toLocal = to[boneIndex];
        if (parent >= 0) {
            fromLocal = MatrixMultiply(fromLocal, MatrixInvert(from[parent]));
            toLocal = MatrixMultiply(toLocal, MatrixInvert(to[parent]));
        }
        const Matrix local = interpolatePoseMatrices(fromLocal, toLocal, blend);
        out[static_cast<std::size_t>(boneIndex)] =
            parent >= 0 ? MatrixMultiply(local, out[static_cast<std::size_t>(parent)]) : local;
    }
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

std::vector<std::vector<Transform>> buildLocalPoseKeyframes(
    const SkeletonAsset& skeleton,
    const std::vector<std::vector<Matrix>>& keyframes) {
    const std::size_t boneCount = skeleton.bones.size();
    for (std::size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
        if (skeleton.bones[boneIndex].parent >= static_cast<int>(boneIndex)) {
            return {};
        }
    }
    std::vector<std::vector<Transform>> local(keyframes.size());
    for (std::size_t frame = 0; frame < keyframes.size(); ++frame) {
        const std::vector<Matrix>& globals = keyframes[frame];
        if (globals.size() != boneCount) {
            return {};
        }
        local[frame].resize(boneCount);
        for (std::size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
            const int parent = skeleton.bones[boneIndex].parent;
            const Matrix matrix = parent >= 0
                ? MatrixMultiply(globals[boneIndex], MatrixInvert(globals[static_cast<std::size_t>(parent)]))
                : globals[boneIndex];
            local[frame][boneIndex] = transformFromMatrix(matrix);
        }
    }
    return local;
}

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

namespace {

bool isSkinnedMesh(const Mesh& mesh) {
    return mesh.boneWeights != nullptr && mesh.boneIndices != nullptr && mesh.vertices != nullptr;
}

template <typename T>
T* cloneArray(const T* source, std::size_t count) {
    if (source == nullptr || count == 0) {
        return nullptr;
    }
    T* copy = static_cast<T*>(RL_MALLOC(count * sizeof(T)));
    std::memcpy(copy, source, count * sizeof(T));
    return copy;
}

Mesh cloneSkinnedMesh(const Mesh& source) {
    const std::size_t vertexCount = static_cast<std::size_t>(source.vertexCount);
    Mesh mesh{};
    mesh.vertexCount = source.vertexCount;
    mesh.triangleCount = source.triangleCount;
    mesh.vertices = cloneArray(source.vertices, vertexCount * 3);
    mesh.texcoords = cloneArray(source.texcoords, vertexCount * 2);
    mesh.texcoords2 = cloneArray(source.texcoords2, vertexCount * 2);
    mesh.normals = cloneArray(source.normals, vertexCount * 3);
    mesh.tangents = cloneArray(source.tangents, vertexCount * 4);
    mesh.colors = cloneArray(source.colors, vertexCount * 4);
    mesh.indices = cloneArray(source.indices, static_cast<std::size_t>(source.triangleCount) * 3);
    mesh.boneCount = source.boneCount;
    mesh.boneIndices = cloneArray(source.boneIndices, vertexCount * 4);
    mesh.boneWeights = cloneArray(source.boneWeights, vertexCount * 4);
    mesh.animVertices = cloneArray(source.vertices, vertexCount * 3);
    mesh.animNormals = cloneArray(source.normals, vertexCount * 3);
    uploadMeshIfGpu(&mesh, true);
    return mesh;
}

} // namespace

Mesh cloneSkinnedMeshInstance(const Mesh& source) {
    return cloneSkinnedMesh(source);
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

    if (source.meshCount > 0 && source.meshes != nullptr) {
        instance.meshes = static_cast<Mesh*>(RL_CALLOC(source.meshCount, sizeof(Mesh)));
        instance.meshMaterial = static_cast<int*>(RL_CALLOC(source.meshCount, sizeof(int)));
        for (int index = 0; index < source.meshCount; ++index) {
            instance.meshMaterial[index] =
                source.meshMaterial != nullptr ? source.meshMaterial[index] : index;
            const Mesh& mesh = source.meshes[index];
            instance.meshes[index] = isSkinnedMesh(mesh) ? cloneSkinnedMesh(mesh) : mesh;
        }
    }

    return instance;
}

void unloadClonedGeoModelInstance(Model& model) {
    if (model.materials != nullptr) {
        RL_FREE(model.materials);
    }
    if (model.skeleton.bones != nullptr && model.meshes != nullptr) {
        for (int index = 0; index < model.meshCount; ++index) {
            if (isSkinnedMesh(model.meshes[index])) {
                UnloadMesh(model.meshes[index]);
            }
        }
        RL_FREE(model.meshes);
        RL_FREE(model.meshMaterial);
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

    std::vector<Matrix> normalMatrices(model.skeleton.boneCount);
    for (std::size_t boneIndex = 0; boneIndex < normalMatrices.size(); ++boneIndex) {
        normalMatrices[boneIndex] = MatrixTranspose(MatrixInvert(model.boneMatrices[boneIndex]));
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
                        normalMatrices[static_cast<std::size_t>(boneIndex)]);
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

namespace {

bool hasBoneScaling(const BoneScale* scales, int boneCount) {
    if (scales == nullptr) {
        return false;
    }
    for (int boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
        if (scales[boneIndex].size != 1.0f || scales[boneIndex].length != 1.0f) {
            return true;
        }
    }
    return false;
}

Matrix parentLocal(const BoneInfo* bones, const std::vector<Matrix>& globals, int boneIndex) {
    const int parent = bones[boneIndex].parent;
    const Matrix& global = globals[static_cast<std::size_t>(boneIndex)];
    return parent >= 0 ? MatrixMultiply(global, MatrixInvert(globals[static_cast<std::size_t>(parent)])) : global;
}

std::vector<Vector3> boneAxes(const BoneInfo* bones, int boneCount, const std::vector<Matrix>& bind) {
    std::vector<Vector3> axes(static_cast<std::size_t>(boneCount), Vector3{0.0f, 0.0f, -1.0f});
    std::vector<float> reach(static_cast<std::size_t>(boneCount), 0.0f);
    for (int boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
        const int parent = bones[boneIndex].parent;
        if (parent < 0) {
            continue;
        }
        const Matrix local = parentLocal(bones, bind, boneIndex);
        const Vector3 offset{local.m12, local.m13, local.m14};
        const float length = Vector3Length(offset);
        if (length > reach[static_cast<std::size_t>(parent)] && length > 1e-5f) {
            reach[static_cast<std::size_t>(parent)] = length;
            axes[static_cast<std::size_t>(parent)] = Vector3Scale(offset, 1.0f / length);
        }
    }
    return axes;
}

Vector3 scaleChildOffset(Vector3 offset, Vector3 axis, const BoneScale& scale) {
    const float along = Vector3DotProduct(offset, axis);
    const Vector3 stretched = Vector3Add(offset, Vector3Scale(axis, along * (scale.length - 1.0f)));
    return Vector3Scale(stretched, scale.size);
}

void rebuildScaledPose(
    const BoneInfo* bones,
    int boneCount,
    const std::vector<Matrix>& globals,
    const std::vector<Vector3>& axes,
    const BoneScale* scales,
    float lift,
    std::vector<Matrix>& out) {
    out.resize(static_cast<std::size_t>(boneCount));
    for (int boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
        Matrix local = parentLocal(bones, globals, boneIndex);
        const int parent = bones[boneIndex].parent;
        if (parent < 0) {
            local.m13 += lift;
            out[static_cast<std::size_t>(boneIndex)] = local;
            continue;
        }
        const Vector3 offset = scaleChildOffset(
            {local.m12, local.m13, local.m14}, axes[static_cast<std::size_t>(parent)], scales[parent]);
        local.m12 = offset.x;
        local.m13 = offset.y;
        local.m14 = offset.z;
        out[static_cast<std::size_t>(boneIndex)] = MatrixMultiply(local, out[static_cast<std::size_t>(parent)]);
    }
}

Matrix ownScaleMatrix(Vector3 axis, const BoneScale& scale) {
    const float s = scale.size;
    const float k = scale.length - 1.0f;
    Matrix matrix = MatrixIdentity();
    matrix.m0 = s * (1.0f + k * axis.x * axis.x);
    matrix.m5 = s * (1.0f + k * axis.y * axis.y);
    matrix.m10 = s * (1.0f + k * axis.z * axis.z);
    matrix.m1 = matrix.m4 = s * k * axis.x * axis.y;
    matrix.m2 = matrix.m8 = s * k * axis.x * axis.z;
    matrix.m6 = matrix.m9 = s * k * axis.y * axis.z;
    return matrix;
}

float maxJointReach(const std::vector<Matrix>& globals) {
    float reach = 0.0f;
    for (const Matrix& global : globals) {
        reach = std::max(reach, Vector3Length({global.m12, global.m13, global.m14}));
    }
    return reach;
}

PoseScaleResult finishPose(
    Model& model,
    const std::vector<Matrix>& bind,
    std::vector<Matrix>& current,
    const BoneScale* scales,
    const std::uint8_t* hiddenBones) {
    const int boneCount = model.skeleton.boneCount;
    const BoneInfo* bones = model.skeleton.bones;
    PoseScaleResult result{};
    const bool scaled = hasBoneScaling(scales, boneCount) && parentsPrecedeChildren(bones, boneCount);
    std::vector<Vector3> axes;
    if (scaled) {
        axes = boneAxes(bones, boneCount, bind);
        std::vector<Matrix> scaledBind;
        rebuildScaledPose(bones, boneCount, bind, axes, scales, 0.0f, scaledBind);
        std::vector<bool> leaf(static_cast<std::size_t>(boneCount), true);
        for (int boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
            if (bones[boneIndex].parent >= 0) {
                leaf[static_cast<std::size_t>(bones[boneIndex].parent)] = false;
            }
        }
        int lowest = -1;
        for (int boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
            if (leaf[static_cast<std::size_t>(boneIndex)] &&
                (lowest < 0 ||
                 bind[static_cast<std::size_t>(boneIndex)].m13 < bind[static_cast<std::size_t>(lowest)].m13)) {
                lowest = boneIndex;
            }
        }
        if (lowest >= 0) {
            result.lift = scales[lowest].size * bind[static_cast<std::size_t>(lowest)].m13 -
                scaledBind[static_cast<std::size_t>(lowest)].m13;
        }
        float partSpread = 1.0f;
        for (int boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
            partSpread = std::max(partSpread, scales[boneIndex].size * std::max(scales[boneIndex].length, 1.0f));
        }
        const float bindReach = maxJointReach(bind);
        const float scaledReach = maxJointReach(scaledBind);
        result.spread = std::max(partSpread, bindReach > 1e-5f ? scaledReach / bindReach : 1.0f);
        std::vector<Matrix> unscaled = current;
        rebuildScaledPose(bones, boneCount, unscaled, axes, scales, result.lift, current);
    }
    for (int boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
        const Matrix& currentMatrix = current[static_cast<std::size_t>(boneIndex)];
        const Matrix inverseBind = MatrixInvert(bind[static_cast<std::size_t>(boneIndex)]);
        model.currentPose[boneIndex] = transformFromMatrix(currentMatrix);
        model.boneMatrices[boneIndex] = scaled
            ? MatrixMultiply(
                  MatrixMultiply(inverseBind, ownScaleMatrix(axes[static_cast<std::size_t>(boneIndex)], scales[boneIndex])),
                  currentMatrix)
            : MatrixMultiply(inverseBind, currentMatrix);
    }
    collapseHiddenBones(bones, boneCount, hiddenBones, current.data(), model.boneMatrices);
    return result;
}

}

PoseScaleResult updateRiggedModelAnimation(
    Model& model,
    const ModelAnimation& anim,
    float frame,
    const std::vector<Matrix>* bindGlobalMatrices,
    const AnimClipMatrices* matrixClip,
    bool skinVertices,
    const BoneScale* boneScales,
    const std::uint8_t* hiddenBones) {
    const std::vector<std::vector<Matrix>>* matrixKeyframes =
        matrixClip != nullptr && !matrixClip->keyframes.empty() ? &matrixClip->keyframes : nullptr;
    if (model.boneMatrices == nullptr || model.skeleton.bones == nullptr || model.currentPose == nullptr ||
        anim.keyframePoses == nullptr || anim.keyframeCount <= 0) {
        return {};
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
        return {};
    }

    std::vector<Matrix> current;
    if (hasMatrixKeyframes && bindMatrices != nullptr) {
        static bool loggedMatrixSkinning = false;
        if (!loggedMatrixSkinning) {
            TraceLog(
                LOG_INFO,
                "RIG: using matrix skinning (%s bind source)",
                bindGlobalMatrices != nullptr ? ".bind file" : "clip frame 0");
            loggedMatrixSkinning = true;
        }
        const bool hasLocal = matrixClip->localKeyframes.size() == matrixKeyframes->size() &&
            matrixClip->localKeyframes[static_cast<std::size_t>(currentFrame)].size() ==
                static_cast<std::size_t>(boneCount) &&
            matrixClip->localKeyframes[static_cast<std::size_t>(nextFrame)].size() ==
                static_cast<std::size_t>(boneCount);
        interpolatePoseFrames(
            model.skeleton.bones,
            boneCount,
            (*matrixKeyframes)[static_cast<std::size_t>(currentFrame)].data(),
            (*matrixKeyframes)[static_cast<std::size_t>(nextFrame)].data(),
            blend,
            current,
            hasLocal ? matrixClip->localKeyframes[static_cast<std::size_t>(currentFrame)].data() : nullptr,
            hasLocal ? matrixClip->localKeyframes[static_cast<std::size_t>(nextFrame)].data() : nullptr);
        const PoseScaleResult result = finishPose(model, *bindMatrices, current, boneScales, hiddenBones);
        if (skinVertices) {
            updateSkinnedMeshVertexBuffers(model);
        }
        return result;
    }

    std::vector<Matrix> fromFrame(static_cast<std::size_t>(boneCount));
    std::vector<Matrix> toFrame(static_cast<std::size_t>(boneCount));
    for (int boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
        fromFrame[static_cast<std::size_t>(boneIndex)] =
            matrixFromPoseTransform(anim.keyframePoses[currentFrame][boneIndex]);
        toFrame[static_cast<std::size_t>(boneIndex)] =
            matrixFromPoseTransform(anim.keyframePoses[nextFrame][boneIndex]);
    }
    interpolatePoseFrames(model.skeleton.bones, boneCount, fromFrame.data(), toFrame.data(), blend, current);
    std::vector<Matrix> bindPose(static_cast<std::size_t>(boneCount));
    for (int boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
        bindPose[static_cast<std::size_t>(boneIndex)] = matrixFromPoseTransform(model.skeleton.bindPose[boneIndex]);
    }
    const PoseScaleResult result = finishPose(model, bindPose, current, boneScales, hiddenBones);

    if (skinVertices) {
        updateSkinnedMeshVertexBuffers(model);
    }
    return result;
}

}
