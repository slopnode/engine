#include "render/model_hitboxes.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <raymath.h>

namespace slopengine {

namespace {

int dominantBone(const Mesh& mesh, int vertex) {
    int best = -1;
    float bestWeight = 0.0f;
    for (int k = 0; k < 4; ++k) {
        const float weight = mesh.boneWeights[vertex * 4 + k];
        if (weight > bestWeight) {
            bestWeight = weight;
            best = mesh.boneIndices[vertex * 4 + k];
        }
    }
    return best;
}

Vector3 transformDirection(Vector3 v, const Matrix& m) {
    return {
        v.x * m.m0 + v.y * m.m4 + v.z * m.m8,
        v.x * m.m1 + v.y * m.m5 + v.z * m.m9,
        v.x * m.m2 + v.y * m.m6 + v.z * m.m10,
    };
}

bool slab(float origin, float dir, float lo, float hi, float& tNear, float& tFar) {
    if (std::fabs(dir) < 1e-9f) {
        return origin >= lo && origin <= hi;
    }
    float t0 = (lo - origin) / dir;
    float t1 = (hi - origin) / dir;
    if (t0 > t1) {
        std::swap(t0, t1);
    }
    tNear = std::max(tNear, t0);
    tFar = std::min(tFar, t1);
    return tNear <= tFar;
}

}

std::vector<BoneHitbox> buildBoneHitboxes(const Model& model, float minHalfExtent) {
    const int boneCount = model.skeleton.boneCount;
    std::vector<BoneHitbox> boxes(static_cast<std::size_t>(std::max(boneCount, 0)));
    if (boneCount <= 0 || model.meshes == nullptr) {
        return boxes;
    }
    for (int meshIndex = 0; meshIndex < model.meshCount; ++meshIndex) {
        const Mesh& mesh = model.meshes[meshIndex];
        if (mesh.vertices == nullptr || mesh.boneIndices == nullptr || mesh.boneWeights == nullptr) {
            continue;
        }
        for (int vertex = 0; vertex < mesh.vertexCount; ++vertex) {
            const int bone = dominantBone(mesh, vertex);
            if (bone < 0 || bone >= boneCount) {
                continue;
            }
            const Vector3 p{
                mesh.vertices[vertex * 3 + 0],
                mesh.vertices[vertex * 3 + 1],
                mesh.vertices[vertex * 3 + 2],
            };
            BoneHitbox& box = boxes[static_cast<std::size_t>(bone)];
            if (!box.valid) {
                box.min = p;
                box.max = p;
                box.valid = true;
            } else {
                box.min = Vector3Min(box.min, p);
                box.max = Vector3Max(box.max, p);
            }
        }
    }
    for (BoneHitbox& box : boxes) {
        if (!box.valid) {
            continue;
        }
        const Vector3 center = Vector3Scale(Vector3Add(box.min, box.max), 0.5f);
        const Vector3 half = Vector3Max(
            Vector3Scale(Vector3Subtract(box.max, box.min), 0.5f),
            {minHalfExtent, minHalfExtent, minHalfExtent});
        box.min = Vector3Subtract(center, half);
        box.max = Vector3Add(center, half);
    }
    return boxes;
}

const std::vector<BoneHitbox>& ensureModelHitboxes(const Model& model, ModelHitboxes& hitboxes) {
    if (hitboxes.source != model.meshes ||
        hitboxes.boxes.size() != model.skeleton.boneCount) {
        hitboxes.boxes = buildBoneHitboxes(model);
        hitboxes.source = model.meshes;
    }
    return hitboxes.boxes;
}

bool boneHidden(const std::vector<std::uint8_t>* hidden, int bone) {
    return hidden != nullptr && bone >= 0 && static_cast<std::size_t>(bone) < hidden->size() &&
        (*hidden)[static_cast<std::size_t>(bone)] != 0;
}

std::optional<ModelBoneHit> raycastModelBones(
    const Model& model,
    const std::vector<BoneHitbox>& boxes,
    const Matrix& world,
    const std::vector<std::uint8_t>* hidden,
    Ray ray,
    float maxDistance) {
    if (model.boneMatrices == nullptr) {
        return std::nullopt;
    }
    const int boneCount = std::min(static_cast<int>(model.skeleton.boneCount), static_cast<int>(boxes.size()));
    std::optional<ModelBoneHit> best;
    float bestDistance = maxDistance;
    for (int bone = 0; bone < boneCount; ++bone) {
        const BoneHitbox& box = boxes[static_cast<std::size_t>(bone)];
        if (!box.valid || boneHidden(hidden, bone)) {
            continue;
        }
        const Matrix toWorld = MatrixMultiply(model.boneMatrices[bone], world);
        const float det = MatrixDeterminant(toWorld);
        if (!std::isfinite(det) || std::fabs(det) < 1e-12f) {
            continue;
        }
        const Matrix toLocal = MatrixInvert(toWorld);
        const Vector3 origin = Vector3Transform(ray.position, toLocal);
        const Vector3 dir = transformDirection(ray.direction, toLocal);
        float tNear = 0.0f;
        float tFar = bestDistance;
        if (!slab(origin.x, dir.x, box.min.x, box.max.x, tNear, tFar) ||
            !slab(origin.y, dir.y, box.min.y, box.max.y, tNear, tFar) ||
            !slab(origin.z, dir.z, box.min.z, box.max.z, tNear, tFar)) {
            continue;
        }
        bestDistance = tNear;
        best = ModelBoneHit{
            bone,
            tNear,
            Vector3Add(ray.position, Vector3Scale(ray.direction, tNear)),
        };
    }
    return best;
}

std::vector<int> boneSubtree(const Model& model, int root) {
    std::vector<int> out;
    const int boneCount = model.skeleton.boneCount;
    if (root < 0 || root >= boneCount || model.skeleton.bones == nullptr) {
        return out;
    }
    std::vector<std::uint8_t> inside(static_cast<std::size_t>(boneCount), 0);
    inside[static_cast<std::size_t>(root)] = 1;
    out.push_back(root);
    bool grew = true;
    while (grew) {
        grew = false;
        for (int bone = 0; bone < boneCount; ++bone) {
            const int parent = model.skeleton.bones[bone].parent;
            if (inside[static_cast<std::size_t>(bone)] == 0 && parent >= 0 &&
                inside[static_cast<std::size_t>(parent)] != 0) {
                inside[static_cast<std::size_t>(bone)] = 1;
                out.push_back(bone);
                grew = true;
            }
        }
    }
    return out;
}

bool posedBonesBounds(
    const Model& model,
    const std::vector<BoneHitbox>& boxes,
    const Matrix& world,
    const std::vector<int>& bones,
    BoundingBox& out) {
    if (model.boneMatrices == nullptr) {
        return false;
    }
    bool any = false;
    const float inf = std::numeric_limits<float>::max();
    out = {{inf, inf, inf}, {-inf, -inf, -inf}};
    for (int bone : bones) {
        if (bone < 0 || static_cast<std::size_t>(bone) >= boxes.size() ||
            !boxes[static_cast<std::size_t>(bone)].valid) {
            continue;
        }
        const BoneHitbox& box = boxes[static_cast<std::size_t>(bone)];
        const Matrix toWorld = MatrixMultiply(model.boneMatrices[bone], world);
        for (int corner = 0; corner < 8; ++corner) {
            const Vector3 local{
                (corner & 1) != 0 ? box.max.x : box.min.x,
                (corner & 2) != 0 ? box.max.y : box.min.y,
                (corner & 4) != 0 ? box.max.z : box.min.z,
            };
            const Vector3 p = Vector3Transform(local, toWorld);
            out.min = Vector3Min(out.min, p);
            out.max = Vector3Max(out.max, p);
        }
        any = true;
    }
    return any;
}

Matrix collapsedBoneMatrix(Vector3 point) {
    Matrix m{};
    m.m12 = point.x;
    m.m13 = point.y;
    m.m14 = point.z;
    m.m15 = 1.0f;
    return m;
}

void collapseHiddenBones(
    const BoneInfo* bones,
    int boneCount,
    const std::uint8_t* hidden,
    const Matrix* globals,
    Matrix* skin) {
    if (bones == nullptr || hidden == nullptr || globals == nullptr || skin == nullptr) {
        return;
    }
    for (int bone = 0; bone < boneCount; ++bone) {
        if (hidden[bone] == 0) {
            continue;
        }
        int top = bone;
        while (bones[top].parent >= 0 && hidden[bones[top].parent] != 0) {
            top = bones[top].parent;
        }
        const Matrix& joint = globals[top];
        skin[bone] = collapsedBoneMatrix({joint.m12, joint.m13, joint.m14});
    }
}

}
