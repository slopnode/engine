#include "map/pvs.hpp"

#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace slopengine {

namespace {

constexpr float kPvsEps = 1e-4f;

struct PvsPlane {
    Vector3 normal{};
    float distance = 0.0f;
};

// Portal polygons live in fixed arrays so clipping never allocates; the
// cell portal merge keeps pieces apart when their hull would not fit.
constexpr int kMaxWindingVerts = 64;

struct Winding {
    int count = 0;
    Vector3 v[kMaxWindingVerts];
};

void copyWinding(Winding& dst, const Winding& src) {
    dst.count = src.count;
    std::copy(src.v, src.v + src.count, dst.v);
}

void setWinding(Winding& dst, const std::vector<Vector3>& src) {
    dst.count = static_cast<int>(std::min<std::size_t>(src.size(), kMaxWindingVerts));
    std::copy(src.begin(), src.begin() + dst.count, dst.v);
}

struct DirectedPortal {
    int fromLeaf = -1;
    int toLeaf = -1;
    Winding winding;
    PvsPlane plane{};
    Vector3 origin{};
    float radius = 0.0f;
};

void setPortalBounds(DirectedPortal& portal) {
    Vector3 sum{};
    for (int i = 0; i < portal.winding.count; ++i) {
        sum = Vector3Add(sum, portal.winding.v[i]);
    }
    portal.origin = Vector3Scale(sum, 1.0f / static_cast<float>(std::max(1, portal.winding.count)));
    portal.radius = 0.0f;
    for (int i = 0; i < portal.winding.count; ++i) {
        portal.radius = std::max(portal.radius, Vector3Distance(portal.origin, portal.winding.v[i]));
    }
}

struct LeafPortalRef {
    int portalIndex = -1;
    int otherLeaf = -1;
};

PvsPlane planeFromNormalPoint(Vector3 normal, Vector3 point) {
    const Vector3 n = Vector3Normalize(normal);
    return PvsPlane{n, Vector3DotProduct(n, point)};
}

bool polygonPlane(const std::vector<Vector3>& poly, PvsPlane& out) {
    if (poly.size() < 3) {
        return false;
    }
    Vector3 n{};
    Vector3 sum{};
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const Vector3& a = poly[i];
        const Vector3& b = poly[(i + 1) % poly.size()];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
        sum = Vector3Add(sum, a);
    }
    if (Vector3LengthSqr(n) < 1e-12f) {
        return false;
    }
    out = planeFromNormalPoint(n, Vector3Scale(sum, 1.0f / static_cast<float>(poly.size())));
    return true;
}

float planeDistance(const PvsPlane& plane, Vector3 p) {
    return Vector3DotProduct(plane.normal, p) - plane.distance;
}

// Clips @p poly to one side of @p plane in place. False once fewer than
// three vertices remain.
bool clipWinding(Winding& poly, const PvsPlane& plane, bool keepFront) {
    const int count = poly.count;
    if (count < 3) {
        poly.count = 0;
        return false;
    }
    const float sign = keepFront ? 1.0f : -1.0f;
    Vector3 out[kMaxWindingVerts];
    int outCount = 0;
    float cd = sign * planeDistance(plane, poly.v[0]);
    bool curIn = cd >= -kPvsEps;
    for (int i = 0; i < count; ++i) {
        const Vector3& cur = poly.v[i];
        const Vector3& next = poly.v[i + 1 == count ? 0 : i + 1];
        const float nd = sign * planeDistance(plane, next);
        const bool nextIn = nd >= -kPvsEps;
        if (curIn && outCount < kMaxWindingVerts) {
            out[outCount++] = cur;
        }
        if (curIn != nextIn && outCount < kMaxWindingVerts) {
            const float denom = cd - nd;
            if (std::fabs(denom) > 1e-12f) {
                out[outCount++] = Vector3Lerp(cur, next, cd / denom);
            }
        }
        cd = nd;
        curIn = nextIn;
    }
    poly.count = outCount;
    std::copy(out, out + outCount, poly.v);
    if (outCount < 3) {
        poly.count = 0;
        return false;
    }
    return true;
}

constexpr float kSeparatorEps = 1e-3f;

float maxPlaneDistance(const Winding& winding, const PvsPlane& plane) {
    float best = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < winding.count; ++i) {
        best = std::max(best, planeDistance(plane, winding.v[i]));
    }
    return best;
}

float minPlaneDistance(const Winding& winding, const PvsPlane& plane) {
    float best = std::numeric_limits<float>::infinity();
    for (int i = 0; i < winding.count; ++i) {
        best = std::min(best, planeDistance(plane, winding.v[i]));
    }
    return best;
}

// Quake's ClipToSeperators: every plane through an edge of @p source and a
// vertex of @p pass that has pass entirely in front and source entirely behind
// bounds the antipenumbra of sightlines through both. @p fn gets each such
// plane, oriented so the kept side is in front, and returns false to stop.
// @p flipClip keeps the back side instead, for the call with source and pass
// swapped. Side tests run on the unnormalised plane against an epsilon scaled
// by its length, so only the planes handed on are normalised.
template <typename Fn>
bool forEachSeparator(const Winding& source, const Winding& pass, bool flipClip, Fn&& fn) {
    const int sourceCount = source.count;
    const int passCount = pass.count;
    for (int i = 0; i < sourceCount; ++i) {
        const int l = i + 1 == sourceCount ? 0 : i + 1;
        const Vector3 v1 = Vector3Subtract(source.v[l], source.v[i]);
        for (int j = 0; j < passCount; ++j) {
            const Vector3 v2 = Vector3Subtract(pass.v[j], source.v[i]);
            const Vector3 normal = Vector3CrossProduct(v1, v2);
            const float len2 = Vector3DotProduct(normal, normal);
            if (len2 < 1e-12f) {
                continue;
            }
            const float eps2 = kSeparatorEps * kSeparatorEps * len2;
            const float dist = Vector3DotProduct(normal, pass.v[j]);

            bool flip = false;
            bool decided = false;
            for (int k = 0; k < sourceCount; ++k) {
                if (k == i || k == l) {
                    continue;
                }
                const float d = Vector3DotProduct(normal, source.v[k]) - dist;
                if (d * d > eps2) {
                    flip = d > 0.0f;
                    decided = true;
                    break;
                }
            }
            if (!decided) {
                continue;
            }
            const float sign = flip ? -1.0f : 1.0f;

            int front = 0;
            bool behind = false;
            for (int k = 0; k < passCount; ++k) {
                if (k == j) {
                    continue;
                }
                const float d = sign * (Vector3DotProduct(normal, pass.v[k]) - dist);
                if (d * d > eps2) {
                    if (d < 0.0f) {
                        behind = true;
                        break;
                    }
                    ++front;
                }
            }
            if (behind || front == 0) {
                continue;
            }

            const float scale = (flipClip ? -sign : sign) / std::sqrt(len2);
            if (!fn(PvsPlane{Vector3Scale(normal, scale), dist * scale})) {
                return false;
            }
        }
    }
    return true;
}

