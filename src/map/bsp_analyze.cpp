#include "map/bsp_analyze.hpp"

#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <queue>
#include <unordered_set>
#include <utility>
#include <vector>

namespace slopengine {

namespace {

constexpr float kEps = 1e-4f;

bool leafTouchesBounds(const BspLeaf& leaf, Vector3 boundsMins, Vector3 boundsMaxs) {
    return std::fabs(leaf.mins.x - boundsMins.x) <= kEps
        || std::fabs(leaf.mins.y - boundsMins.y) <= kEps
        || std::fabs(leaf.mins.z - boundsMins.z) <= kEps
        || std::fabs(leaf.maxs.x - boundsMaxs.x) <= kEps
        || std::fabs(leaf.maxs.y - boundsMaxs.y) <= kEps
        || std::fabs(leaf.maxs.z - boundsMaxs.z) <= kEps;
}

void floodExterior(const BspTree& tree, std::vector<std::uint8_t>& exteriorEmpty) {
    const std::size_t leafCount = tree.leaves.size();
    exteriorEmpty.assign(leafCount, 0);
    std::queue<std::int32_t> queue;
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(leafCount); ++i) {
        const BspLeaf& leaf = tree.leaves[static_cast<std::size_t>(i)];
        if (leaf.solid) {
            continue;
        }
        if (!leafTouchesBounds(leaf, tree.boundsMins, tree.boundsMaxs)) {
            continue;
        }
        exteriorEmpty[static_cast<std::size_t>(i)] = 1;
        queue.push(i);
    }

    while (!queue.empty()) {
        const std::int32_t leafIndex = queue.front();
        queue.pop();
        for (std::int32_t neighbor : tree.leaves[static_cast<std::size_t>(leafIndex)].neighbors) {
            if (neighbor < 0 || neighbor >= static_cast<std::int32_t>(leafCount)) {
                continue;
            }
            if (tree.leaves[static_cast<std::size_t>(neighbor)].solid) {
                continue;
            }
            if (exteriorEmpty[static_cast<std::size_t>(neighbor)] != 0) {
                continue;
            }
            exteriorEmpty[static_cast<std::size_t>(neighbor)] = 1;
            queue.push(neighbor);
        }
    }
}

bool isInteriorEmpty(
    const BspTree& tree,
    const std::vector<std::uint8_t>& exteriorEmpty,
    std::int32_t leafIndex) {
    if (leafIndex < 0 || leafIndex >= static_cast<std::int32_t>(tree.leaves.size())) {
        return false;
    }
    if (tree.leaves[static_cast<std::size_t>(leafIndex)].solid) {
        return false;
    }
    return exteriorEmpty[static_cast<std::size_t>(leafIndex)] == 0;
}

Vector3 hullCenter(const std::vector<Brush>& brushes) {
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
    bool any = false;
    for (const Brush& brush : brushes) {
        if (brush.role != BrushRole::Hull) {
            continue;
        }
        any = true;
        mins.x = std::min(mins.x, brush.mins.x);
        mins.y = std::min(mins.y, brush.mins.y);
        mins.z = std::min(mins.z, brush.mins.z);
        maxs.x = std::max(maxs.x, brush.maxs.x);
        maxs.y = std::max(maxs.y, brush.maxs.y);
        maxs.z = std::max(maxs.z, brush.maxs.z);
    }
    if (!any) {
        return {};
    }
    return Vector3{
        0.5f * (mins.x + maxs.x),
        0.5f * (mins.y + maxs.y),
        0.5f * (mins.z + maxs.z),
    };
}

float distSq(Vector3 a, Vector3 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

void buildLeakPath(
    const BspTree& tree,
    const std::vector<Brush>& brushes,
    std::vector<std::string>& leakPathFaceIds) {
    leakPathFaceIds.clear();
    const Vector3 target = hullCenter(brushes);
    std::int32_t goal = -1;
    float bestDist = std::numeric_limits<float>::max();
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(tree.leaves.size()); ++i) {
        const BspLeaf& leaf = tree.leaves[static_cast<std::size_t>(i)];
        if (leaf.solid) {
            continue;
        }
        const float d = distSq(leafCentroid(leaf), target);
        if (d < bestDist) {
            bestDist = d;
            goal = i;
        }
    }
    if (goal < 0) {
        return;
    }

    std::vector<std::int32_t> parent(tree.leaves.size(), -2);
    std::queue<std::int32_t> queue;
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(tree.leaves.size()); ++i) {
        const BspLeaf& leaf = tree.leaves[static_cast<std::size_t>(i)];
        if (leaf.solid) {
            continue;
        }
        if (!leafTouchesBounds(leaf, tree.boundsMins, tree.boundsMaxs)) {
            continue;
        }
        parent[static_cast<std::size_t>(i)] = -1;
        queue.push(i);
    }

