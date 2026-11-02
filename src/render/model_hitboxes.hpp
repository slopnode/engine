#pragma once

#include <raylib.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace slopengine {

/** Bind-space box around the vertices a bone carries most weight for. */
struct BoneHitbox {
    Vector3 min{0.0f, 0.0f, 0.0f};
    Vector3 max{0.0f, 0.0f, 0.0f};
    bool valid = false;
};

/** Per-bone hitboxes built lazily for a rigged Model3D, Half-Life style.
 *  @ingroup render_components
 */
struct ModelHitboxes {
    std::vector<BoneHitbox> boxes;
    const Mesh* source = nullptr;
};

/** Bones a script has hidden, collapsed to the joint of their topmost hidden ancestor.
 *  @ingroup render_components
 */
struct HiddenBones {
    std::vector<std::uint8_t> mask;
    bool dirty = true;
};

struct ModelBoneHit {
    int bone = -1;
    float distance = 0.0f;
    Vector3 point{0.0f, 0.0f, 0.0f};
};

std::vector<BoneHitbox> buildBoneHitboxes(const Model& model, float minHalfExtent = 0.01f);

/** Rebuilds @p hitboxes when the model's meshes changed. */
const std::vector<BoneHitbox>& ensureModelHitboxes(const Model& model, ModelHitboxes& hitboxes);

bool boneHidden(const std::vector<std::uint8_t>* hidden, int bone);

/** Nearest posed bone box along @p ray within @p maxDistance, from the model's skin matrices. */
std::optional<ModelBoneHit> raycastModelBones(
    const Model& model,
    const std::vector<BoneHitbox>& boxes,
    const Matrix& world,
    const std::vector<std::uint8_t>* hidden,
    Ray ray,
    float maxDistance);

/** @p root and every bone below it, parents before children. */
std::vector<int> boneSubtree(const Model& model, int root);

/** World AABB of the posed boxes of @p bones. Returns false when none is valid. */
bool posedBonesBounds(
    const Model& model,
    const std::vector<BoneHitbox>& boxes,
    const Matrix& world,
    const std::vector<int>& bones,
    BoundingBox& out);

/** Skin matrix that collapses every vertex onto @p point. */
Matrix collapsedBoneMatrix(Vector3 point);

/** Overwrites the skin matrices of hidden bones with collapses onto the joint of each one's
 *  topmost hidden ancestor, taken from @p globals (model-space bone poses). */
void collapseHiddenBones(
    const BoneInfo* bones,
    int boneCount,
    const std::uint8_t* hidden,
    const Matrix* globals,
    Matrix* skin);

}
