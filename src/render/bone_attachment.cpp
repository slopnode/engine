#include "render/bone_attachment.hpp"

#include <cstring>

#include <raymath.h>

namespace slopengine {

int findModelBoneIndex(const Model& model, std::string_view name) {
    if (model.skeleton.bones == nullptr || name.empty()) {
        return -1;
    }
    for (int index = 0; index < model.skeleton.boneCount; ++index) {
        const char* boneName = model.skeleton.bones[index].name;
        if (std::strncmp(boneName, name.data(), name.size()) == 0 && boneName[name.size()] == '\0') {
            return index;
        }
    }
    return -1;
}

Transform composeBoneAttachment(const Transform& bone, const BoneAttachment& attachment) {
    const Vector3 scaledOffset{
        attachment.offset.x * bone.scale.x,
        attachment.offset.y * bone.scale.y,
        attachment.offset.z * bone.scale.z,
    };
    Transform out{};
    out.translation = Vector3Add(bone.translation, Vector3RotateByQuaternion(scaledOffset, bone.rotation));
    out.rotation = QuaternionNormalize(QuaternionMultiply(bone.rotation, attachment.rotation));
    out.scale = {
        attachment.scale.x * bone.scale.x,
        attachment.scale.y * bone.scale.y,
        attachment.scale.z * bone.scale.z,
    };
    return out;
}

}
