#include "map/pvs.hpp"

#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace slopengine {

namespace {

constexpr float kPvsEps = 1e-4f;

struct PvsPlane {
    Vector3 normal{};
    float distance = 0.0f;
};

struct DirectedPortal {
    int fromLeaf = -1;
    int toLeaf = -1;
    std::vector<Vector3> winding;
    PvsPlane plane{};
};

struct LeafPortalRef {
    int portalIndex = -1;
    int otherLeaf = -1;
};

PvsPlane planeFromNormalPoint(Vector3 normal, Vector3 point) {
    const Vector3 n = Vector3Normalize(normal);
    return PvsPlane{n, Vector3DotProduct(n, point)};
}

bool makePlaneFromPoints(Vector3 a, Vector3 b, Vector3 c, PvsPlane& out) {
    const Vector3 ab = Vector3Subtract(b, a);
    const Vector3 ac = Vector3Subtract(c, a);
    const Vector3 n = Vector3CrossProduct(ab, ac);
    const float lenSq = Vector3LengthSqr(n);
    if (lenSq < 1e-12f) {
        return false;
    }
    out = planeFromNormalPoint(n, a);
    return true;
}

float planeDistance(const PvsPlane& plane, Vector3 p) {
    return Vector3DotProduct(plane.normal, p) - plane.distance;
}

std::vector<Vector3> clipPolygonAgainstPlane(
    const std::vector<Vector3>& poly,
    const PvsPlane& plane,
    bool keepFront) {
    if (poly.size() < 3) {
        return {};
    }
    std::vector<Vector3> out;
    out.reserve(poly.size() + 1);
    const float sign = keepFront ? 1.0f : -1.0f;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const Vector3& cur = poly[i];
        const Vector3& next = poly[(i + 1) % poly.size()];
        const float cd = sign * planeDistance(plane, cur);
        const float nd = sign * planeDistance(plane, next);
        const bool curIn = cd >= -kPvsEps;
        const bool nextIn = nd >= -kPvsEps;
        if (curIn) {
            out.push_back(cur);
        }
        if (curIn != nextIn) {
            const float denom = cd - nd;
            if (std::fabs(denom) > 1e-12f) {
                const float t = cd / denom;
                out.push_back(Vector3Lerp(cur, next, t));
            }
        }
    }
    if (out.size() < 3) {
        return {};
    }
    return out;
}

float windingArea(const std::vector<Vector3>& winding) {
    if (winding.size() < 3) {
        return 0.0f;
    }
    Vector3 accum{};
    for (std::size_t i = 1; i + 1 < winding.size(); ++i) {
        const Vector3 ab = Vector3Subtract(winding[i], winding[0]);
        const Vector3 ac = Vector3Subtract(winding[i + 1], winding[0]);
        accum = Vector3Add(accum, Vector3CrossProduct(ab, ac));
    }
    return 0.5f * Vector3Length(accum);
}

void floodFromPortal(
    const std::vector<DirectedPortal>& portals,
    const std::vector<std::vector<LeafPortalRef>>& leafPortals,
    PvsFile& pvs,
    int sourceLeaf,
    int leaf,
    const DirectedPortal& basePortal,
    const std::vector<Vector3>& pass,
    std::vector<float>& bestPassArea) {
    pvsSetBitSymmetric(pvs, sourceLeaf, leaf);
    if (leaf < 0 || leaf >= static_cast<int>(leafPortals.size())) {
        return;
    }

    const float area = windingArea(pass);
    if (area <= bestPassArea[static_cast<std::size_t>(leaf)] + kPvsEps) {
        return;
    }
    bestPassArea[static_cast<std::size_t>(leaf)] = area;

    for (const LeafPortalRef& ref : leafPortals[static_cast<std::size_t>(leaf)]) {
        if (ref.otherLeaf == basePortal.fromLeaf) {
            continue;
        }
        const DirectedPortal& next = portals[static_cast<std::size_t>(ref.portalIndex)];
        std::vector<Vector3> clipped =
            clipPolygonAgainstPlane(next.winding, basePortal.plane, true);
        if (clipped.size() < 3) {
            continue;
        }
        floodFromPortal(
            portals,
            leafPortals,
            pvs,
            sourceLeaf,
            next.toLeaf,
            next,
            clipped,
            bestPassArea);
    }
}