void collectSeparators(const Winding& source, const Winding& pass, bool flipClip, std::vector<PvsPlane>& out) {
    forEachSeparator(source, pass, flipClip, [&](const PvsPlane& plane) {
        out.push_back(plane);
        return true;
    });
}

bool clipToSeparators(const Winding& source, const Winding& pass, Winding& target, bool flipClip) {
    return forEachSeparator(source, pass, flipClip, [&](const PvsPlane& plane) {
        return clipWinding(target, plane, true);
    });
}

bool clipByPlanes(Winding& poly, const std::vector<PvsPlane>& planes) {
    for (const PvsPlane& plane : planes) {
        if (!clipWinding(poly, plane, true)) {
            return false;
        }
    }
    return true;
}

void pvsSetBitAtomic(DensePvs& pvs, int fromLeaf, int toLeaf) {
    if (fromLeaf < 0 || toLeaf < 0 || fromLeaf >= pvs.leafCount || toLeaf >= pvs.leafCount) {
        return;
    }
    const std::size_t index =
        static_cast<std::size_t>(fromLeaf) * static_cast<std::size_t>(pvs.wordsPerRow) +
        static_cast<std::size_t>(toLeaf >> 5);
    std::atomic_ref<std::uint32_t> word(pvs.bits[index]);
    word.fetch_or(1u << (toLeaf & 31), std::memory_order_relaxed);
}

void pvsSetBitSymmetricAtomic(DensePvs& pvs, int a, int b) {
    pvsSetBitAtomic(pvs, a, b);
    pvsSetBitAtomic(pvs, b, a);
}

struct PortalGraph {
    std::vector<DirectedPortal> portals;
    std::vector<std::vector<LeafPortalRef>> leafPortals;
};

inline bool testBit(const std::vector<std::uint32_t>& bits, int index) {
    return (bits[static_cast<std::size_t>(index >> 5)] & (1u << (index & 31))) != 0u;
}

inline void setBit(std::vector<std::uint32_t>& bits, int index) {
    bits[static_cast<std::size_t>(index >> 5)] |= 1u << (index & 31);
}

// Base vis: the portals a sightline leaving @p sourceIndex could possibly
// pass through, found by flooding with plane tests only. Each candidate must
// reach in front of the source plane while the source reaches behind the
// candidate's plane.
int floodMightSee(
    const PortalGraph& graph,
    int sourceIndex,
    std::vector<std::uint32_t>& mightSee,
    std::vector<int>& queue,
    std::uint32_t* leafBits) {
    std::fill(mightSee.begin(), mightSee.end(), 0u);
    const DirectedPortal& source = graph.portals[static_cast<std::size_t>(sourceIndex)];
    int count = 0;
    queue.clear();
    queue.push_back(source.toLeaf);
    if (leafBits != nullptr) {
        leafBits[source.toLeaf >> 5] |= 1u << (source.toLeaf & 31);
    }
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const int leaf = queue[head];
        for (const LeafPortalRef& ref : graph.leafPortals[static_cast<std::size_t>(leaf)]) {
            if (testBit(mightSee, ref.portalIndex)) {
                continue;
            }
            const DirectedPortal& candidate = graph.portals[static_cast<std::size_t>(ref.portalIndex)];
            if (planeDistance(source.plane, candidate.origin) < -candidate.radius ||
                planeDistance(candidate.plane, source.origin) > source.radius) {
                continue;
            }
            if (maxPlaneDistance(candidate.winding, source.plane) <= kSeparatorEps ||
                minPlaneDistance(source.winding, candidate.plane) >= -kSeparatorEps) {
                continue;
            }
            setBit(mightSee, ref.portalIndex);
            ++count;
            if (leafBits != nullptr) {
                leafBits[candidate.toLeaf >> 5] |= 1u << (candidate.toLeaf & 31);
            }
            queue.push_back(candidate.toLeaf);
        }
    }
    return count;
}

// Base vis rows are bit sets over cells, one row per directed portal when
// that fits the budget and one per cell (the union of its portals) otherwise.
struct MightSeeTable {
    bool perPortal = false;
    std::size_t words = 0;
    std::vector<std::uint32_t> bits;

    std::uint32_t* row(int portalIndex, int leaf) {
        const std::size_t index = static_cast<std::size_t>(perPortal ? portalIndex : leaf);
        return bits.data() + index * words;
    }

    const std::uint32_t* row(int portalIndex, int leaf) const {
        const std::size_t index = static_cast<std::size_t>(perPortal ? portalIndex : leaf);
        return bits.data() + index * words;
    }
};

// Passage vis (q3map2): for a portal p and each portal q leaving the cell
// behind it, the cells whose portals survive the antipenumbra of p and q.
// Rows are indexed by p and q's position among that cell's portals. Empty
// when the table is not built.
struct PassageTable {
    std::size_t words = 0;
    std::vector<std::size_t> start;
    std::vector<std::uint32_t> bits;

    bool built() const {
        return !bits.empty();
    }

    const std::uint32_t* row(int portalIndex, std::size_t refIndex) const {
        return bits.data() + (start[static_cast<std::size_t>(portalIndex)] + refIndex) * words;
    }
};

void intersectWithWords(std::vector<std::uint32_t>& might, const std::uint32_t* words) {
    for (std::size_t w = 0; w < might.size(); ++w) {
        std::atomic_ref<std::uint32_t> word(const_cast<std::uint32_t&>(words[w]));
        might[w] &= word.load(std::memory_order_relaxed);
    }
}

void intersectWithRow(std::vector<std::uint32_t>& might, const DensePvs& pvs, int leaf) {
    intersectWithWords(
        might,
        pvs.bits.data() + static_cast<std::size_t>(leaf) * static_cast<std::size_t>(pvs.wordsPerRow));
}

