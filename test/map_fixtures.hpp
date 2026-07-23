#pragma once

#include "map/brush.hpp"
#include "map/bsp.hpp"
#include "map/bsp_analyze.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace slopengine {
namespace mapfixtures {

inline std::function<std::string()> idAllocator(std::string prefix) {
    return [prefix, n = 0]() mutable {
        return prefix + std::to_string(n++);
    };
}

/** Six hull slabs forming a sealed hollow room. Interior roughly (-1.75,0,-1.75)..(1.75,2.5,1.75). */
inline std::vector<Brush> sealedHollowRoom() {
    Brush shell = makeBrushBox(
        "shell",
        {-2.0f, -0.25f, -2.0f},
        {2.0f, 2.75f, 2.0f},
        "mat/a",
        {});
    return hollowBrushBox(shell, 0.25f, idAllocator("wall-"));
}

/** Same as sealedHollowRoom with the east wall removed (leaks to exterior). */
inline std::vector<Brush> leakyHollowRoom() {
    std::vector<Brush> brushes = sealedHollowRoom();
    if (!brushes.empty()) {
        brushes.pop_back();
    }
    return brushes;
}

/**
 * Sealed shell with an east-wall opening filled by a thin window brush
 * (same topology class as packages/base/maps/bsp-smoke).
 */
inline std::vector<Brush> sealedRoomWithWindow() {
    std::vector<Brush> brushes;
    brushes.push_back(makeBrushBox(
        "floor",
        {-2.0f, -0.25f, -2.0f},
        {2.0f, 0.0f, 2.0f},
        "mat/a",
        {}));
    brushes.push_back(makeBrushBox(
        "ceiling",
        {-2.0f, 2.5f, -2.0f},
        {2.0f, 2.75f, 2.0f},
        "mat/a",
        {}));
    brushes.push_back(makeBrushBox(
        "wall-west",
        {-2.25f, 0.0f, -2.0f},
        {-2.0f, 2.5f, 2.0f},
        "mat/a",
        {}));
    brushes.push_back(makeBrushBox(
        "wall-east-n",
        {2.0f, 0.0f, -2.0f},
        {2.25f, 2.5f, -0.5f},
        "mat/a",
        {}));
    brushes.push_back(makeBrushBox(
        "wall-east-s",
        {2.0f, 0.0f, 0.5f},
        {2.25f, 2.5f, 2.0f},
        "mat/a",
        {}));
    brushes.push_back(makeBrushBox(
        "wall-east-lintel",
        {2.0f, 2.0f, -0.5f},
        {2.25f, 2.5f, 0.5f},
        "mat/a",
        {}));
    brushes.push_back(makeBrushBox(
        "wall-east-sill",
        {2.0f, 0.0f, -0.5f},
        {2.25f, 0.5f, 0.5f},
        "mat/a",
        {}));
    brushes.push_back(makeBrushBox(
        "window-east",
        {2.0f, 0.5f, -0.5f},
        {2.08f, 2.0f, 0.5f},
        "mat/a",
        {},
        BrushRole::Window));
    brushes.push_back(makeBrushBox(
        "wall-north",
        {-2.0f, 0.0f, -2.25f},
        {2.0f, 2.5f, -2.0f},
        "mat/a",
        {}));
    brushes.push_back(makeBrushBox(
        "wall-south",
        {-2.0f, 0.0f, 2.0f},
        {2.0f, 2.5f, 2.25f},
        "mat/a",
        {}));
    return brushes;
}

inline Brush hintMidPlane() {
    return makeBrushBox(
        "hint-mid",
        {-0.01f, 0.0f, -2.0f},
        {0.01f, 2.5f, 2.0f},
        "mat/a",
        {},
        BrushRole::Hint);
}

/**
 * Sealed outer shell with an interior partition doorway (jambs + lintel, no window seal).
 * Topology class of games/slopdoom/maps/doors. Partition role defaults to hull (forgot detail).
 */
inline std::vector<Brush> sealedRoomWithInteriorDoorway(BrushRole partitionRole = BrushRole::Hull) {
    std::vector<Brush> brushes;
    brushes.push_back(makeBrushBox(
        "floor",
        {-4.0f, -0.25f, -6.0f},
        {4.0f, 0.0f, 6.0f},
        "mat/a",
        {}));
    brushes.push_back(makeBrushBox(
        "ceiling",
        {-4.0f, 4.0f, -6.0f},
        {4.0f, 4.25f, 6.0f},
        "mat/a",
        {}));
    brushes.push_back(makeBrushBox(
        "wall-n",
        {-4.0f, 0.0f, -6.25f},
        {4.0f, 4.0f, -6.0f},
        "mat/a",
        {}));
    brushes.push_back(makeBrushBox(
        "wall-s",
        {-4.0f, 0.0f, 6.0f},
        {4.0f, 4.0f, 6.25f},
        "mat/a",
        {}));
    brushes.push_back(makeBrushBox(
        "wall-w",
        {-4.25f, 0.0f, -6.0f},
        {-4.0f, 4.0f, 6.0f},
        "mat/a",
        {}));
    brushes.push_back(makeBrushBox(
        "wall-e",
        {4.0f, 0.0f, -6.0f},
        {4.25f, 4.0f, 6.0f},
        "mat/a",
        {}));
    brushes.push_back(makeBrushBox(
        "partition-w",
        {-4.0f, 0.0f, -0.15f},
        {-1.0f, 4.0f, 0.15f},
        "mat/a",
        {},
        partitionRole));
    brushes.push_back(makeBrushBox(
        "partition-e",
        {1.0f, 0.0f, -0.15f},
        {4.0f, 4.0f, 0.15f},
        "mat/a",
        {},
        partitionRole));
    brushes.push_back(makeBrushBox(
        "partition-lintel",
        {-1.0f, 2.2f, -0.15f},
        {1.0f, 4.0f, 0.15f},
        "mat/a",
        {},
        partitionRole));
    return brushes;
}

inline int countOpenLeaves(const BspTree& tree) {
    int count = 0;
    for (const BspLeaf& leaf : tree.leaves) {
        if (leafIsOpen(leaf.contents)) {
            ++count;
        }
    }
    return count;
}

inline int countInteriorOpenLeaves(const BspTree& tree, const MapHullAnalysis& analysis) {
    int count = 0;
    for (std::size_t i = 0; i < tree.leaves.size(); ++i) {
        if (!leafIsOpen(tree.leaves[i].contents)) {
            continue;
        }
        if (i < analysis.exteriorEmpty.size() && analysis.exteriorEmpty[i] == 0) {
            ++count;
        }
    }
    return count;
}

inline bool hasGlassLeaf(const BspTree& tree) {
    for (const BspLeaf& leaf : tree.leaves) {
        if ((leaf.contents & BspContents::Glass) != 0) {
            return true;
        }
    }
    return false;
}

inline bool hasSolidLeaf(const BspTree& tree) {
    for (const BspLeaf& leaf : tree.leaves) {
        if ((leaf.contents & BspContents::Solid) != 0) {
            return true;
        }
    }
    return false;
}

} // namespace mapfixtures
} // namespace slopengine