bool isPvsSourceLeaf(
    const BspTree& tree,
    int leaf,
    const std::vector<std::uint8_t>* exteriorEmpty) {
    if (leaf < 0 || leaf >= static_cast<int>(tree.leaves.size())) {
        return false;
    }
    if (!leafIsOpen(tree.leaves[static_cast<std::size_t>(leaf)].contents)) {
        return false;
    }
    if (exteriorEmpty != nullptr &&
        leaf < static_cast<int>(exteriorEmpty->size()) &&
        (*exteriorEmpty)[static_cast<std::size_t>(leaf)] != 0) {
        return false;
    }
    return true;
}

} // namespace

PvsFile buildPvs(const BspTree& tree, const std::vector<std::uint8_t>* exteriorEmpty) {
    PvsFile pvs;
    const int n = static_cast<int>(tree.leaves.size());
    pvs.leafCount = n;
    if (n <= 0) {
        return pvs;
    }
    pvs.wordsPerRow = (n + 31) / 32;
    pvs.bits.assign(static_cast<std::size_t>(n * pvs.wordsPerRow), 0u);

    std::vector<DirectedPortal> portals;
    portals.reserve(tree.portals.size() * 2);
    std::vector<std::vector<LeafPortalRef>> leafPortals(static_cast<std::size_t>(n));

    for (const BspPortal& portal : tree.portals) {
        if (portal.leafA < 0 || portal.leafB < 0 || portal.vertices.size() < 3) {
            continue;
        }
        if (portal.leafA >= n || portal.leafB >= n) {
            continue;
        }
        if (!leafParticipatesInPortalGraph(tree.leaves[static_cast<std::size_t>(portal.leafA)].contents)
            || !leafParticipatesInPortalGraph(tree.leaves[static_cast<std::size_t>(portal.leafB)].contents)) {
            continue;
        }

        PvsPlane plane{};
        if (!makePlaneFromPoints(portal.vertices[0], portal.vertices[1], portal.vertices[2], plane)) {
            continue;
        }
        const Vector3 centerA = leafCentroid(tree.leaves[static_cast<std::size_t>(portal.leafA)]);
        if (planeDistance(plane, centerA) > 0.0f) {
            plane.normal = Vector3Scale(plane.normal, -1.0f);
            plane.distance = -plane.distance;
        }

        DirectedPortal ab;
        ab.fromLeaf = portal.leafA;
        ab.toLeaf = portal.leafB;
        ab.winding = portal.vertices;
        ab.plane = plane;
        const int abIndex = static_cast<int>(portals.size());
        portals.push_back(std::move(ab));
        leafPortals[static_cast<std::size_t>(portal.leafA)].push_back(
            LeafPortalRef{abIndex, portal.leafB});

        DirectedPortal ba;
        ba.fromLeaf = portal.leafB;
        ba.toLeaf = portal.leafA;
        ba.winding = portal.vertices;
        std::reverse(ba.winding.begin(), ba.winding.end());
        ba.plane = PvsPlane{Vector3Scale(plane.normal, -1.0f), -plane.distance};
        const int baIndex = static_cast<int>(portals.size());
        portals.push_back(std::move(ba));
        leafPortals[static_cast<std::size_t>(portal.leafB)].push_back(
            LeafPortalRef{baIndex, portal.leafA});
    }

    std::vector<float> bestPassArea(static_cast<std::size_t>(n), 0.0f);
    for (int source = 0; source < n; ++source) {
        if (!isPvsSourceLeaf(tree, source, exteriorEmpty)) {
            continue;
        }
        pvsSetBit(pvs, source, source);
        std::fill(bestPassArea.begin(), bestPassArea.end(), 0.0f);
        bestPassArea[static_cast<std::size_t>(source)] = std::numeric_limits<float>::infinity();
        for (const LeafPortalRef& ref : leafPortals[static_cast<std::size_t>(source)]) {
            const DirectedPortal& portal = portals[static_cast<std::size_t>(ref.portalIndex)];
            floodFromPortal(
                portals,
                leafPortals,
                pvs,
                source,
                portal.toLeaf,
                portal,
                portal.winding,
                bestPassArea);
        }
    }

    return pvs;
}

}