bool anyNew(const std::vector<std::uint32_t>& might, const std::vector<std::uint32_t>& seen) {
    for (std::size_t w = 0; w < might.size(); ++w) {
        if ((might[w] & ~seen[w]) != 0u) {
            return true;
        }
    }
    return false;
}

struct FlowFrame {
    int leaf = -1;
    int through = -1;
    std::size_t nextPortal = 0;
    PvsPlane portalPlane{};
    Winding source;
    Winding pass;
    std::vector<std::uint32_t> might;
    bool separatorsReady = false;
    std::vector<PvsPlane> separators;
};

struct FlowScratch {
    std::vector<std::uint32_t> mightSee;
    std::vector<std::uint32_t> portalVis;
    std::vector<std::uint8_t> onChain;
    std::vector<int> queue;
    std::vector<FlowFrame> frames;
    Winding target;
    Winding clippedSource;
};

// Quake's RecursiveLeafFlow as an explicit frame stack. Each hop narrows the
// sightline bundle: the next portal is chopped to the front of the portal just
// passed, the source is chopped behind the next portal, and both are cut down
// by the separating planes between source and pass. The separators between a
// frame's source and pass are shared by every portal tried from that frame.
// What can still be reached is bounded by the next portal's base vis, or by
// its finished vis once that portal's own flow is done, so portals with little
// to see are flowed first and prune the open ones.
void flowFromPortal(
    const PortalGraph& graph,
    const std::vector<std::atomic<std::uint8_t>>& leafDone,
    const MightSeeTable& mightSee,
    const PassageTable& passages,
    DensePvs& pvs,
    int sourceIndex,
    FlowScratch& scratch) {
    const DirectedPortal& base = graph.portals[static_cast<std::size_t>(sourceIndex)];
    const int sourceLeaf = base.fromLeaf;
    floodMightSee(graph, sourceIndex, scratch.mightSee, scratch.queue, nullptr);
    scratch.portalVis.assign(mightSee.words, 0u);
    scratch.onChain.assign(static_cast<std::size_t>(pvs.leafCount), 0);
    scratch.onChain[static_cast<std::size_t>(sourceLeaf)] = 1;
    scratch.onChain[static_cast<std::size_t>(base.toLeaf)] = 1;

    auto mark = [&](int leaf) {
        setBit(scratch.portalVis, leaf);
        pvsSetBitSymmetricAtomic(pvs, sourceLeaf, leaf);
    };
    auto prune = [&](std::vector<std::uint32_t>& might, int portalIndex, int toLeaf) {
        if (!mightSee.perPortal &&
            leafDone[static_cast<std::size_t>(toLeaf)].load(std::memory_order_acquire) != 0) {
            intersectWithRow(might, pvs, toLeaf);
        } else {
            intersectWithWords(might, mightSee.row(portalIndex, toLeaf));
        }
    };

    mark(base.toLeaf);
    std::size_t depth = 0;
    {
        if (scratch.frames.empty()) {
            scratch.frames.emplace_back();
        }
        FlowFrame& root = scratch.frames[depth++];
        root.leaf = base.toLeaf;
        root.through = sourceIndex;
        root.nextPortal = 0;
        root.portalPlane = base.plane;
        copyWinding(root.source, base.winding);
        root.pass.count = 0;
        root.might.assign(mightSee.words, 0xffffffffu);
        prune(root.might, sourceIndex, base.toLeaf);
        root.separatorsReady = false;
    }

    while (depth > 0) {
        if (scratch.frames.size() <= depth) {
            scratch.frames.emplace_back();
        }
        FlowFrame& frame = scratch.frames[depth - 1];
        const std::vector<LeafPortalRef>& refs = graph.leafPortals[static_cast<std::size_t>(frame.leaf)];
        if (frame.nextPortal >= refs.size()) {
            scratch.onChain[static_cast<std::size_t>(frame.leaf)] = 0;
            --depth;
            continue;
        }
        const LeafPortalRef ref = refs[frame.nextPortal++];
        if (!testBit(scratch.mightSee, ref.portalIndex) ||
            !testBit(frame.might, ref.otherLeaf) ||
            scratch.onChain[static_cast<std::size_t>(ref.otherLeaf)] != 0) {
            continue;
        }
        const DirectedPortal& portal = graph.portals[static_cast<std::size_t>(ref.portalIndex)];
        if (Vector3DotProduct(portal.plane.normal, frame.portalPlane.normal) < -0.9999f) {
            continue;
        }

        FlowFrame& next = scratch.frames[depth];
        next.might.resize(mightSee.words);
        std::copy(frame.might.begin(), frame.might.end(), next.might.begin());
        prune(next.might, ref.portalIndex, portal.toLeaf);
        if (passages.built()) {
            intersectWithWords(next.might, passages.row(frame.through, frame.nextPortal - 1));
        }
        if (!anyNew(next.might, scratch.portalVis)) {
            continue;
        }

        const float passDist = planeDistance(frame.portalPlane, portal.origin);
        if (passDist < -portal.radius) {
            continue;
        }
        Winding& target = scratch.target;
        copyWinding(target, portal.winding);
        if (passDist <= portal.radius) {
            if (maxPlaneDistance(target, frame.portalPlane) <= kSeparatorEps ||
                !clipWinding(target, frame.portalPlane, true)) {
                continue;
            }
        }
        if (minPlaneDistance(target, base.plane) < -kSeparatorEps) {
            if (maxPlaneDistance(target, base.plane) <= kSeparatorEps ||
                !clipWinding(target, base.plane, true)) {
                continue;
            }
        }

        const float sourceDist = planeDistance(portal.plane, base.origin);
        if (sourceDist > base.radius) {
            continue;
        }
        Winding& source = scratch.clippedSource;
        copyWinding(source, frame.source);
        if (sourceDist >= -base.radius) {
            if (minPlaneDistance(source, portal.plane) >= -kSeparatorEps ||
                !clipWinding(source, portal.plane, false)) {
                continue;
            }
        }

        if (frame.pass.count != 0) {
            if (!frame.separatorsReady) {
                frame.separators.clear();
                collectSeparators(frame.source, frame.pass, false, frame.separators);
                collectSeparators(frame.pass, frame.source, true, frame.separators);
                frame.separatorsReady = true;
            }
            if (!clipByPlanes(target, frame.separators) ||
                !clipToSeparators(target, frame.pass, source, false) ||
                !clipToSeparators(frame.pass, target, source, true)) {
                continue;
            }
        }

        mark(portal.toLeaf);
        next.leaf = portal.toLeaf;
        next.through = ref.portalIndex;
        next.nextPortal = 0;
        next.portalPlane = portal.plane;
        copyWinding(next.source, source);
        copyWinding(next.pass, target);
        next.separatorsReady = false;
        scratch.onChain[static_cast<std::size_t>(next.leaf)] = 1;
        ++depth;
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

// A convex cell as its bounding planes (outward normals) and corner points.
// Open leaves whose union is still convex are merged into one cell before
// vis: a plane extended across the map from some unrelated brush splits open
// rooms into slivers that cost sightline paths without hiding anything.
struct LeafHull {
    std::vector<PvsPlane> planes;
    std::vector<Vector3> verts;
};

constexpr float kHullEps = 1e-3f;

PvsPlane flipPlane(const PvsPlane& plane) {
    return PvsPlane{Vector3Scale(plane.normal, -1.0f), -plane.distance};
}

bool samePlane(const PvsPlane& a, const PvsPlane& b) {
    return Vector3DotProduct(a.normal, b.normal) > 0.9999f &&
        std::fabs(a.distance - b.distance) < kHullEps;
}

bool sameOrFlippedPlane(const PvsPlane& a, const PvsPlane& b) {
    return samePlane(a, b) || samePlane(a, flipPlane(b));
}

void addHullPlane(LeafHull& hull, const PvsPlane& plane) {
    for (const PvsPlane& existing : hull.planes) {
        if (samePlane(existing, plane)) {
            return;
        }
    }
    hull.planes.push_back(plane);
}

void addHullVertex(LeafHull& hull, Vector3 v) {
    for (const Vector3& existing : hull.verts) {
        if (Vector3DistanceSqr(existing, v) < kHullEps * kHullEps) {
            return;
        }
    }
    hull.verts.push_back(v);
}

bool leafHull(const BspLeaf& leaf, LeafHull& out) {
    const Vector3 center = leafCentroid(leaf);
    for (const std::vector<Vector3>& face : leaf.faces) {
        PvsPlane plane{};
        if (!polygonPlane(face, plane)) {
            continue;
        }
        if (planeDistance(plane, center) > 0.0f) {
            plane = flipPlane(plane);
        }
        addHullPlane(out, plane);
        for (const Vector3& v : face) {
            addHullVertex(out, v);
        }
    }
    return out.planes.size() >= 4 && out.verts.size() >= 4;
}

bool hullInsidePlanes(const LeafHull& hull, const LeafHull& bounds, const PvsPlane& shared) {
    for (const PvsPlane& plane : bounds.planes) {
        if (sameOrFlippedPlane(plane, shared)) {
            continue;
        }
        for (const Vector3& v : hull.verts) {
            if (planeDistance(plane, v) > kHullEps) {
                return false;
            }
        }
    }
    return true;
}

LeafHull mergeHulls(const LeafHull& a, const LeafHull& b, const PvsPlane& shared) {
    LeafHull out;
    for (const LeafHull* hull : {&a, &b}) {
        for (const PvsPlane& plane : hull->planes) {
            if (!sameOrFlippedPlane(plane, shared)) {
                addHullPlane(out, plane);
            }
        }
    }
    for (const LeafHull* hull : {&a, &b}) {
        for (const Vector3& v : hull->verts) {
            bool onBoundary = false;
            for (const PvsPlane& plane : out.planes) {
                if (planeDistance(plane, v) >= -kHullEps) {
                    onBoundary = true;
                    break;
                }
            }
            if (onBoundary) {
                addHullVertex(out, v);
            }
        }
    }
    return out;
}

struct LeafClusters {
    std::vector<int> clusterOf;
    std::vector<std::vector<int>> leaves;
};

LeafClusters clusterLeaves(const BspTree& tree, const std::vector<std::uint8_t>& isSource) {
    const int n = static_cast<int>(tree.leaves.size());
    std::vector<int> parent(static_cast<std::size_t>(n));
    std::vector<LeafHull> hulls(static_cast<std::size_t>(n));
    std::vector<std::uint8_t> mergeable(static_cast<std::size_t>(n), 0);
    for (int leaf = 0; leaf < n; ++leaf) {
        parent[static_cast<std::size_t>(leaf)] = leaf;
        const BspLeaf& data = tree.leaves[static_cast<std::size_t>(leaf)];
        if (leafIsOpen(data.contents) && leafHull(data, hulls[static_cast<std::size_t>(leaf)])) {
            mergeable[static_cast<std::size_t>(leaf)] = 1;
        }
    }
    auto find = [&](int x) {
        while (parent[static_cast<std::size_t>(x)] != x) {
            parent[static_cast<std::size_t>(x)] = parent[static_cast<std::size_t>(parent[static_cast<std::size_t>(x)])];
            x = parent[static_cast<std::size_t>(x)];
        }
        return x;
    };

    bool merged = true;
    while (merged) {
        merged = false;
        for (const BspPortal& portal : tree.portals) {
            if (portal.leafA < 0 || portal.leafB < 0 || portal.leafA >= n || portal.leafB >= n) {
                continue;
            }
            const int a = find(portal.leafA);
            const int b = find(portal.leafB);
            if (a == b || mergeable[static_cast<std::size_t>(a)] == 0 || mergeable[static_cast<std::size_t>(b)] == 0) {
                continue;
            }
            if (isSource[static_cast<std::size_t>(a)] != isSource[static_cast<std::size_t>(b)]) {
                continue;
            }
            PvsPlane shared{};
            if (!polygonPlane(portal.vertices, shared)) {
                continue;
            }
            LeafHull& hullA = hulls[static_cast<std::size_t>(a)];
            LeafHull& hullB = hulls[static_cast<std::size_t>(b)];
            if (!hullInsidePlanes(hullA, hullB, shared) || !hullInsidePlanes(hullB, hullA, shared)) {
                continue;
            }
            hullA = mergeHulls(hullA, hullB, shared);
            hullB = LeafHull{};
            parent[static_cast<std::size_t>(b)] = a;
            merged = true;
        }
    }

    LeafClusters clusters;
    clusters.clusterOf.assign(static_cast<std::size_t>(n), -1);
    std::vector<int> rootCluster(static_cast<std::size_t>(n), -1);
    for (int leaf = 0; leaf < n; ++leaf) {
        if (!leafParticipatesInPortalGraph(tree.leaves[static_cast<std::size_t>(leaf)].contents)) {
            continue;
        }
        const int root = find(leaf);
        int& cluster = rootCluster[static_cast<std::size_t>(root)];
        if (cluster < 0) {
            cluster = static_cast<int>(clusters.leaves.size());
            clusters.leaves.emplace_back();
        }
        clusters.clusterOf[static_cast<std::size_t>(leaf)] = cluster;
        clusters.leaves[static_cast<std::size_t>(cluster)].push_back(leaf);
    }
    return clusters;
}

// Portals between the same two cells on the same plane are one window: two
// convex cells meet in one convex face, so the convex hull of their pieces is
// exactly that face.
struct CellPortal {
    int a = -1;
    int b = -1;
    PvsPlane plane{};
    std::vector<Vector3> winding;
    std::vector<std::vector<Vector3>> pieces;
};

std::vector<Vector3> convexHullOnPlane(const std::vector<Vector3>& points, const PvsPlane& plane) {
    const Vector3 n = plane.normal;
    const Vector3 helper = std::fabs(n.x) < 0.9f ? Vector3{1.0f, 0.0f, 0.0f} : Vector3{0.0f, 1.0f, 0.0f};
    const Vector3 u = Vector3Normalize(Vector3CrossProduct(n, helper));
    const Vector3 v = Vector3CrossProduct(n, u);
    struct Projected {
        float x;
        float y;
        Vector3 p;
    };
    std::vector<Projected> pts;
    pts.reserve(points.size());
    for (const Vector3& p : points) {
        pts.push_back(Projected{Vector3DotProduct(p, u), Vector3DotProduct(p, v), p});
    }
    std::sort(pts.begin(), pts.end(), [](const Projected& l, const Projected& r) {
        return l.x < r.x || (l.x == r.x && l.y < r.y);
    });
    auto cross = [](const Projected& o, const Projected& a, const Projected& b) {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };
    std::vector<Projected> hull(pts.size() * 2);
    std::size_t k = 0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], pts[i]) <= 1e-9f) {
            --k;
        }
        hull[k++] = pts[i];
    }
    for (std::size_t i = pts.size() - 1, t = k + 1; i > 0; --i) {
        while (k >= t && cross(hull[k - 2], hull[k - 1], pts[i - 1]) <= 1e-9f) {
            --k;
        }
        hull[k++] = pts[i - 1];
    }
    std::vector<Vector3> out;
    for (std::size_t i = 0; i + 1 < k; ++i) {
        out.push_back(hull[i].p);
    }
    return out;
}

