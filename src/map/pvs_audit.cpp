#include "map/pvs_audit.hpp"

#include "map/bsp_polygon.hpp"

#include <cmath>
#include <cstdio>
#include <random>

namespace slopengine {

namespace {

enum class SegmentClass {
    Open,
    Glass,
    Blocked,
};

SegmentClass classifySegment(const BspTree& tree, Vector3 a, Vector3 b, bool& crossesDoor) {
    crossesDoor = false;
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float dz = b.z - a.z;
    const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
    const int steps = std::max(2, static_cast<int>(length / 0.05f) + 1);
    bool glass = false;
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const Vector3 p{a.x + dx * t, a.y + dy * t, a.z + dz * t};
        const std::int32_t leaf = pointLeaf(tree, p);
        if (leaf < 0) {
            return SegmentClass::Blocked;
        }
        const std::uint32_t contents = tree.leaves[static_cast<std::size_t>(leaf)].contents;
        if ((contents & BspContents::Door) != 0) {
            crossesDoor = true;
        }
        if (leafParticipatesInPortalGraph(contents)) {
            continue;
        }
        if ((contents & BspContents::Glass) != 0 && (contents & BspContents::Solid) == 0) {
            glass = true;
            continue;
        }
        if (contents == BspContents::Glass) {
            glass = true;
            continue;
        }
        return SegmentClass::Blocked;
    }
    return glass ? SegmentClass::Glass : SegmentClass::Open;
}

struct LeafStep {
    std::int32_t leaf = -1;
    Vector3 at{};
};

std::vector<LeafStep> leafChain(const BspTree& tree, Vector3 a, Vector3 b) {
    std::vector<LeafStep> chain;
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float dz = b.z - a.z;
    const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
    const int steps = std::max(2, static_cast<int>(length / 0.02f) + 1);
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const Vector3 p{a.x + dx * t, a.y + dy * t, a.z + dz * t};
        const std::int32_t leaf = pointLeaf(tree, p);
        if (chain.empty() || chain.back().leaf != leaf) {
            chain.push_back({leaf, p});
        }
    }
    return chain;
}

bool randomPointInLeaf(const BspTree& tree, std::int32_t leaf, std::mt19937& rng, Vector3& out) {
    const BspLeaf& l = tree.leaves[static_cast<std::size_t>(leaf)];
    std::uniform_real_distribution<float> ux(l.mins.x, l.maxs.x);
    std::uniform_real_distribution<float> uy(l.mins.y, l.maxs.y);
    std::uniform_real_distribution<float> uz(l.mins.z, l.maxs.z);
    for (int attempt = 0; attempt < 48; ++attempt) {
        const Vector3 p{ux(rng), uy(rng), uz(rng)};
        if (pointLeaf(tree, p) == leaf) {
            out = p;
            return true;
        }
    }
    return false;
}

}

bool segmentOpenThroughTree(const BspTree& tree, Vector3 a, Vector3 b) {
    bool crossesDoor = false;
    return classifySegment(tree, a, b, crossesDoor) != SegmentClass::Blocked;
}

ViewAuditResult auditView(const BspTree& tree, const PvsFile& pvs, Vector3 camera) {
    ViewAuditResult result;
    result.cameraLeaf = pvsSampleLeaf(tree, camera);
    if (result.cameraLeaf < 0) {
        return result;
    }
    char buffer[240];
    for (const BspSurfaceFace& face : tree.surfaceFaces) {
        if (face.vertices.size() < 3) {
            continue;
        }
        ++result.facesTested;
        std::vector<Vector3> nudged;
        nudged.reserve(face.vertices.size());
        for (const Vector3& v : face.vertices) {
            nudged.push_back({v.x + face.normal.x * 0.01f, v.y + face.normal.y * 0.01f, v.z + face.normal.z * 0.01f});
        }
        std::vector<std::int32_t> leaves;
        collectPolygonLeaves(tree, nudged, leaves);
        if (leaves.empty()) {
            ++result.facesWithoutLeaves;
        }
        bool pvsVisible = leaves.empty();
        for (std::int32_t leaf : leaves) {
            if (pvsCanSee(pvs, result.cameraLeaf, leaf)) {
                pvsVisible = true;
                break;
            }
        }
        bool marchVisible = false;
        Vector3 seenAt{};
        const Vector3& a = face.vertices[0];
        for (std::size_t i = 1; i + 1 < face.vertices.size() && !marchVisible; ++i) {
            const Vector3& b = face.vertices[i];
            const Vector3& c = face.vertices[i + 1];
            for (int u = 0; u <= 4 && !marchVisible; ++u) {
                for (int v = 0; u + v <= 4; ++v) {
                    const float fu = static_cast<float>(u) / 4.0f;
                    const float fv = static_cast<float>(v) / 4.0f;
                    const float fw = 1.0f - fu - fv;
                    const Vector3 point{
                        a.x * fw + b.x * fu + c.x * fv + face.normal.x * 0.02f,
                        a.y * fw + b.y * fu + c.y * fv + face.normal.y * 0.02f,
                        a.z * fw + b.z * fu + c.z * fv + face.normal.z * 0.02f,
                    };
                    bool crossesDoor = false;
                    if (classifySegment(tree, camera, point, crossesDoor) != SegmentClass::Blocked) {
                        marchVisible = true;
                        seenAt = point;
                        break;
                    }
                }
            }
        }
        if (marchVisible) {
            ++result.facesVisibleByMarch;
            if (!pvsVisible) {
                ++result.facesCulledWrongly;
                if (result.examples.size() < 16) {
                    std::snprintf(
                        buffer,
                        sizeof(buffer),
                        "face '%s' (%s) visible at (%.1f %.1f %.1f) but its %d leaves are all hidden from camera leaf %d",
                        face.id.c_str(),
                        face.material.c_str(),
                        seenAt.x, seenAt.y, seenAt.z,
                        static_cast<int>(leaves.size()),
                        result.cameraLeaf);
                    result.examples.emplace_back(buffer);
                }
            }
        }
    }
    return result;
}

