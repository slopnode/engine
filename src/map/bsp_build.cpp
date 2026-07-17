#include "map/bsp.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace slopengine {

namespace {

constexpr float kPlaneEps = 1e-4f;
constexpr float kBoundsPad = 0.5f;

struct SplitCandidate {
    std::int32_t axis = 0;
    float distance = 0.0f;
};

bool pointInsideBrush(Vector3 point, const Brush& brush) {
    return point.x > brush.mins.x + kPlaneEps && point.x < brush.maxs.x - kPlaneEps
        && point.y > brush.mins.y + kPlaneEps && point.y < brush.maxs.y - kPlaneEps
        && point.z > brush.mins.z + kPlaneEps && point.z < brush.maxs.z - kPlaneEps;
}

bool leafCenterInsideAnyHull(Vector3 mins, Vector3 maxs, const std::vector<Brush>& hulls) {
    const Vector3 center{
        0.5f * (mins.x + maxs.x),
        0.5f * (mins.y + maxs.y),
        0.5f * (mins.z + maxs.z),
    };
    for (const Brush& brush : hulls) {
        if (pointInsideBrush(center, brush)) {
            return true;
        }
    }
    return false;
}

float axisMin(Vector3 v, std::int32_t axis) {
    return (&v.x)[axis];
}

float& axisRef(Vector3& v, std::int32_t axis) {
    return (&v.x)[axis];
}

bool intervalsOverlap(float a0, float a1, float b0, float b1) {
    return a0 < b1 - kPlaneEps && b0 < a1 - kPlaneEps;
}

bool sharePortalFace(const BspLeaf& a, const BspLeaf& b) {
    for (std::int32_t axis = 0; axis < 3; ++axis) {
        const std::int32_t u = (axis + 1) % 3;
        const std::int32_t v = (axis + 2) % 3;
        const float aMax = axisMin(a.maxs, axis);
        const float aMin = axisMin(a.mins, axis);
        const float bMax = axisMin(b.maxs, axis);
        const float bMin = axisMin(b.mins, axis);

        const bool touchAFrontBBack = std::fabs(aMax - bMin) <= kPlaneEps;
        const bool touchBFrontABack = std::fabs(bMax - aMin) <= kPlaneEps;
        if (!touchAFrontBBack && !touchBFrontABack) {
            continue;
        }

        if (intervalsOverlap(axisMin(a.mins, u), axisMin(a.maxs, u), axisMin(b.mins, u), axisMin(b.maxs, u))
            && intervalsOverlap(
                   axisMin(a.mins, v), axisMin(a.maxs, v), axisMin(b.mins, v), axisMin(b.maxs, v))) {
            return true;
        }
    }
    return false;
}

bool sharedFaceQuad(
    const BspLeaf& a,
    const BspLeaf& b,
    std::array<Vector3, 4>& corners) {
    for (std::int32_t axis = 0; axis < 3; ++axis) {
        const std::int32_t u = (axis + 1) % 3;
        const std::int32_t v = (axis + 2) % 3;
        const float* aMins = &a.mins.x;
        const float* aMaxs = &a.maxs.x;
        const float* bMins = &b.mins.x;
        const float* bMaxs = &b.maxs.x;

        float plane = 0.0f;
        if (std::fabs(aMaxs[axis] - bMins[axis]) <= kPlaneEps) {
            plane = aMaxs[axis];
        } else if (std::fabs(bMaxs[axis] - aMins[axis]) <= kPlaneEps) {
            plane = bMaxs[axis];
        } else {
            continue;
        }

        const float u0 = std::max(aMins[u], bMins[u]);
        const float u1 = std::min(aMaxs[u], bMaxs[u]);
        const float v0 = std::max(aMins[v], bMins[v]);
        const float v1 = std::min(aMaxs[v], bMaxs[v]);
        if (u1 - u0 <= kPlaneEps || v1 - v0 <= kPlaneEps) {
            continue;
        }

        auto setCorner = [&](Vector3& out, float uu, float vv) {
            float* p = &out.x;
            p[axis] = plane;
            p[u] = uu;
            p[v] = vv;
        };
        setCorner(corners[0], u0, v0);
        setCorner(corners[1], u1, v0);
        setCorner(corners[2], u1, v1);
        setCorner(corners[3], u0, v1);
        return true;
    }
    return false;
}

void collectSplits(const std::vector<Brush>& hulls, std::vector<SplitCandidate>& out) {
    out.clear();
    for (const Brush& brush : hulls) {
        out.push_back({0, brush.mins.x});
        out.push_back({0, brush.maxs.x});
        out.push_back({1, brush.mins.y});
        out.push_back({1, brush.maxs.y});
        out.push_back({2, brush.mins.z});
        out.push_back({2, brush.maxs.z});
    }

    std::sort(out.begin(), out.end(), [](const SplitCandidate& a, const SplitCandidate& b) {
        if (a.axis != b.axis) {
            return a.axis < b.axis;
        }
        return a.distance < b.distance;
    });
    out.erase(
        std::unique(
            out.begin(),
            out.end(),
            [](const SplitCandidate& a, const SplitCandidate& b) {
                return a.axis == b.axis && std::fabs(a.distance - b.distance) <= kPlaneEps;
            }),
        out.end());
}

bool splitCutsBounds(const SplitCandidate& split, Vector3 mins, Vector3 maxs) {
    const float lo = axisMin(mins, split.axis);
    const float hi = axisMin(maxs, split.axis);
    return split.distance > lo + kPlaneEps && split.distance < hi - kPlaneEps;
}

std::int32_t encodeLeaf(std::int32_t leafIndex) {
    return -leafIndex - 1;
}

bool isLeafChild(std::int32_t child) {
    return child < 0;
}

std::int32_t decodeLeaf(std::int32_t child) {
    return -child - 1;
}

std::int32_t buildNode(
    BspTree& tree,
    Vector3 mins,
    Vector3 maxs,
    const std::vector<Brush>& hulls,
    const std::vector<SplitCandidate>& splits) {
    SplitCandidate chosen{};
    bool found = false;
    for (const SplitCandidate& split : splits) {
        if (!splitCutsBounds(split, mins, maxs)) {
            continue;
        }
        chosen = split;
        found = true;
        break;
    }

    if (!found) {
        BspLeaf leaf;
        leaf.mins = mins;
        leaf.maxs = maxs;
        leaf.solid = leafCenterInsideAnyHull(mins, maxs, hulls);
        const std::int32_t leafIndex = static_cast<std::int32_t>(tree.leaves.size());
        tree.leaves.push_back(std::move(leaf));
        return encodeLeaf(leafIndex);
    }

    const std::int32_t nodeIndex = static_cast<std::int32_t>(tree.nodes.size());
    tree.nodes.push_back({});

    Vector3 frontMins = mins;
    Vector3 frontMaxs = maxs;
    Vector3 backMins = mins;
    Vector3 backMaxs = maxs;
    axisRef(frontMins, chosen.axis) = chosen.distance;
    axisRef(backMaxs, chosen.axis) = chosen.distance;

    const std::int32_t front = buildNode(tree, frontMins, frontMaxs, hulls, splits);
    const std::int32_t back = buildNode(tree, backMins, backMaxs, hulls, splits);

    tree.nodes[static_cast<std::size_t>(nodeIndex)].plane = {chosen.axis, chosen.distance};
    tree.nodes[static_cast<std::size_t>(nodeIndex)].front = front;
    tree.nodes[static_cast<std::size_t>(nodeIndex)].back = back;
    return nodeIndex;
}

void buildAdjacency(BspTree& tree) {
    const std::int32_t leafCount = static_cast<std::int32_t>(tree.leaves.size());
    for (std::int32_t i = 0; i < leafCount; ++i) {
        if (tree.leaves[static_cast<std::size_t>(i)].solid) {
            continue;
        }
        for (std::int32_t j = i + 1; j < leafCount; ++j) {
            if (tree.leaves[static_cast<std::size_t>(j)].solid) {
                continue;
            }
            if (!sharePortalFace(
                    tree.leaves[static_cast<std::size_t>(i)],
                    tree.leaves[static_cast<std::size_t>(j)])) {
                continue;
            }
            tree.leaves[static_cast<std::size_t>(i)].neighbors.push_back(j);
            tree.leaves[static_cast<std::size_t>(j)].neighbors.push_back(i);
        }
    }
}

bool pointInsideBrushInclusive(Vector3 point, const Brush& brush) {
    return point.x >= brush.mins.x - kPlaneEps && point.x <= brush.maxs.x + kPlaneEps
        && point.y >= brush.mins.y - kPlaneEps && point.y <= brush.maxs.y + kPlaneEps
        && point.z >= brush.mins.z - kPlaneEps && point.z <= brush.maxs.z + kPlaneEps;
}

void orientSurfaceWinding(BspSurfaceFace& face) {
    const Vector3 windingNormal = faceNormalFromCorners(face.corners);
    const float alignment =
        windingNormal.x * face.normal.x
        + windingNormal.y * face.normal.y
        + windingNormal.z * face.normal.z;
    if (alignment < 0.0f) {
        std::swap(face.corners[1], face.corners[3]);
    }
}

void assignSurfaceMaterials(BspTree& tree, const std::vector<Brush>& hulls) {
    for (std::size_t faceIndex = 0; faceIndex < tree.surfaceFaces.size(); ++faceIndex) {
        BspSurfaceFace& face = tree.surfaceFaces[faceIndex];
        const Vector3 faceCenter{
            0.25f * (face.corners[0].x + face.corners[1].x + face.corners[2].x + face.corners[3].x),
            0.25f * (face.corners[0].y + face.corners[1].y + face.corners[2].y + face.corners[3].y),
            0.25f * (face.corners[0].z + face.corners[1].z + face.corners[2].z + face.corners[3].z),
        };
        const Vector3 solidProbe{
            faceCenter.x - face.normal.x * 0.02f,
            faceCenter.y - face.normal.y * 0.02f,
            faceCenter.z - face.normal.z * 0.02f,
        };

        const BrushFace* bestFace = nullptr;
        float bestScore = -1.0f;
        for (const Brush& brush : hulls) {
            if (!pointInsideBrushInclusive(solidProbe, brush)) {
                continue;
            }
            for (const BrushFace& brushFace : brush.faces) {
                const float alignment =
                    brushFace.normal.x * face.normal.x
                    + brushFace.normal.y * face.normal.y
                    + brushFace.normal.z * face.normal.z;
                if (alignment < 0.5f) {
                    continue;
                }
                if (alignment > bestScore) {
                    bestScore = alignment;
                    bestFace = &brushFace;
                }
            }
        }

        if (bestFace != nullptr) {
            face.id = bestFace->id;
            face.material = bestFace->material;
            face.uvShiftPixels = bestFace->uvShiftPixels;
            face.normal = bestFace->normal;
        } else if (face.id.empty()) {
            face.id = "surface/" + std::to_string(faceIndex);
            face.material = "default/unassigned";
        }

        orientSurfaceWinding(face);
    }
}

void buildSurfaceFaces(BspTree& tree, const std::vector<Brush>& hulls) {
    tree.surfaceFaces.clear();
    constexpr float kNudge = 0.005f;
    const std::int32_t leafCount = static_cast<std::int32_t>(tree.leaves.size());
    for (std::int32_t i = 0; i < leafCount; ++i) {
        const BspLeaf& emptyLeaf = tree.leaves[static_cast<std::size_t>(i)];
        if (emptyLeaf.solid) {
            continue;
        }
        const Vector3 emptyCenter{
            0.5f * (emptyLeaf.mins.x + emptyLeaf.maxs.x),
            0.5f * (emptyLeaf.mins.y + emptyLeaf.maxs.y),
            0.5f * (emptyLeaf.mins.z + emptyLeaf.maxs.z),
        };
        for (std::int32_t j = 0; j < leafCount; ++j) {
            if (!tree.leaves[static_cast<std::size_t>(j)].solid) {
                continue;
            }
            BspSurfaceFace face;
            if (!sharedFaceQuad(
                    emptyLeaf,
                    tree.leaves[static_cast<std::size_t>(j)],
                    face.corners)) {
                continue;
            }

            Vector3 faceCenter{
                0.25f * (face.corners[0].x + face.corners[1].x + face.corners[2].x + face.corners[3].x),
                0.25f * (face.corners[0].y + face.corners[1].y + face.corners[2].y + face.corners[3].y),
                0.25f * (face.corners[0].z + face.corners[1].z + face.corners[2].z + face.corners[3].z),
            };
            Vector3 toEmpty{
                emptyCenter.x - faceCenter.x,
                emptyCenter.y - faceCenter.y,
                emptyCenter.z - faceCenter.z,
            };
            const float len = std::sqrt(
                toEmpty.x * toEmpty.x + toEmpty.y * toEmpty.y + toEmpty.z * toEmpty.z);
            face.normal = faceNormalFromCorners(face.corners);
            if (len > kPlaneEps) {
                const float towardEmpty =
                    face.normal.x * toEmpty.x
                    + face.normal.y * toEmpty.y
                    + face.normal.z * toEmpty.z;
                if (towardEmpty < 0.0f) {
                    face.normal.x = -face.normal.x;
                    face.normal.y = -face.normal.y;
                    face.normal.z = -face.normal.z;
                    std::swap(face.corners[1], face.corners[3]);
                }
                const float invLen = kNudge / len;
                const Vector3 nudge{
                    toEmpty.x * invLen,
                    toEmpty.y * invLen,
                    toEmpty.z * invLen,
                };
                for (Vector3& corner : face.corners) {
                    corner.x += nudge.x;
                    corner.y += nudge.y;
                    corner.z += nudge.z;
                }
            }

            face.emptyLeaf = i;
            tree.surfaceFaces.push_back(face);
        }
    }

    assignSurfaceMaterials(tree, hulls);
}

} // namespace