std::vector<CellPortal> buildCellPortals(const BspTree& tree, const LeafClusters& clusters) {
    const int leafCount = static_cast<int>(tree.leaves.size());
    std::vector<CellPortal> out;
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> byPair;
    for (const BspPortal& portal : tree.portals) {
        if (portal.leafA < 0 || portal.leafB < 0 || portal.vertices.size() < 3) {
            continue;
        }
        if (portal.leafA >= leafCount || portal.leafB >= leafCount) {
            continue;
        }
        int a = clusters.clusterOf[static_cast<std::size_t>(portal.leafA)];
        int b = clusters.clusterOf[static_cast<std::size_t>(portal.leafB)];
        if (a < 0 || b < 0 || a == b) {
            continue;
        }
        PvsPlane plane{};
        if (!polygonPlane(portal.vertices, plane)) {
            continue;
        }
        if (planeDistance(plane, leafCentroid(tree.leaves[static_cast<std::size_t>(portal.leafA)])) > 0.0f) {
            plane = flipPlane(plane);
        }
        if (a > b) {
            std::swap(a, b);
            plane = flipPlane(plane);
        }
        const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(a)) << 32) |
            static_cast<std::uint32_t>(b);
        std::vector<std::size_t>& list = byPair[key];
        bool merged = false;
        for (std::size_t index : list) {
            CellPortal& existing = out[index];
            if (samePlane(existing.plane, plane)) {
                existing.winding.insert(existing.winding.end(), portal.vertices.begin(), portal.vertices.end());
                existing.pieces.push_back(portal.vertices);
                merged = true;
                break;
            }
        }
        if (merged) {
            continue;
        }
        list.push_back(out.size());
        out.push_back(CellPortal{a, b, plane, portal.vertices, {portal.vertices}});
    }
    std::vector<CellPortal> split;
    for (CellPortal& portal : out) {
        if (portal.pieces.size() > 1) {
            portal.winding = convexHullOnPlane(portal.winding, portal.plane);
            if (portal.winding.size() > static_cast<std::size_t>(kMaxWindingVerts)) {
                portal.winding = portal.pieces.front();
                for (std::size_t i = 1; i < portal.pieces.size(); ++i) {
                    split.push_back(CellPortal{portal.a, portal.b, portal.plane, portal.pieces[i], {}});
                }
            }
        }
        if (portal.winding.size() > static_cast<std::size_t>(kMaxWindingVerts)) {
            portal.winding = convexHullOnPlane(portal.winding, portal.plane);
        }
        portal.pieces.clear();
    }
    out.insert(out.end(), split.begin(), split.end());
    return out;
}

