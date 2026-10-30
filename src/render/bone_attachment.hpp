#pragma once

#include <raylib.h>

#include <string>
#include <string_view>

namespace slopengine {

/** Pins a child entity to a bone of its parent's skinned model, Half-Life attachment style:
 *  each frame the child's local transform becomes the bone's model-space pose composed with
 *  the stored offset.
 *  @ingroup render_components
 */
struct BoneAttachment {
    std::string bone;
    int boneIndex = -1;
    Vector3 offset{0.0f, 0.0f, 0.0f};
    Quaternion rotation{0.0f, 0.0f, 0.0f, 1.0f};
    Vector3 scale{1.0f, 1.0f, 1.0f};
};

/** Returns the index of the bone named @p name in @p model's skeleton, or -1. */
int findModelBoneIndex(const Model& model, std::string_view name);

/** Composes @p bone (a model-space bone pose) with @p attachment's offset into a local transform. */
Transform composeBoneAttachment(const Transform& bone, const BoneAttachment& attachment);

}
