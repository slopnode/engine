#include "map/pvs.hpp"

#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <limits>
#include <thread>
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

// Every source leaf floods on its own thread, and a symmetric bit write can
// land in a row owned by a *different* thread's source (B discovering A
// touches row A, which A's own thread is concurrently writing to). Every
// write during the parallel flood therefore has to be atomic; word-level
// fetch_or via atomic_ref is cheap enough that it's simplest to always use
// it rather than special-case the "own row" writes.
void pvsSetBitAtomic(PvsFile& pvs, int fromLeaf, int toLeaf) {
    if (fromLeaf < 0 || toLeaf < 0 || fromLeaf >= pvs.leafCount || toLeaf >= pvs.leafCount) {
        return;
    }
    const std::size_t index =
        static_cast<std::size_t>(fromLeaf * pvs.wordsPerRow + (toLeaf >> 5));
    std::atomic_ref<std::uint32_t> word(pvs.bits[index]);
    word.fetch_or(1u << (toLeaf & 31), std::memory_order_relaxed);
}

void pvsSetBitSymmetricAtomic(PvsFile& pvs, int a, int b) {
    pvsSetBitAtomic(pvs, a, b);
    pvsSetBitAtomic(pvs, b, a);
}

struct FloodWorkItem {
    int leaf = -1;
    int cameFromLeaf = -1;
    PvsPlane clipPlane{};
    std::vector<Vector3> pass;
};

// Recursing one call per portal hop used to blow the stack on Windows: a
// std::thread there defaults to a ~1MiB stack (vs. glibc's 8MiB pthread
// default on Linux), and a portal-dense map can chain thousands of hops deep
// before a source leaf's flood bottoms out. An explicit worklist moves that
// state off the call stack and onto the heap, where it belongs — `stack` is
// reused across every source leaf a worker processes so its capacity is paid
// for once, not on every flood. LIFO push/pop still walks depth-first, just
// with sibling portals visited in reverse order versus the old recursive
// calls; the area-relaxation below re-expands a leaf whenever a wider path
// reaches it regardless of visitation order, so the result is unaffected.
void floodFromSource(
    const std::vector<DirectedPortal>& portals,
    const std::vector<std::vector<LeafPortalRef>>& leafPortals,
    PvsFile& pvs,
    int sourceLeaf,
    std::vector<float>& bestPassArea,
    std::vector<FloodWorkItem>& stack) {
    stack.clear();
    for (const LeafPortalRef& ref : leafPortals[static_cast<std::size_t>(sourceLeaf)]) {
        const DirectedPortal& portal = portals[static_cast<std::size_t>(ref.portalIndex)];
        stack.push_back(FloodWorkItem{portal.toLeaf, portal.fromLeaf, portal.plane, portal.winding});
    }

    while (!stack.empty()) {
        FloodWorkItem item = std::move(stack.back());
        stack.pop_back();

        pvsSetBitSymmetricAtomic(pvs, sourceLeaf, item.leaf);
        if (item.leaf < 0 || item.leaf >= static_cast<int>(leafPortals.size())) {
            continue;
        }

        const float area = windingArea(item.pass);
        if (area <= bestPassArea[static_cast<std::size_t>(item.leaf)] + kPvsEps) {
            continue;
        }
        bestPassArea[static_cast<std::size_t>(item.leaf)] = area;

        for (const LeafPortalRef& ref : leafPortals[static_cast<std::size_t>(item.leaf)]) {
            if (ref.otherLeaf == item.cameFromLeaf) {
                continue;
            }
            const DirectedPortal& next = portals[static_cast<std::size_t>(ref.portalIndex)];
            std::vector<Vector3> clipped = clipPolygonAgainstPlane(next.winding, item.clipPlane, true);
            if (clipped.size() < 3) {
                continue;
            }
            stack.push_back(FloodWorkItem{next.toLeaf, next.fromLeaf, next.plane, std::move(clipped)});
        }
    }
}

bool isPvsSourceLeaf(
    const BspTree& tree,
    int leaf,
    const std::vector<std::uint8_t>* exteriorEmpty) {
    if (leaf < 0 || leaf >= static_cast<int>(tree.leaves.size())) {
        return false;
    }
    if (!leafParticipatesInPortalGraph(tree.leaves[static_cast<std::size_t>(leaf)].contents)) {
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

    // Per-source flood cost varies wildly (an open source leaf can flood the
    // whole level; a closet floods almost nothing), so leaves are handed out
    // one at a time from a shared counter rather than split into static
    // ranges — a fixed chunk-per-thread split would leave fast threads idle
    // while one thread is still stuck on a big open area.
    const unsigned hwThreads = std::max(1u, std::thread::hardware_concurrency());
    const int threadCount = std::min<int>(static_cast<int>(hwThreads), n);

    std::atomic<int> nextSource{0};
    auto worker = [&]() {
        std::vector<float> bestPassArea(static_cast<std::size_t>(n), 0.0f);
        std::vector<FloodWorkItem> stack;
        for (;;) {
            const int source = nextSource.fetch_add(1, std::memory_order_relaxed);
            if (source >= n) {
                break;
            }
            if (!isPvsSourceLeaf(tree, source, exteriorEmpty)) {
                continue;
            }
            pvsSetBitAtomic(pvs, source, source);
            std::fill(bestPassArea.begin(), bestPassArea.end(), 0.0f);
            bestPassArea[static_cast<std::size_t>(source)] = std::numeric_limits<float>::infinity();
            floodFromSource(portals, leafPortals, pvs, source, bestPassArea, stack);
        }
    };

    if (threadCount <= 1) {
        worker();
    } else {
        std::vector<std::thread> workers;
        workers.reserve(static_cast<std::size_t>(threadCount));
        for (int t = 0; t < threadCount; ++t) {
            workers.emplace_back(worker);
        }
        for (std::thread& t : workers) {
            t.join();
        }
    }

    TraceLog(LOG_INFO, "slopvis: flooded %d leaves using %d threads", n, threadCount);

    return pvs;
}

}