std::uint64_t hashRow(const std::vector<std::uint32_t>& row) {
    std::uint64_t hash = 1469598103934665603ull;
    for (std::uint32_t word : row) {
        hash = (hash ^ word) * 1099511628211ull;
    }
    return hash;
}

// Packs bit rows over @p leafCount columns into a PvsFile, keeping only the
// columns some row sets and one copy of each distinct row. @p leafRowIndex
// names each leaf's row in @p rows, or -1 for none.
PvsFile compactRows(
    int leafCount,
    const std::vector<std::uint32_t>& rows,
    int rowWordsIn,
    const std::vector<std::int32_t>& leafRowIndex) {
    PvsFile pvs;
    pvs.leafCount = leafCount;
    if (leafCount <= 0) {
        return pvs;
    }
    pvs.leafRow.assign(static_cast<std::size_t>(leafCount), -1);
    pvs.leafColumn.assign(static_cast<std::size_t>(leafCount), -1);
    if (rowWordsIn <= 0 || rows.empty()) {
        return pvs;
    }

    const std::size_t inWords = static_cast<std::size_t>(rowWordsIn);
    const std::size_t inRows = rows.size() / inWords;
    std::vector<std::uint32_t> used(inWords, 0u);
    for (std::size_t r = 0; r < inRows; ++r) {
        const std::uint32_t* src = rows.data() + r * inWords;
        for (std::size_t w = 0; w < inWords; ++w) {
            used[w] |= src[w];
        }
    }
    int columns = 0;
    for (int b = 0; b < leafCount; ++b) {
        if ((used[static_cast<std::size_t>(b >> 5)] & (1u << (b & 31))) != 0u) {
            pvs.leafColumn[static_cast<std::size_t>(b)] = columns++;
        }
    }
    pvs.columnCount = columns;
    pvs.wordsPerRow = (columns + 31) / 32;
    if (columns == 0) {
        return pvs;
    }

    const std::size_t rowWords = static_cast<std::size_t>(pvs.wordsPerRow);
    std::unordered_map<std::uint64_t, std::vector<std::int32_t>> rowsByHash;
    std::vector<std::int32_t> rowMatch(inRows, -1);
    std::vector<std::uint32_t> packed(rowWords);
    for (std::size_t r = 0; r < inRows; ++r) {
        const std::uint32_t* src = rows.data() + r * inWords;
        std::fill(packed.begin(), packed.end(), 0u);
        bool any = false;
        for (std::size_t w = 0; w < inWords; ++w) {
            std::uint32_t word = src[w];
            while (word != 0u) {
                const int bit = std::countr_zero(word);
                word &= word - 1u;
                const int b = static_cast<int>(w * 32) + bit;
                if (b >= leafCount) {
                    continue;
                }
                const std::int32_t c = pvs.leafColumn[static_cast<std::size_t>(b)];
                packed[static_cast<std::size_t>(c >> 5)] |= 1u << (c & 31);
                any = true;
            }
        }
        if (!any) {
            continue;
        }

        std::vector<std::int32_t>& candidates = rowsByHash[hashRow(packed)];
        std::int32_t match = -1;
        for (std::int32_t candidate : candidates) {
            const auto begin = pvs.rows.begin() + static_cast<std::ptrdiff_t>(
                static_cast<std::size_t>(candidate) * rowWords);
            if (std::equal(packed.begin(), packed.end(), begin)) {
                match = candidate;
                break;
            }
        }
        if (match < 0) {
            match = static_cast<std::int32_t>(pvs.rows.size() / rowWords);
            pvs.rows.insert(pvs.rows.end(), packed.begin(), packed.end());
            candidates.push_back(match);
        }
        rowMatch[r] = match;
    }
    for (int leaf = 0; leaf < leafCount; ++leaf) {
        const std::int32_t r = leafRowIndex[static_cast<std::size_t>(leaf)];
        if (r >= 0 && static_cast<std::size_t>(r) < inRows) {
            pvs.leafRow[static_cast<std::size_t>(leaf)] = rowMatch[static_cast<std::size_t>(r)];
        }
    }
    return pvs;
}

