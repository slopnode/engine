#include "assets/anim_loader.hpp"
#include "assets/skeleton_loader.hpp"

#include <charconv>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#include <raymath.h>

namespace daggerlike {

namespace {

constexpr char kTracksMagic[] = {'D', 'L', 'K', 'T'};
constexpr std::uint16_t kTracksVersionTrs = 1;
constexpr std::uint16_t kTracksVersionMatrix = 2;

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

std::optional<float> readFloatField(std::string_view line, std::string_view prefix) {
    const std::size_t prefixPos = line.find(prefix);
    if (prefixPos == std::string_view::npos) {
        return std::nullopt;
    }

    std::string_view value = trim(line.substr(prefixPos + prefix.size()));
    float parsed = 0.0f;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr == begin) {
        return std::nullopt;
    }

    return parsed;
}

bool startsWith(std::string_view line, std::string_view prefix) {
    return line.rfind(prefix, 0) == 0;
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

} // namespace

bool parseAnimAsset(std::string_view source, AnimAsset& asset) {
    asset = {};

    AnimClip current{};
    bool inClip = false;

    std::size_t lineStart = 0;
    while (lineStart <= source.size()) {
        const std::size_t lineEnd = source.find('\n', lineStart);
        const std::string_view line = trim(source.substr(
            lineStart,
            lineEnd == std::string_view::npos ? std::string_view::npos : lineEnd - lineStart));

        if (lineEnd == std::string_view::npos && line.empty()) {
            break;
        }

        if (auto skeletonId = readQuotedField(line, "(skeleton ")) {
            asset.skeletonId = *skeletonId;
        } else if (startsWith(line, "(name ")) {
            if (inClip) {
                asset.clips.push_back(current);
            }
            current = {};
            current.tracksImplicit = true;
            if (auto name = readQuotedField(line, "(name ")) {
                current.name = *name;
            }
            inClip = true;
        } else if (inClip) {
            if (auto fps = readFloatField(line, "fps ")) {
                current.fps = *fps;
            } else if (auto duration = readFloatField(line, "duration ")) {
                current.duration = *duration;
            } else if (auto tracks = readQuotedField(line, "tracks ")) {
                current.tracksFile = *tracks;
                current.tracksImplicit = false;
            } else if (startsWith(line, "tracks implicit")) {
                current.tracksImplicit = true;
            } else if (startsWith(line, ")")) {
                asset.clips.push_back(current);
                current = {};
                inClip = false;
            }
        }

        if (lineEnd == std::string_view::npos) {
            break;
        }
        lineStart = lineEnd + 1;
    }

    if (inClip) {
        asset.clips.push_back(current);
    }

    return !asset.clips.empty();
}

bool loadTracksToModelAnimation(
    std::span<const std::byte> data,
    const SkeletonAsset& skeleton,
    ModelAnimation& animation,
    AnimClipMatrices* matrixClip) {
    unloadModelAnimation(animation);
    if (matrixClip != nullptr) {
        matrixClip->keyframes.clear();
    }

    if (data.size() < 4 + sizeof(std::uint16_t) + sizeof(std::uint32_t) * 2) {
        return false;
    }
    if (std::memcmp(data.data(), kTracksMagic, 4) != 0) {
        return false;
    }

    std::size_t offset = 4;
    std::uint16_t version = 0;
    std::uint32_t boneCount = 0;
    std::uint32_t frameCount = 0;
    if (!readScalar(data, offset, version) ||
        (version != kTracksVersionTrs && version != kTracksVersionMatrix)) {
        return false;
    }
    if (!readScalar(data, offset, boneCount) || !readScalar(data, offset, frameCount)) {
        return false;
    }
    if (boneCount != skeleton.bones.size() || frameCount == 0) {
        return false;
    }

    const std::size_t floatsPerBone = version == kTracksVersionMatrix ? 16 : 10;
    const std::size_t expectedBytes = boneCount * frameCount * floatsPerBone * sizeof(float);
    if (offset + expectedBytes > data.size()) {
        return false;
    }

    animation.boneCount = static_cast<int>(boneCount);
    animation.keyframeCount = static_cast<int>(frameCount);
    animation.keyframePoses =
        static_cast<Transform**>(RL_CALLOC(frameCount, sizeof(Transform*)));

    if (matrixClip != nullptr && version == kTracksVersionMatrix) {
        matrixClip->keyframes.resize(frameCount);
    }

    for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
        animation.keyframePoses[frame] =
            static_cast<Transform*>(RL_CALLOC(boneCount, sizeof(Transform)));
        if (matrixClip != nullptr && version == kTracksVersionMatrix) {
            matrixClip->keyframes[frame].resize(boneCount);
        }

        for (std::uint32_t bone = 0; bone < boneCount; ++bone) {
            if (version == kTracksVersionMatrix) {
                float values[16] = {};
                for (float& value : values) {
                    std::memcpy(&value, data.data() + offset, sizeof(float));
                    offset += sizeof(float);
                }
                const Matrix matrix = readMatrixFromFloats(values);
                animation.keyframePoses[frame][static_cast<int>(bone)] = transformFromMatrix(matrix);
                if (matrixClip != nullptr) {
                    matrixClip->keyframes[frame][bone] = matrix;
                }
            } else {
                float values[10] = {};
                for (float& value : values) {
                    std::memcpy(&value, data.data() + offset, sizeof(float));
                    offset += sizeof(float);
                }

                Transform& transform = animation.keyframePoses[frame][static_cast<int>(bone)];
                transform.translation = {values[0], values[1], values[2]};
                transform.rotation = {values[3], values[4], values[5], values[6]};
                transform.scale = {values[7], values[8], values[9]};
            }
        }
    }

    if (version == kTracksVersionTrs) {
        for (int frame = 0; frame < animation.keyframeCount; ++frame) {
            globalizePoseFromParentJoints(skeleton, animation.keyframePoses[frame]);
        }
    }

    return true;
}

void unloadModelAnimation(ModelAnimation& animation) {
    if (animation.keyframePoses != nullptr) {
        for (int frame = 0; frame < animation.keyframeCount; ++frame) {
            RL_FREE(animation.keyframePoses[frame]);
        }
        RL_FREE(animation.keyframePoses);
    }

    animation = {};
}

void unloadAnimBank(AnimBank& bank) {
    for (ModelAnimation& clip : bank.clips) {
        unloadModelAnimation(clip);
    }
    bank = {};
}

bool buildAnimBank(
    const AnimAsset& asset,
    const SkeletonAsset& skeleton,
    const std::function<std::vector<std::byte>(const AnimClip&)>& readTracks,
    AnimBank& bank) {
    unloadAnimBank(bank);
    bank.skeletonId = asset.skeletonId;

    for (const AnimClip& clip : asset.clips) {
        const std::vector<std::byte> trackBytes = readTracks(clip);
        if (trackBytes.empty()) {
            continue;
        }

        ModelAnimation animation{};
        std::memset(animation.name, 0, sizeof(animation.name));
        std::strncpy(animation.name, clip.name.c_str(), sizeof(animation.name) - 1);

        AnimClipMatrices matrixClip{};
        if (!loadTracksToModelAnimation(trackBytes, skeleton, animation, &matrixClip)) {
            unloadModelAnimation(animation);
            continue;
        }

        bank.clipIndexByName[clip.name] = bank.clips.size();
        bank.clipMeta.push_back(clip);
        bank.clips.push_back(animation);
        bank.matrixClips.push_back(std::move(matrixClip));
    }

    return !bank.clips.empty();
}

}
