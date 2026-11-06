#pragma once

#include "map/bsp.hpp"
#include "map/map_areas.hpp"

#include <cstdint>
#include <vector>

namespace slopengine {

/** Dense leaf↔leaf bit matrix, the build-time and PVS1 on-disk layout. */
struct DensePvs {
    int leafCount = 0;
    int wordsPerRow = 0;
    std::vector<std::uint32_t> bits;
};

/** Compact leaf↔leaf PVS (PVS2): deduplicated rows over visible-leaf columns only. */
struct PvsFile {
    int leafCount = 0;
    int columnCount = 0;
    int wordsPerRow = 0;
    std::vector<std::int32_t> leafRow;
    std::vector<std::int32_t> leafColumn;
    std::vector<std::uint32_t> rows;

    int rowCount() const {
        return wordsPerRow > 0 ? static_cast<int>(rows.size()) / wordsPerRow : 0;
    }
};

/** Runtime map PVS blob. */
struct MapPvs {
    PvsFile pvs{};
};

inline bool pvsCanSee(const PvsFile& pvs, std::int32_t fromLeaf, std::int32_t toLeaf) {
    if (fromLeaf < 0 || toLeaf < 0 || fromLeaf >= pvs.leafCount || toLeaf >= pvs.leafCount) {
        return true;
    }
    if (pvs.leafRow.empty() || pvs.leafColumn.empty()) {
        return true;
    }
    const std::int32_t row = pvs.leafRow[static_cast<std::size_t>(fromLeaf)];
    const std::int32_t column = pvs.leafColumn[static_cast<std::size_t>(toLeaf)];
    if (row < 0 || column < 0) {
        return false;
    }
    const std::uint32_t word = pvs.rows[static_cast<std::size_t>(row) *
                                            static_cast<std::size_t>(pvs.wordsPerRow) +
                                        static_cast<std::size_t>(column >> 5)];
    return (word & (1u << (column & 31))) != 0u;
}

/** Portal-graph-participating-leaf sample for entity PVS. Feet often sit in
 *  solid floor; nudge up. A raw point sampled while standing inside a Door
 *  leaf's low ceiling must accept that leaf directly instead of nudging past
 *  it -- nudging can escape into unrelated, poorly-connected geometry above
 *  the doorway (e.g. a stray sliver leaf with no portals at all), which would
 *  return a technically "open" leaf whose PVS row sees almost nothing. See
 *  leafParticipatesInPortalGraph. */
inline std::int32_t pvsSampleLeaf(const BspTree& tree, Vector3 point) {
    static constexpr float kNudges[] = {0.0f, 0.05f, 0.25f, 0.75f, 1.5f};
    for (float dy : kNudges) {
        const std::int32_t leaf = pointLeaf(tree, {point.x, point.y + dy, point.z});
        if (leaf >= 0 && leaf < static_cast<std::int32_t>(tree.leaves.size()) &&
            leafParticipatesInPortalGraph(tree.leaves[static_cast<std::size_t>(leaf)].contents)) {
            return leaf;
        }
    }
    return -1;
}

inline bool pvsVisiblePointFromLeaf(
    const BspTree& tree,
    const PvsFile& pvs,
    std::int32_t fromLeaf,
    Vector3 to,
    const AreaGate* gate = nullptr) {
    const std::int32_t b = pvsSampleLeaf(tree, to);
    if (fromLeaf < 0 || b < 0) {
        return true;
    }
    if (gate != nullptr && !areaGateAllows(*gate, tree, b)) {
        return false;
    }
    return pvsCanSee(pvs, fromLeaf, b);
}

inline bool pvsVisiblePoints(
    const BspTree& tree,
    const PvsFile& pvs,
    Vector3 from,
    Vector3 to,
    const AreaGate* gate = nullptr) {
    return pvsVisiblePointFromLeaf(tree, pvs, pvsSampleLeaf(tree, from), to, gate);
}

inline bool pvsVisibleBoxFromLeaf(
    const BspTree& tree,
    const PvsFile& pvs,
    std::int32_t a,
    const BoundingBox& box,
    const AreaGate* gate = nullptr) {
    if (a < 0) {
        return true;
    }
    const Vector3 samples[] = {
        {(box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f,
         (box.min.z + box.max.z) * 0.5f},
        {box.min.x, box.min.y, box.min.z},
        {box.min.x, box.min.y, box.max.z},
        {box.min.x, box.max.y, box.min.z},
        {box.min.x, box.max.y, box.max.z},
        {box.max.x, box.min.y, box.min.z},
        {box.max.x, box.min.y, box.max.z},
        {box.max.x, box.max.y, box.min.z},
        {box.max.x, box.max.y, box.max.z},
    };
    for (const Vector3& point : samples) {
        const std::int32_t b = pvsSampleLeaf(tree, point);
        if (b < 0) {
            return true;
        }
        if (gate != nullptr && !areaGateAllows(*gate, tree, b)) {
            continue;
        }
        if (pvsCanSee(pvs, a, b)) {
            return true;
        }
    }
    return false;
}

inline bool pvsVisibleBox(
    const BspTree& tree,
    const PvsFile& pvs,
    Vector3 from,
    const BoundingBox& box,
    const AreaGate* gate = nullptr) {
    return pvsVisibleBoxFromLeaf(tree, pvs, pvsSampleLeaf(tree, from), box, gate);
}

inline std::vector<std::int32_t> pvsBoxSampleLeaves(const BspTree& tree, const BoundingBox& box) {
    const Vector3 samples[] = {
        {(box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f,
         (box.min.z + box.max.z) * 0.5f},
        {box.min.x, box.min.y, box.min.z},
        {box.min.x, box.min.y, box.max.z},
        {box.min.x, box.max.y, box.min.z},
        {box.min.x, box.max.y, box.max.z},
        {box.max.x, box.min.y, box.min.z},
        {box.max.x, box.min.y, box.max.z},
        {box.max.x, box.max.y, box.min.z},
        {box.max.x, box.max.y, box.max.z},
    };
    std::vector<std::int32_t> leaves;
    for (const Vector3& point : samples) {
        const std::int32_t leaf = pvsSampleLeaf(tree, point);
        if (leaf < 0) {
            return {};
        }
        bool seen = false;
        for (std::int32_t known : leaves) {
            if (known == leaf) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            leaves.push_back(leaf);
        }
    }
    return leaves;
}

inline bool pvsVisibleLeavesFromLeaf(
    const BspTree& tree,
    const PvsFile& pvs,
    std::int32_t a,
    const std::vector<std::int32_t>& leaves,
    const AreaGate* gate = nullptr) {
    if (a < 0 || leaves.empty()) {
        return true;
    }
    for (std::int32_t b : leaves) {
        if (gate != nullptr && !areaGateAllows(*gate, tree, b)) {
            continue;
        }
        if (pvsCanSee(pvs, a, b)) {
            return true;
        }
    }
    return false;
}

/** Number of leaves visible from @p fromLeaf. */
int pvsVisibleCount(const PvsFile& pvs, std::int32_t fromLeaf);

/** Compacts a dense matrix: drops all-zero rows and columns, shares identical rows. */
PvsFile compactPvs(const DensePvs& dense);

/**
 * Builds leaf↔leaf PVS from sealed BSP portals (one cluster per open leaf).
 * When @p exteriorEmpty is non-null (analyzeMapHull), only sealed-interior open
 * leaves are used as PVS sources. @p fast stops after the plane-test base pass,
 * which is looser but finishes in seconds on maps the full clip cannot.
 */
PvsFile buildPvs(
    const BspTree& tree,
    const std::vector<std::uint8_t>* exteriorEmpty = nullptr,
    bool fast = false);

}