template <typename Work>
void runWorkers(int threadCount, Work&& work) {
    std::vector<std::thread> workers;
    for (int t = 1; t < threadCount; ++t) {
        workers.emplace_back(work);
    }
    work();
    for (std::thread& t : workers) {
        t.join();
    }
}

PvsFile expandClusterPvs(
    const DensePvs& clusterPvs,
    const LeafClusters& clusters,
    int leafCount,
    int threadCount) {
    const int clusterCount = clusterPvs.leafCount;
    const std::size_t leafWords = static_cast<std::size_t>((leafCount + 31) / 32);
    std::vector<std::uint32_t> rows(static_cast<std::size_t>(clusterCount) * leafWords, 0u);
    std::atomic<int> nextCluster{0};
    runWorkers(threadCount, [&]() {
        for (;;) {
            const int c = nextCluster.fetch_add(1, std::memory_order_relaxed);
            if (c >= clusterCount) {
                break;
            }
            const std::uint32_t* src =
                clusterPvs.bits.data() + static_cast<std::size_t>(c) * static_cast<std::size_t>(clusterPvs.wordsPerRow);
            std::uint32_t* dst = rows.data() + static_cast<std::size_t>(c) * leafWords;
            for (int w = 0; w < clusterPvs.wordsPerRow; ++w) {
                std::uint32_t word = src[static_cast<std::size_t>(w)];
                while (word != 0u) {
                    const int other = w * 32 + std::countr_zero(word);
                    word &= word - 1u;
                    if (other >= clusterCount) {
                        continue;
                    }
                    for (int leaf : clusters.leaves[static_cast<std::size_t>(other)]) {
                        dst[static_cast<std::size_t>(leaf >> 5)] |= 1u << (leaf & 31);
                    }
                }
            }
        }
    });
    std::vector<std::int32_t> leafRowIndex(clusters.clusterOf.begin(), clusters.clusterOf.end());
    return compactRows(leafCount, rows, static_cast<int>(leafWords), leafRowIndex);
}

} // namespace

int pvsVisibleCount(const PvsFile& pvs, std::int32_t fromLeaf) {
    if (fromLeaf < 0 || fromLeaf >= static_cast<std::int32_t>(pvs.leafRow.size())) {
        return 0;
    }
    const std::int32_t row = pvs.leafRow[static_cast<std::size_t>(fromLeaf)];
    if (row < 0) {
        return 0;
    }
    const std::size_t begin = static_cast<std::size_t>(row) * static_cast<std::size_t>(pvs.wordsPerRow);
    int count = 0;
    for (int w = 0; w < pvs.wordsPerRow; ++w) {
        count += std::popcount(pvs.rows[begin + static_cast<std::size_t>(w)]);
    }
    return count;
}

PvsFile compactPvs(const DensePvs& dense) {
    std::vector<std::int32_t> identity(static_cast<std::size_t>(std::max(0, dense.leafCount)));
    for (std::size_t i = 0; i < identity.size(); ++i) {
        identity[i] = static_cast<std::int32_t>(i);
    }
    return compactRows(dense.leafCount, dense.bits, dense.wordsPerRow, identity);
}

