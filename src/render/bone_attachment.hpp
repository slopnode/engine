#pragma once

#include "assets/rigged_assets.hpp"

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
    Vector3 nudge{0.0f, 0.0f, 0.0f};
    Quaternion nudgeRotation{0.0f, 0.0f, 0.0f, 1.0f};
};

/** Returns the index of the bone named @p name in @p model's skeleton, or -1. */
int findModelBoneIndex(const Model& model, std::string_view name);

/** Composes @p bone (a model-space bone pose) with @p attachment's offset into a local transform. */
Transform composeBoneAttachment(const Transform& bone, const BoneAttachment& attachment);

/** Builds the attachment for @p point on @p model: either a bone name or, when @p geo is set,
 *  the name of one of its attachments. @p offset, @p eulerRad and @p scale apply relative to
 *  that point. Returns false when @p point names neither. */
bool resolveBoneAttachment(
    const Model& model,
    const GeoAsset* geo,
    std::string_view point,
    Vector3 offset,
    Vector3 eulerRad,
    float scale,
    BoneAttachment& out);

}
