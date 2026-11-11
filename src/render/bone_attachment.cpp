#include "render/bone_attachment.hpp"

#include "assets/geo_loader.hpp"

#include <cstring>

#include <raymath.h>

namespace slopengine {

int findModelBoneIndex(const Model& model, std::string_view name) {
    if (model.skeleton.bones == nullptr || name.empty()) {
        return -1;
    }
    const int boneCount = static_cast<int>(model.skeleton.boneCount);
    for (int index = 0; index < boneCount; ++index) {
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
    out.translation = Vector3Add(out.translation, Vector3RotateByQuaternion(attachment.nudge, out.rotation));
    out.rotation = QuaternionNormalize(QuaternionMultiply(out.rotation, attachment.nudgeRotation));
    out.scale = {
        attachment.scale.x * bone.scale.x,
        attachment.scale.y * bone.scale.y,
        attachment.scale.z * bone.scale.z,
    };
    return out;
}

bool resolveBoneAttachment(
    const Model& model,
    const GeoAsset* geo,
    std::string_view point,
    Vector3 offset,
    Vector3 eulerRad,
    float scale,
    BoneAttachment& out) {
    Transform base{};
    base.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    base.scale = {1.0f, 1.0f, 1.0f};
    std::string bone{point};
    if (geo != nullptr) {
        if (const GeoAttachment* attachment = findGeoAttachment(*geo, point)) {
            base = geoAttachmentTransform(*attachment);
            bone = attachment->bone;
        }
    }
    const int boneIndex = findModelBoneIndex(model, bone);
    if (boneIndex < 0) {
        return false;
    }
    BoneAttachment extra{};
    extra.offset = offset;
    extra.rotation = QuaternionFromEuler(eulerRad.x, eulerRad.y, eulerRad.z);
    extra.scale = {scale, scale, scale};
    const Transform composed = composeBoneAttachment(base, extra);
    out = {};
    out.bone = bone;
    out.boneIndex = boneIndex;
    out.offset = composed.translation;
    out.rotation = composed.rotation;
    out.scale = composed.scale;
    return true;
}

}