PvsFile buildPvs(const BspTree& tree, const std::vector<std::uint8_t>* exteriorEmpty, bool fast) {
    const int leafCount = static_cast<int>(tree.leaves.size());
    if (leafCount <= 0) {
        DensePvs none;
        none.leafCount = leafCount;
        return compactPvs(none);
    }

    std::vector<std::uint8_t> leafIsSource(static_cast<std::size_t>(leafCount), 0);
    for (int leaf = 0; leaf < leafCount; ++leaf) {
        if (isPvsSourceLeaf(tree, leaf, exteriorEmpty)) {
            leafIsSource[static_cast<std::size_t>(leaf)] = 1;
        }
    }
    const LeafClusters clusters = clusterLeaves(tree, leafIsSource);
    const int n = static_cast<int>(clusters.leaves.size());

    DensePvs pvs;
    pvs.leafCount = n;
    pvs.wordsPerRow = (n + 31) / 32;
    pvs.bits.assign(static_cast<std::size_t>(n) * static_cast<std::size_t>(pvs.wordsPerRow), 0u);

    PortalGraph graph;
    graph.portals.reserve(tree.portals.size() * 2);
    graph.leafPortals.resize(static_cast<std::size_t>(n));

    const std::vector<CellPortal> cellPortals = buildCellPortals(tree, clusters);
    for (const CellPortal& portal : cellPortals) {
        if (portal.winding.size() < 3 || portal.winding.size() > static_cast<std::size_t>(kMaxWindingVerts)) {
            continue;
        }
        DirectedPortal ab;
        ab.fromLeaf = portal.a;
        ab.toLeaf = portal.b;
        setWinding(ab.winding, portal.winding);
        ab.plane = portal.plane;
        setPortalBounds(ab);
        const int abIndex = static_cast<int>(graph.portals.size());
        graph.portals.push_back(std::move(ab));
        graph.leafPortals[static_cast<std::size_t>(portal.a)].push_back(LeafPortalRef{abIndex, portal.b});

        DirectedPortal ba;
        ba.fromLeaf = portal.b;
        ba.toLeaf = portal.a;
        setWinding(ba.winding, portal.winding);
        std::reverse(ba.winding.v, ba.winding.v + ba.winding.count);
        ba.plane = flipPlane(portal.plane);
        setPortalBounds(ba);
        const int baIndex = static_cast<int>(graph.portals.size());
        graph.portals.push_back(std::move(ba));
        graph.leafPortals[static_cast<std::size_t>(portal.b)].push_back(LeafPortalRef{baIndex, portal.a});
    }

    const int portalCount = static_cast<int>(graph.portals.size());
    const std::size_t portalWords = static_cast<std::size_t>((portalCount + 31) / 32);
    const unsigned hwThreads = std::max(1u, std::thread::hardware_concurrency());
    const int threadCount = std::max(1, std::min<int>(static_cast<int>(hwThreads), n));

    std::vector<std::uint8_t> isSource(static_cast<std::size_t>(n), 0);
    std::vector<int> portalLeaves;
    for (int c = 0; c < n; ++c) {
        if (leafIsSource[static_cast<std::size_t>(clusters.leaves[static_cast<std::size_t>(c)].front())] != 0) {
            isSource[static_cast<std::size_t>(c)] = 1;
            pvsSetBitAtomic(pvs, c, c);
        }
        if (!graph.leafPortals[static_cast<std::size_t>(c)].empty()) {
            portalLeaves.push_back(c);
        }
    }
    TraceLog(
        LOG_INFO,
        "slopvis: %d leaves merged into %d cells, %zu portals from %zu",
        leafCount,
        n,
        graph.portals.size() / 2,
        tree.portals.size());

    // Base vis is kept per directed portal when it fits: what lies beyond a
    // portal is a far tighter bound than what its cell can reach in any
    // direction, and a finished portal's real vis replaces its row in place.
    // Past the budget it collapses to one row per cell.
    constexpr std::size_t kMightSeeBudgetBytes = std::size_t{1536} * 1024 * 1024;
    MightSeeTable mightSee;
    mightSee.words = static_cast<std::size_t>(pvs.wordsPerRow);
    mightSee.perPortal =
        static_cast<std::size_t>(portalCount) * mightSee.words * 4 <= kMightSeeBudgetBytes;
    mightSee.bits.assign(static_cast<std::size_t>(mightSee.perPortal ? portalCount : n) * mightSee.words, 0u);
    std::vector<int> portalMightCount(static_cast<std::size_t>(portalCount), 0);
    const bool usePassages = mightSee.perPortal &&
        static_cast<std::size_t>(portalCount) * portalWords * 4 <= kMightSeeBudgetBytes / 2;
    std::vector<std::uint32_t> portalBase;
    if (usePassages) {
        portalBase.assign(static_cast<std::size_t>(portalCount) * portalWords, 0u);
    }
    {
        std::atomic<std::size_t> nextLeaf{0};
        runWorkers(threadCount, [&]() {
            std::vector<std::uint32_t> portalBits(portalWords, 0u);
            std::vector<int> queue;
            for (;;) {
                const std::size_t i = nextLeaf.fetch_add(1, std::memory_order_relaxed);
                if (i >= portalLeaves.size()) {
                    break;
                }
                const int leaf = portalLeaves[i];
                for (const LeafPortalRef& ref : graph.leafPortals[static_cast<std::size_t>(leaf)]) {
                    portalMightCount[static_cast<std::size_t>(ref.portalIndex)] = floodMightSee(
                        graph, ref.portalIndex, portalBits, queue, mightSee.row(ref.portalIndex, leaf));
                    if (usePassages) {
                        std::copy(
                            portalBits.begin(),
                            portalBits.end(),
                            portalBase.begin() + static_cast<std::ptrdiff_t>(
                                static_cast<std::size_t>(ref.portalIndex) * portalWords));
                    }
                }
            }
        });
    }

    PassageTable passages;
    if (usePassages && !fast) {
        passages.words = mightSee.words;
        passages.start.assign(static_cast<std::size_t>(portalCount) + 1, 0);
        for (int p = 0; p < portalCount; ++p) {
            const int cell = graph.portals[static_cast<std::size_t>(p)].toLeaf;
            passages.start[static_cast<std::size_t>(p) + 1] =
                passages.start[static_cast<std::size_t>(p)] + graph.leafPortals[static_cast<std::size_t>(cell)].size();
        }
        passages.bits.assign(passages.start.back() * passages.words, 0u);
        std::atomic<int> nextPortal{0};
        runWorkers(threadCount, [&]() {
            Winding pass;
            Winding target;
            std::vector<PvsPlane> separators;
            for (;;) {
                const int p = nextPortal.fetch_add(1, std::memory_order_relaxed);
                if (p >= portalCount) {
                    break;
                }
                const DirectedPortal& source = graph.portals[static_cast<std::size_t>(p)];
                const std::uint32_t* sourceBase = portalBase.data() + static_cast<std::size_t>(p) * portalWords;
                const std::vector<LeafPortalRef>& refs = graph.leafPortals[static_cast<std::size_t>(source.toLeaf)];
                for (std::size_t r = 0; r < refs.size(); ++r) {
                    std::uint32_t* row = passages.bits.data() + (passages.start[static_cast<std::size_t>(p)] + r) * passages.words;
                    const int q = refs[r].portalIndex;
                    const DirectedPortal& through = graph.portals[static_cast<std::size_t>(q)];
                    if ((sourceBase[static_cast<std::size_t>(q >> 5)] & (1u << (q & 31))) == 0u) {
                        continue;
                    }
                    copyWinding(pass, through.winding);
                    if (!clipWinding(pass, source.plane, true)) {
                        continue;
                    }
                    separators.clear();
                    collectSeparators(source.winding, pass, false, separators);
                    collectSeparators(pass, source.winding, true, separators);
                    row[static_cast<std::size_t>(through.toLeaf >> 5)] |= 1u << (through.toLeaf & 31);
                    const std::uint32_t* qBase = portalBase.data() + static_cast<std::size_t>(q) * portalWords;
                    for (std::size_t w = 0; w < portalWords; ++w) {
                        std::uint32_t word = qBase[w] & sourceBase[w];
                        while (word != 0u) {
                            const int rIndex = static_cast<int>(w * 32) + std::countr_zero(word);
                            word &= word - 1u;
                            if (rIndex >= portalCount) {
                                continue;
                            }
                            const DirectedPortal& far = graph.portals[static_cast<std::size_t>(rIndex)];
                            if ((row[static_cast<std::size_t>(far.toLeaf >> 5)] & (1u << (far.toLeaf & 31))) != 0u) {
                                continue;
                            }
                            copyWinding(target, far.winding);
                            if (!clipWinding(target, through.plane, true) ||
                                !clipWinding(target, source.plane, true) ||
                                !clipByPlanes(target, separators)) {
                                continue;
                            }
                            row[static_cast<std::size_t>(far.toLeaf >> 5)] |= 1u << (far.toLeaf & 31);
                        }
                    }
                }
            }
        });
        portalBase.clear();
        portalBase.shrink_to_fit();
        TraceLog(
            LOG_INFO,
            "slopvis: passages built for %zu portal pairs (%.0f MB)",
            passages.start.back(),
            static_cast<double>(passages.bits.size() * 4) / (1024.0 * 1024.0));
    }
    {
        std::vector<int> sorted = portalMightCount;
        std::sort(sorted.begin(), sorted.end());
        TraceLog(
            LOG_INFO,
            "slopvis: base vis done for %d portals (%s rows, %.0f MB), might see median %d max %d",
            portalCount,
            mightSee.perPortal ? "per-portal" : "per-cell",
            static_cast<double>(mightSee.bits.size() * 4) / (1024.0 * 1024.0),
            sorted.empty() ? 0 : sorted[sorted.size() / 2],
            sorted.empty() ? 0 : sorted.back());
    }

    std::vector<int> work;
    for (int p = 0; p < portalCount; ++p) {
        if (isSource[static_cast<std::size_t>(graph.portals[static_cast<std::size_t>(p)].fromLeaf)] != 0) {
            work.push_back(p);
        }
    }

    if (fast) {
        for (int p : work) {
            const DirectedPortal& portal = graph.portals[static_cast<std::size_t>(p)];
            const std::uint32_t* row = mightSee.row(p, portal.fromLeaf);
            pvsSetBitSymmetricAtomic(pvs, portal.fromLeaf, portal.toLeaf);
            for (int w = 0; w < pvs.wordsPerRow; ++w) {
                std::uint32_t word = row[static_cast<std::size_t>(w)];
                while (word != 0u) {
                    const int leaf = w * 32 + std::countr_zero(word);
                    word &= word - 1u;
                    if (leaf < n) {
                        pvsSetBitSymmetricAtomic(pvs, portal.fromLeaf, leaf);
                    }
                }
            }
        }
        TraceLog(LOG_INFO, "slopvis: fast vis from %zu source portals", work.size());
        return expandClusterPvs(pvs, clusters, leafCount, threadCount);
    }

    // Portals are flowed in order of how much they might see, so closets
    // finish before halls and prune them; the counter hands portals out one
    // at a time because per-portal cost varies by orders of magnitude.
    std::stable_sort(work.begin(), work.end(), [&](int a, int b) {
        return portalMightCount[static_cast<std::size_t>(a)] < portalMightCount[static_cast<std::size_t>(b)];
    });

    std::vector<std::atomic<int>> leafRemaining(static_cast<std::size_t>(n));
    for (int p : work) {
        leafRemaining[static_cast<std::size_t>(graph.portals[static_cast<std::size_t>(p)].fromLeaf)]
            .fetch_add(1, std::memory_order_relaxed);
    }
    std::vector<std::atomic<std::uint8_t>> leafDone(static_cast<std::size_t>(n));
    std::atomic<std::size_t> nextWork{0};
    std::atomic<std::size_t> finished{0};
    runWorkers(threadCount, [&]() {
        FlowScratch scratch;
        scratch.mightSee.assign(portalWords, 0u);
        for (;;) {
            const std::size_t i = nextWork.fetch_add(1, std::memory_order_relaxed);
            if (i >= work.size()) {
                break;
            }
            const int p = work[i];
            const int leaf = graph.portals[static_cast<std::size_t>(p)].fromLeaf;
            flowFromPortal(graph, leafDone, mightSee, passages, pvs, p, scratch);
            if (mightSee.perPortal) {
                std::uint32_t* row = mightSee.row(p, leaf);
                for (std::size_t w = 0; w < scratch.portalVis.size(); ++w) {
                    std::atomic_ref<std::uint32_t> word(row[w]);
                    word.store(scratch.portalVis[w], std::memory_order_relaxed);
                }
            }
            if (leafRemaining[static_cast<std::size_t>(leaf)].fetch_sub(1, std::memory_order_acq_rel) == 1) {
                leafDone[static_cast<std::size_t>(leaf)].store(1, std::memory_order_release);
            }
            const std::size_t done = finished.fetch_add(1, std::memory_order_relaxed) + 1;
            if (done % 10000 == 0) {
                TraceLog(LOG_INFO, "slopvis: flowed %zu/%zu portals", done, work.size());
            }
        }
    });

    TraceLog(
        LOG_INFO,
        "slopvis: flowed %zu source portals over %d cells using %d threads",
        work.size(),
        n,
        threadCount);

    return expandClusterPvs(pvs, clusters, leafCount, threadCount);
}

}