    while (!queue.empty()) {
        const std::int32_t leafIndex = queue.front();
        queue.pop();
        if (leafIndex == goal) {
            break;
        }
        for (std::int32_t neighbor : tree.leaves[static_cast<std::size_t>(leafIndex)].neighbors) {
            if (neighbor < 0 || neighbor >= static_cast<std::int32_t>(tree.leaves.size())) {
                continue;
            }
            if (tree.leaves[static_cast<std::size_t>(neighbor)].solid) {
                continue;
            }
            if (parent[static_cast<std::size_t>(neighbor)] != -2) {
                continue;
            }
            parent[static_cast<std::size_t>(neighbor)] = leafIndex;
            queue.push(neighbor);
        }
    }

    if (parent[static_cast<std::size_t>(goal)] == -2) {
        return;
    }

    std::vector<std::int32_t> chain;
    for (std::int32_t cursor = goal; cursor >= 0; cursor = parent[static_cast<std::size_t>(cursor)]) {
        chain.push_back(cursor);
    }
    std::reverse(chain.begin(), chain.end());

    for (std::int32_t leafIndex : chain) {
        const Vector3 c = leafCentroid(tree.leaves[static_cast<std::size_t>(leafIndex)]);
        char buffer[96];
        std::snprintf(
            buffer,
            sizeof(buffer),
            "leaf:%d@(%.2f,%.2f,%.2f)",
            leafIndex,
            c.x,
            c.y,
            c.z);
        leakPathFaceIds.emplace_back(buffer);
    }
}

void collectDetailWarnings(
    const BspTree& tree,
    const std::vector<std::uint8_t>& exteriorEmpty,
    const std::vector<Brush>& brushes,
    std::vector<std::string>& warnings) {
    for (const Brush& brush : brushes) {
        if (brush.role != BrushRole::Detail) {
            continue;
        }
        const Vector3 center{
            0.5f * (brush.mins.x + brush.maxs.x),
            0.5f * (brush.mins.y + brush.maxs.y),
            0.5f * (brush.mins.z + brush.maxs.z),
        };
        const std::int32_t leafIndex = pointLeaf(tree, center);
        if (!isInteriorEmpty(tree, exteriorEmpty, leafIndex)) {
            warnings.push_back("detail '" + brush.id + "' is outside sealed hull");
        }
    }
}

} // namespace

MapHullAnalysis analyzeMapHull(const BspTree& tree, const std::vector<Brush>& brushes) {
    MapHullAnalysis analysis;
    if (tree.leaves.empty()) {
        TraceLog(LOG_WARNING, "BSP: analyze skipped (empty tree)");
        return analysis;
    }

    TraceLog(
        LOG_INFO,
        "BSP: analyze start leaves=%d surfaces=%d brushes=%d",
        static_cast<int>(tree.leaves.size()),
        static_cast<int>(tree.surfaceFaces.size()),
        static_cast<int>(brushes.size()));

    floodExterior(tree, analysis.exteriorEmpty);

    int exteriorEmpty = 0;
    int interiorEmpty = 0;
    int solidLeaves = 0;
    bool anyInteriorEmpty = false;
    for (std::size_t i = 0; i < tree.leaves.size(); ++i) {
        if (tree.leaves[i].solid) {
            ++solidLeaves;
            continue;
        }
        if (analysis.exteriorEmpty[i] == 0) {
            anyInteriorEmpty = true;
            ++interiorEmpty;
        } else {
            ++exteriorEmpty;
        }
    }
    analysis.sealed = anyInteriorEmpty;

    TraceLog(
        LOG_INFO,
        "BSP: flood exteriorEmpty=%d interiorEmpty=%d solid=%d sealed=%s",
        exteriorEmpty,
        interiorEmpty,
        solidLeaves,
        analysis.sealed ? "yes" : "no");

    if (!analysis.sealed) {
        buildLeakPath(tree, brushes, analysis.leakPathFaceIds);
        TraceLog(
            LOG_WARNING,
            "BSP: leak detected pathSteps=%d",
            static_cast<int>(analysis.leakPathFaceIds.size()));
        return analysis;
    }

    collectDetailWarnings(tree, analysis.exteriorEmpty, brushes, analysis.detailOutsideWarnings);

    TraceLog(
        LOG_INFO,
        "BSP: analyze done detailWarnings=%d (nodraw via slopvis)",
        static_cast<int>(analysis.detailOutsideWarnings.size()));
    return analysis;
}

void applyInferredNodraw(std::vector<Brush>& brushes, const MapHullAnalysis& analysis) {
    if (!analysis.sealed || analysis.inferredNodrawFaceIds.empty()) {
        return;
    }
    std::unordered_set<std::string> ids(
        analysis.inferredNodrawFaceIds.begin(),
        analysis.inferredNodrawFaceIds.end());
    for (Brush& brush : brushes) {
        for (BrushFace& face : brush.faces) {
            if (ids.contains(face.id)) {
                face.nodraw = true;
            }
        }
    }
}

}