BspTree buildBspFromHullBrushes(const std::vector<Brush>& brushes) {
    BspTree tree;
    std::vector<Brush> hulls;
    hulls.reserve(brushes.size());
    for (const Brush& brush : brushes) {
        if (brush.role == BrushRole::Hull) {
            hulls.push_back(brush);
        }
    }

    if (hulls.empty()) {
        return tree;
    }

    Vector3 mins{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    Vector3 maxs{
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
    };
    for (const Brush& brush : hulls) {
        mins.x = std::min(mins.x, brush.mins.x);
        mins.y = std::min(mins.y, brush.mins.y);
        mins.z = std::min(mins.z, brush.mins.z);
        maxs.x = std::max(maxs.x, brush.maxs.x);
        maxs.y = std::max(maxs.y, brush.maxs.y);
        maxs.z = std::max(maxs.z, brush.maxs.z);
    }
    mins.x -= kBoundsPad;
    mins.y -= kBoundsPad;
    mins.z -= kBoundsPad;
    maxs.x += kBoundsPad;
    maxs.y += kBoundsPad;
    maxs.z += kBoundsPad;

    tree.boundsMins = mins;
    tree.boundsMaxs = maxs;

    std::vector<SplitCandidate> splits;
    collectSplits(hulls, splits);
    tree.root = buildNode(tree, mins, maxs, hulls, splits);
    buildAdjacency(tree);
    buildSurfaceFaces(tree, hulls);
    return tree;
}

std::int32_t pointLeaf(const BspTree& tree, Vector3 point) {
    if (tree.leaves.empty()) {
        return -1;
    }

    if (point.x < tree.boundsMins.x || point.x > tree.boundsMaxs.x
        || point.y < tree.boundsMins.y || point.y > tree.boundsMaxs.y
        || point.z < tree.boundsMins.z || point.z > tree.boundsMaxs.z) {
        return -1;
    }

    std::int32_t child = tree.root;
    while (!isLeafChild(child)) {
        const BspNode& node = tree.nodes[static_cast<std::size_t>(child)];
        const float value = axisMin(point, node.plane.axis);
        child = value >= node.plane.distance ? node.front : node.back;
    }
    return decodeLeaf(child);
}

bool leafIsEmpty(const BspTree& tree, std::int32_t leafIndex) {
    if (leafIndex < 0 || leafIndex >= static_cast<std::int32_t>(tree.leaves.size())) {
        return false;
    }
    return !tree.leaves[static_cast<std::size_t>(leafIndex)].solid;
}

const std::vector<std::int32_t>& leafNeighbors(const BspTree& tree, std::int32_t leafIndex) {
    static const std::vector<std::int32_t> kEmpty;
    if (leafIndex < 0 || leafIndex >= static_cast<std::int32_t>(tree.leaves.size())) {
        return kEmpty;
    }
    return tree.leaves[static_cast<std::size_t>(leafIndex)].neighbors;
}

}