PvsAuditResult auditPvs(
    const BspTree& tree,
    const PvsFile& pvs,
    int samplePairs,
    unsigned seed,
    const std::vector<std::uint8_t>* exteriorEmpty,
    const MapAreas* areas) {
    PvsAuditResult result;
    const auto interior = [&](std::int32_t leaf) {
        return exteriorEmpty == nullptr || static_cast<std::size_t>(leaf) >= exteriorEmpty->size() ||
            (*exteriorEmpty)[static_cast<std::size_t>(leaf)] == 0;
    };
    std::vector<std::int32_t> open;
    for (std::size_t i = 0; i < tree.leaves.size(); ++i) {
        if (leafParticipatesInPortalGraph(tree.leaves[i].contents) && interior(static_cast<std::int32_t>(i))) {
            open.push_back(static_cast<std::int32_t>(i));
        }
    }
    if (open.size() < 2) {
        return result;
    }

    for (const BspPortal& portal : tree.portals) {
        if (!interior(portal.leafA) || !interior(portal.leafB)) {
            continue;
        }
        ++result.portalPairs;
        if (!pvsCanSee(pvs, portal.leafA, portal.leafB) || !pvsCanSee(pvs, portal.leafB, portal.leafA)) {
            ++result.portalPairsBlind;
        }
    }

    char buffer[240];
    for (std::int32_t leaf : open) {
        const std::size_t li = static_cast<std::size_t>(leaf);
        const bool blind = li >= pvs.leafRow.size() || pvs.leafRow[li] < 0;
        if (blind) {
            ++result.interiorBlindLeaves;
            if (result.blindExamples.size() < 10) {
                const BspLeaf& l = tree.leaves[li];
                std::snprintf(
                    buffer,
                    sizeof(buffer),
                    "interior leaf %d has no PVS row, bounds (%.1f %.1f %.1f)-(%.1f %.1f %.1f)",
                    leaf, l.mins.x, l.mins.y, l.mins.z, l.maxs.x, l.maxs.y, l.maxs.z);
                result.blindExamples.emplace_back(buffer);
            }
        }
    }
    if (exteriorEmpty != nullptr) {
        for (std::size_t i = 0; i < tree.leaves.size() && i < exteriorEmpty->size(); ++i) {
            if ((*exteriorEmpty)[i] == 0 || !leafParticipatesInPortalGraph(tree.leaves[i].contents)) {
                continue;
            }
            const BspLeaf& l = tree.leaves[i];
            if (l.maxs.y > -1.4f && l.mins.y < 30.0f) {
                ++result.exteriorAbovePlayable;
                if (result.blindExamples.size() < 10) {
                    std::snprintf(
                        buffer,
                        sizeof(buffer),
                        "exterior leaf %d in the playable height range, bounds (%.1f %.1f %.1f)-(%.1f %.1f %.1f)",
                        static_cast<int>(i), l.mins.x, l.mins.y, l.mins.z, l.maxs.x, l.maxs.y, l.maxs.z);
                    result.blindExamples.emplace_back(buffer);
                }
            }
        }
    }

    std::vector<std::vector<std::int32_t>> portalLinks(tree.leaves.size());
    for (const BspPortal& portal : tree.portals) {
        if (portal.leafA >= 0 && portal.leafB >= 0) {
            portalLinks[static_cast<std::size_t>(portal.leafA)].push_back(portal.leafB);
            portalLinks[static_cast<std::size_t>(portal.leafB)].push_back(portal.leafA);
        }
    }
    const auto portalBetween = [&](std::int32_t x, std::int32_t y) {
        for (std::int32_t n : portalLinks[static_cast<std::size_t>(x)]) {
            if (n == y) {
                return true;
            }
        }
        return false;
    };

    std::mt19937 rng(seed);
    std::uniform_int_distribution<std::size_t> pick(0, open.size() - 1);
    for (int n = 0; n < samplePairs; ++n) {
        const std::int32_t a = open[pick(rng)];
        const std::int32_t b = open[pick(rng)];
        if (a == b) {
            continue;
        }
        Vector3 pa{};
        Vector3 pb{};
        if (!randomPointInLeaf(tree, a, rng, pa) || !randomPointInLeaf(tree, b, rng, pb)) {
            continue;
        }
        ++result.pairsTested;
        bool crossesDoor = false;
        const SegmentClass seg = classifySegment(tree, pa, pb, crossesDoor);
        if (seg == SegmentClass::Blocked) {
            continue;
        }
        const bool sees = pvsCanSee(pvs, a, b);
        if (seg == SegmentClass::Open && !crossesDoor && areas != nullptr &&
            static_cast<std::size_t>(a) < areas->leafArea.size() &&
            static_cast<std::size_t>(b) < areas->leafArea.size()) {
            ++result.areaPairs;
            if (areas->leafArea[static_cast<std::size_t>(a)] != areas->leafArea[static_cast<std::size_t>(b)]) {
                ++result.areaMismatch;
                if (result.areaExamples.size() < 8) {
                    std::snprintf(
                        buffer,
                        sizeof(buffer),
                        "leaf %d (%.1f %.1f %.1f) area %d and leaf %d (%.1f %.1f %.1f) area %d see each other with no door between",
                        a, pa.x, pa.y, pa.z, areas->leafArea[static_cast<std::size_t>(a)],
                        b, pb.x, pb.y, pb.z, areas->leafArea[static_cast<std::size_t>(b)]);
                    result.areaExamples.emplace_back(buffer);
                }
            }
        }
        if (seg == SegmentClass::Open) {
            ++result.openPairs;
            if (!sees) {
                ++result.falseInvisible;
                const std::vector<LeafStep> chain = leafChain(tree, pa, pb);
                for (std::size_t i = 1; i < chain.size(); ++i) {
                    const std::int32_t x = chain[i - 1].leaf;
                    const std::int32_t y = chain[i].leaf;
                    if (x < 0 || y < 0 || portalBetween(x, y)) {
                        continue;
                    }
                    ++result.missingPortals;
                    if (result.missingPortalExamples.size() < 12) {
                        const BspLeaf& lx = tree.leaves[static_cast<std::size_t>(x)];
                        const BspLeaf& ly = tree.leaves[static_cast<std::size_t>(y)];
                        std::snprintf(
                            buffer,
                            sizeof(buffer),
                            "no portal between leaf %d (%.1f %.1f %.1f)-(%.1f %.1f %.1f) c=%u and leaf %d (%.1f %.1f %.1f)-(%.1f %.1f %.1f) c=%u crossing at (%.2f %.2f %.2f)",
                            x, lx.mins.x, lx.mins.y, lx.mins.z, lx.maxs.x, lx.maxs.y, lx.maxs.z, lx.contents,
                            y, ly.mins.x, ly.mins.y, ly.mins.z, ly.maxs.x, ly.maxs.y, ly.maxs.z, ly.contents,
                            chain[i].at.x, chain[i].at.y, chain[i].at.z);
                        result.missingPortalExamples.emplace_back(buffer);
                    }
                    break;
                }
                if (result.examples.size() < 12) {
                    std::snprintf(
                        buffer,
                        sizeof(buffer),
                        "leaf %d (%.1f %.1f %.1f) cannot see leaf %d (%.1f %.1f %.1f) through open space",
                        a, pa.x, pa.y, pa.z, b, pb.x, pb.y, pb.z);
                    result.examples.emplace_back(buffer);
                }
            }
        } else {
            ++result.openThroughGlass;
            if (!sees) {
                ++result.falseInvisibleThroughGlass;
            }
        }
    }
    return result;
}

}
