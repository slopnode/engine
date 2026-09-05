#pragma once

#include "map/bsp.hpp"

#include <cstdint>
#include <vector>

namespace slopengine {

/** In-memory / on-disk leaf↔leaf PVS (PVS1). */
struct PvsFile {
    int leafCount = 0;
    int wordsPerRow = 0;
    std::vector<std::uint32_t> bits;
};

/** Runtime map PVS blob. */
struct MapPvs {
    PvsFile pvs{};
};

inline bool pvsCanSee(const PvsFile& pvs, std::int32_t fromLeaf, std::int32_t toLeaf) {
    if (fromLeaf < 0 || toLeaf < 0 || fromLeaf >= pvs.leafCount || toLeaf >= pvs.leafCount) {
        return true;
    }
    if (pvs.wordsPerRow <= 0 || pvs.bits.empty()) {
        return true;
    }
    const std::uint32_t word =
        pvs.bits[static_cast<std::size_t>(fromLeaf * pvs.wordsPerRow + (toLeaf >> 5))];
    return (word & (1u << (toLeaf & 31))) != 0u;
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

inline bool pvsVisiblePoints(
    const BspTree& tree,
    const PvsFile& pvs,
    Vector3 from,
    Vector3 to) {
    const std::int32_t a = pvsSampleLeaf(tree, from);
    const std::int32_t b = pvsSampleLeaf(tree, to);
    if (a < 0 || b < 0) {
        return true;
    }
    return pvsCanSee(pvs, a, b);
}

inline bool pvsVisibleBox(
    const BspTree& tree,
    const PvsFile& pvs,
    Vector3 from,
    const BoundingBox& box) {
    const std::int32_t a = pvsSampleLeaf(tree, from);
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
        if (b < 0 || pvsCanSee(pvs, a, b)) {
            return true;
        }
    }
    return false;
}

inline void pvsSetBit(PvsFile& pvs, int fromLeaf, int toLeaf) {
    if (fromLeaf < 0 || toLeaf < 0 || fromLeaf >= pvs.leafCount || toLeaf >= pvs.leafCount) {
        return;
    }
    pvs.bits[static_cast<std::size_t>(fromLeaf * pvs.wordsPerRow + (toLeaf >> 5))] |=
        (1u << (toLeaf & 31));
}

inline void pvsSetBitSymmetric(PvsFile& pvs, int a, int b) {
    pvsSetBit(pvs, a, b);
    pvsSetBit(pvs, b, a);
}

/**
 * Builds leaf↔leaf PVS from sealed BSP portals (one cluster per open leaf).
 * When @p exteriorEmpty is non-null (analyzeMapHull), only sealed-interior open
 * leaves are used as PVS sources.
 */
PvsFile buildPvs(
    const BspTree& tree,
    const std::vector<std::uint8_t>* exteriorEmpty = nullptr);

}
