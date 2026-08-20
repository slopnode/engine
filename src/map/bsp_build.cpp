#include "map/bsp.hpp"
#include "map/bsp_analyze.hpp"

#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace slopengine {

namespace {

constexpr float kPlaneEps = 1e-4f;
constexpr float kBoundsPad = 0.5f;
constexpr float kMinFaceArea = 1e-6f;

float dot3(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 add3(Vector3 a, Vector3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 sub3(Vector3 a, Vector3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vector3 scale3(Vector3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}

Vector3 cross3(Vector3 a, Vector3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

float length3(Vector3 v) {
    return std::sqrt(dot3(v, v));
}

Vector3 normalize3(Vector3 v) {
    const float len = length3(v);
    if (len <= 1e-8f) {
        return {};
    }
    return {v.x / len, v.y / len, v.z / len};
}

float planeDistance(const BspPlane& plane, Vector3 point) {
    return dot3(plane.normal, point) - plane.distance;
}

BspPlane planeFromFace(const BrushFace& face) {
    BspPlane plane;
    plane.normal = face.normal;
    if (!face.vertices.empty()) {
        plane.distance = dot3(face.normal, face.vertices[0]);
    }
    return plane;
}

bool planesEqual(const BspPlane& a, const BspPlane& b) {
    const float nd = dot3(a.normal, b.normal);
    if (nd < 0.999f) {
        return false;
    }
    return std::fabs(a.distance - b.distance) <= kPlaneEps;
}

float polygonArea(const std::vector<Vector3>& verts) {
    if (verts.size() < 3) {
        return 0.0f;
    }
    Vector3 accum{};
    for (std::size_t i = 1; i + 1 < verts.size(); ++i) {
        accum = add3(accum, cross3(sub3(verts[i], verts[0]), sub3(verts[i + 1], verts[0])));
    }
    return 0.5f * length3(accum);
}

Vector3 polygonNormal(const std::vector<Vector3>& verts) {
    if (verts.size() < 3) {
        return {};
    }
    Vector3 accum{};
    for (std::size_t i = 1; i + 1 < verts.size(); ++i) {
        accum = add3(accum, cross3(sub3(verts[i], verts[0]), sub3(verts[i + 1], verts[0])));
    }
    return normalize3(accum);
}

Vector3 polygonCentroid(const std::vector<Vector3>& verts) {
    Vector3 sum{};
    if (verts.empty()) {
        return sum;
    }
    for (const Vector3& v : verts) {
        sum = add3(sum, v);
    }
    return scale3(sum, 1.0f / static_cast<float>(verts.size()));
}

std::vector<Vector3> clipPolygonAgainstPlane(
    const std::vector<Vector3>& input,
    const BspPlane& plane,
    bool keepFront) {
    std::vector<Vector3> output;
    if (input.empty()) {
        return output;
    }

    auto isInside = [&](Vector3 p) {
        const float d = planeDistance(plane, p);
        return keepFront ? (d >= -kPlaneEps) : (d <= kPlaneEps);
    };

    for (std::size_t i = 0; i < input.size(); ++i) {
        const Vector3& current = input[i];
        const Vector3& next = input[(i + 1) % input.size()];
        const bool currentIn = isInside(current);
        const bool nextIn = isInside(next);
        const float d0 = planeDistance(plane, current);
        const float d1 = planeDistance(plane, next);

        if (currentIn && nextIn) {
            output.push_back(next);
        } else if (currentIn && !nextIn) {
            const float t = d0 / (d0 - d1);
            output.push_back(add3(current, scale3(sub3(next, current), t)));
        } else if (!currentIn && nextIn) {
            const float t = d0 / (d0 - d1);
            output.push_back(add3(current, scale3(sub3(next, current), t)));
            output.push_back(next);
        }
    }
    return output;
}

std::vector<Vector3> buildCapPolygon(
    const std::vector<std::vector<Vector3>>& clippedFaces,
    const BspPlane& plane,
    bool frontCap) {
    std::vector<Vector3> points;
    for (const std::vector<Vector3>& face : clippedFaces) {
        for (const Vector3& v : face) {
            if (std::fabs(planeDistance(plane, v)) <= kPlaneEps * 4.0f) {
                bool unique = true;
                for (const Vector3& existing : points) {
                    if (length3(sub3(existing, v)) <= kPlaneEps * 4.0f) {
                        unique = false;
                        break;
                    }
                }
                if (unique) {
                    points.push_back(v);
                }
            }
        }
    }
    if (points.size() < 3) {
        return {};
    }

    Vector3 origin = polygonCentroid(points);
    Vector3 n = plane.normal;
    if (!frontCap) {
        n = scale3(n, -1.0f);
    }
    Vector3 tangent = std::fabs(n.y) < 0.9f ? cross3(n, {0.0f, 1.0f, 0.0f}) : cross3(n, {1.0f, 0.0f, 0.0f});
    tangent = normalize3(tangent);
    const Vector3 bitangent = normalize3(cross3(n, tangent));

    std::sort(points.begin(), points.end(), [&](Vector3 a, Vector3 b) {
        const Vector3 da = sub3(a, origin);
        const Vector3 db = sub3(b, origin);
        const float angA = std::atan2(dot3(da, bitangent), dot3(da, tangent));
        const float angB = std::atan2(dot3(db, bitangent), dot3(db, tangent));
        if (angA != angB) return angA < angB;
        const float lenA = dot3(da, tangent) * dot3(da, tangent) + dot3(da, bitangent) * dot3(da, bitangent);
        const float lenB = dot3(db, tangent) * dot3(db, tangent) + dot3(db, bitangent) * dot3(db, bitangent);
        return lenA < lenB;
    });

    if (polygonArea(points) < kMinFaceArea) {
        return {};
    }
    return points;
}

struct Polyhedron {
    std::vector<std::vector<Vector3>> faces;
    Vector3 mins{};
    Vector3 maxs{};
};

void recomputePolyBounds(Polyhedron& poly) {
    poly.mins = {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    poly.maxs = {
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
    };
    bool any = false;
    for (const auto& face : poly.faces) {
        for (const Vector3& v : face) {
            any = true;
            poly.mins.x = std::min(poly.mins.x, v.x);
            poly.mins.y = std::min(poly.mins.y, v.y);
            poly.mins.z = std::min(poly.mins.z, v.z);
            poly.maxs.x = std::max(poly.maxs.x, v.x);
            poly.maxs.y = std::max(poly.maxs.y, v.y);
            poly.maxs.z = std::max(poly.maxs.z, v.z);
        }
    }
    if (!any) {
        poly.mins = {};
        poly.maxs = {};
    }
}

Polyhedron makeBoundsPolyhedron(Vector3 mins, Vector3 maxs) {
    Polyhedron poly;
    const float x0 = mins.x;
    const float x1 = maxs.x;
    const float y0 = mins.y;
    const float y1 = maxs.y;
    const float z0 = mins.z;
    const float z1 = maxs.z;
    poly.faces = {
        {{x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}},
        {{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}},
        {{x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}},
        {{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}},
        {{x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}},
        {{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}},
    };
    recomputePolyBounds(poly);
    return poly;
}

bool splitPolyhedron(
    const Polyhedron& input,
    const BspPlane& plane,
    Polyhedron& frontOut,
    Polyhedron& backOut) {
    frontOut.faces.clear();
    backOut.faces.clear();

    bool anyFront = false;
    bool anyBack = false;
    for (const auto& face : input.faces) {
        for (const Vector3& v : face) {
            const float d = planeDistance(plane, v);
            if (d > kPlaneEps) {
                anyFront = true;
            } else if (d < -kPlaneEps) {
                anyBack = true;
            }
        }
    }
    if (!anyFront || !anyBack) {
        return false;
    }

    for (const auto& face : input.faces) {
        auto frontFace = clipPolygonAgainstPlane(face, plane, true);
        auto backFace = clipPolygonAgainstPlane(face, plane, false);
        if (frontFace.size() >= 3 && polygonArea(frontFace) >= kMinFaceArea) {
            frontOut.faces.push_back(std::move(frontFace));
        }
        if (backFace.size() >= 3 && polygonArea(backFace) >= kMinFaceArea) {
            backOut.faces.push_back(std::move(backFace));
        }
    }

    auto frontCap = buildCapPolygon(frontOut.faces, plane, true);
    auto backCap = buildCapPolygon(backOut.faces, plane, false);
    if (frontCap.size() >= 3) {
        frontOut.faces.push_back(std::move(frontCap));
    }
    if (backCap.size() >= 3) {
        backOut.faces.push_back(std::move(backCap));
    }

    recomputePolyBounds(frontOut);
    recomputePolyBounds(backOut);
    return !frontOut.faces.empty() && !backOut.faces.empty();
}

Vector3 polyCentroid(const Polyhedron& poly) {
    Vector3 sum{};
    std::size_t count = 0;
    for (const auto& face : poly.faces) {
        for (const Vector3& v : face) {
            sum = add3(sum, v);
            ++count;
        }
    }
    if (count == 0) {
        return {
            0.5f * (poly.mins.x + poly.maxs.x),
            0.5f * (poly.mins.y + poly.maxs.y),
            0.5f * (poly.mins.z + poly.maxs.z),
        };
    }
    return scale3(sum, 1.0f / static_cast<float>(count));
}

enum class CellClass {
    WhollySolid,
    WhollyOpen,
    Mixed,
};

struct CellClassification {
    CellClass cell = CellClass::WhollyOpen;
    bool preferGlass = false;
    bool preferDoor = false;
};

void collectCellSamples(const Polyhedron& poly, std::vector<Vector3>& out) {
    out.clear();
    out.push_back(polyCentroid(poly));
    for (const auto& face : poly.faces) {
        for (const Vector3& v : face) {
            out.push_back(v);
        }
        if (face.size() >= 3) {
            out.push_back(polygonCentroid(face));
        }
    }
}

bool pointStrictlyInsideBrush(Vector3 point, const Brush& brush, float epsilon = kPlaneEps) {
    for (const BrushFace& face : brush.faces) {
        if (face.vertices.empty()) {
            return false;
        }
        const float side = dot3(sub3(point, face.vertices[0]), face.normal);
        if (side >= -epsilon) {
            return false;
        }
    }
    return !brush.faces.empty();
}

bool centroidInsideSealing(
    const Polyhedron& poly,
    const std::vector<Brush>& sealing,
    bool& preferGlass,
    bool& preferDoor) {
    preferGlass = false;
    preferDoor = false;
    const Vector3 center = polyCentroid(poly);
    bool inside = false;
    for (const Brush& brush : sealing) {
        if (!pointInsideBrushInclusive(center, brush)) {
            continue;
        }
        inside = true;
        if (brush.role == BrushRole::Window) {
            preferGlass = true;
            preferDoor = false;
        } else if (brush.role == BrushRole::Door) {
            preferGlass = false;
            preferDoor = true;
        } else {
            preferGlass = false;
            preferDoor = false;
            return true;
        }
    }
    return inside;
}

CellClassification classifyAgainstSealing(
    const Polyhedron& poly,
    const std::vector<Brush>& sealing) {
    CellClassification result;
    if (sealing.empty()) {
        result.cell = CellClass::WhollyOpen;
        return result;
    }

    std::vector<Vector3> samples;
    collectCellSamples(poly, samples);

    bool anyInside = false;
    bool anyOutside = false;
    bool glassInside = false;
    bool doorInside = false;
    bool solidInside = false;

    for (const Vector3& sample : samples) {
        bool inside = false;
        for (const Brush& brush : sealing) {
            if (!pointStrictlyInsideBrush(sample, brush)) {
                continue;
            }
            inside = true;
            if (brush.role == BrushRole::Window) {
                glassInside = true;
            } else if (brush.role == BrushRole::Door) {
                doorInside = true;
            } else {
                solidInside = true;
            }
        }
        if (inside) {
            anyInside = true;
        } else {
            anyOutside = true;
        }
    }

    if (anyInside && anyOutside) {
        result.cell = CellClass::Mixed;
    } else if (anyInside) {
        result.cell = CellClass::WhollySolid;
        if (solidInside && (glassInside || doorInside)) {
            bool preferGlass = false;
            bool preferDoor = false;
            centroidInsideSealing(poly, sealing, preferGlass, preferDoor);
            result.preferGlass = preferGlass;
            result.preferDoor = preferDoor;
        } else {
            result.preferGlass = glassInside;
            result.preferDoor = doorInside;
        }
    } else {
        result.cell = CellClass::WhollyOpen;
    }
    return result;
}

struct BuildBrushes {
    std::vector<Brush> sealing;
    std::vector<Brush> water;
    std::vector<Brush> trigger;
    std::vector<Brush> surface;
};

std::uint32_t softContentsAtPoint(
    Vector3 point,
    const std::vector<Brush>& waterBrushes,
    const std::vector<Brush>& triggerBrushes) {
    std::uint32_t contents = 0;
    for (const Brush& brush : waterBrushes) {
        if (pointInsideBrushInclusive(point, brush)) {
            contents |= BspContents::Water;
            break;
        }
    }
    for (const Brush& brush : triggerBrushes) {
        if (pointInsideBrushInclusive(point, brush)) {
            contents |= BspContents::Trigger;
            break;
        }
    }
    return contents;
}

std::uint32_t terminalLeafContents(
    const Polyhedron& poly,
    const BuildBrushes& brushes,
    const CellClassification& classification) {
    if (classification.cell == CellClass::WhollySolid) {
        if (classification.preferGlass) {
            return BspContents::Glass;
        }
        return classification.preferDoor ? (BspContents::Solid | BspContents::Door) : BspContents::Solid;
    }
    if (classification.cell == CellClass::Mixed) {
        bool preferGlass = false;
        bool preferDoor = false;
        if (centroidInsideSealing(poly, brushes.sealing, preferGlass, preferDoor)) {
            if (preferGlass) {
                return BspContents::Glass;
            }
            return preferDoor ? (BspContents::Solid | BspContents::Door) : BspContents::Solid;
        }
    }
    return softContentsAtPoint(polyCentroid(poly), brushes.water, brushes.trigger);
}

std::int32_t emitLeaf(
    BspTree& tree,
    const Polyhedron& poly,
    std::uint32_t contents) {
    BspLeaf leaf;
    leaf.mins = poly.mins;
    leaf.maxs = poly.maxs;
    leaf.faces = poly.faces;
    leaf.contents = contents;
    const std::int32_t leafIndex = static_cast<std::int32_t>(tree.leaves.size());
    tree.leaves.push_back(std::move(leaf));
    return bspEncodeLeaf(leafIndex);
}

struct SplitScore {
    int frontFaces = 0;
    int backFaces = 0;
    int splitFaces = 0;
    int onPlaneFaces = 0;
};

struct SplitPlaneBounds {
    Vector3 mins{};
    Vector3 maxs{};
};

struct SplitPlane {
    BspPlane plane{};
    bool hintOnly = false;
    // AABB of each distinct brush that contributed this plane, kept apart
    // rather than unioned together. A plane can only ever separate solid
    // from open space within reach of the brush it came from, so this bounds
    // where the plane is worth trying as a splitter — see the relevance
    // check in buildNode. Keeping bounds per-contributor (instead of one
    // union box) matters because unrelated brushes routinely share a plane
    // (e.g. every room's floor at the same height): unioning their boxes
    // would mark the plane "relevant" across the dead space between them,
    // including exterior void far from either brush.
    std::vector<SplitPlaneBounds> contributors;
};

SplitScore scoreSplitPlane(const BspPlane& plane, const Polyhedron& poly) {
    SplitScore s;
    for (const auto& face : poly.faces) {
        bool hasFront = false;
        bool hasBack = false;
        for (const Vector3& v : face) {
            const float d = planeDistance(plane, v);
            if (d > kPlaneEps) hasFront = true;
            else if (d < -kPlaneEps) hasBack = true;
        }
        if (hasFront && hasBack) ++s.splitFaces;
        else if (hasFront) ++s.frontFaces;
        else if (hasBack) ++s.backFaces;
        else ++s.onPlaneFaces;
    }
    return s;
}

float evaluateSplit(const SplitScore& s) {
    const int total = s.frontFaces + s.backFaces;
    if (total == 0) return 0.0f;
    const int lo = std::min(s.frontFaces, s.backFaces);
    const int hi = std::max(s.frontFaces, s.backFaces);
    const float balance = static_cast<float>(lo) / static_cast<float>(hi);
    const float splitPenalty = static_cast<float>(s.splitFaces) / static_cast<float>(total + s.splitFaces);
    return balance - splitPenalty;
}

float axialAlignment(const BspPlane& plane) {
    const float ax = std::fabs(plane.normal.x);
    const float ay = std::fabs(plane.normal.y);
    const float az = std::fabs(plane.normal.z);
    return std::max(ax, std::max(ay, az));
}

float contentSeparationBonus(
    const BspPlane& plane,
    const Polyhedron& poly,
    const std::vector<Brush>& sealing) {
    if (sealing.empty()) {
        return 0.0f;
    }
    int frontIn = 0;
    int frontOut = 0;
    int backIn = 0;
    int backOut = 0;
    std::vector<Vector3> samples;
    collectCellSamples(poly, samples);
    for (const Vector3& sample : samples) {
        const float d = planeDistance(plane, sample);
        if (std::fabs(d) <= kPlaneEps) {
            continue;
        }
        bool inside = false;
        for (const Brush& brush : sealing) {
            if (pointInsideBrushInclusive(sample, brush)) {
                inside = true;
                break;
            }
        }
        if (d > 0.0f) {
            if (inside) {
                ++frontIn;
            } else {
                ++frontOut;
            }
        } else if (inside) {
            ++backIn;
        } else {
            ++backOut;
        }
    }
    const bool frontSolidish = frontIn > 0 && frontOut == 0;
    const bool backSolidish = backIn > 0 && backOut == 0;
    const bool frontOpenish = frontOut > 0 && frontIn == 0;
    const bool backOpenish = backOut > 0 && backIn == 0;
    if ((frontSolidish && backOpenish) || (backSolidish && frontOpenish)) {
        return 0.35f;
    }
    if ((frontIn > 0) != (backIn > 0)) {
        return 0.15f;
    }
    return 0.0f;
}

bool planeCutsPolyhedron(const BspPlane& plane, const Polyhedron& poly) {
    bool anyFront = false;
    bool anyBack = false;
    for (const auto& face : poly.faces) {
        for (const Vector3& v : face) {
            const float d = planeDistance(plane, v);
            if (d > kPlaneEps) {
                anyFront = true;
            } else if (d < -kPlaneEps) {
                anyBack = true;
            }
            if (anyFront && anyBack) {
                return true;
            }
        }
    }
    return false;
}

// Cap on SplitPlane::contributors so the O(k) scan in splitPlaneRelevantToCell
// stays cheap even for a plane shared by an unbounded number of disjoint
// brushes (e.g. a pathological map with hundreds of unrelated same-height
// floor islands). Past this many distinct islands, mergeContributorBounds
// falls back to a single union box for the plane — same behavior as before
// per-contributor tracking existed, but only for that one outlier plane.
constexpr std::size_t kMaxSplitPlaneContributors = 24;

bool boundsOverlapOrTouch(Vector3 aMins, Vector3 aMaxs, Vector3 bMins, Vector3 bMaxs) {
    constexpr float kTouchPad = 1e-3f;
    return aMins.x <= bMaxs.x + kTouchPad && aMaxs.x >= bMins.x - kTouchPad
        && aMins.y <= bMaxs.y + kTouchPad && aMaxs.y >= bMins.y - kTouchPad
        && aMins.z <= bMaxs.z + kTouchPad && aMaxs.z >= bMins.z - kTouchPad;
}

// Folds a new brush AABB into the contributor list, merging it with any
// existing entries it overlaps/touches (cascading, since the merged box can
// then reach entries the original didn't) instead of appending unboundedly.
// This keeps contributors sized to the number of spatially separate islands
// on the plane rather than the number of brushes on it: a building with one
// contiguous floor at a shared height collapses to one box (its floor really
// is relevant everywhere in that box); two unrelated rooms that merely share
// a height stay as two boxes, so the gap between them still reads as
// irrelevant. Runs once per brush face at split-collection time, not on the
// buildNode hot path, so the O(k) cost here is a non-issue.
void mergeContributorBounds(
    std::vector<SplitPlaneBounds>& contributors,
    Vector3 brushMins,
    Vector3 brushMaxs) {
    Vector3 mergedMins = brushMins;
    Vector3 mergedMaxs = brushMaxs;
    for (std::size_t i = 0; i < contributors.size();) {
        if (boundsOverlapOrTouch(mergedMins, mergedMaxs, contributors[i].mins, contributors[i].maxs)) {
            mergedMins.x = std::min(mergedMins.x, contributors[i].mins.x);
            mergedMins.y = std::min(mergedMins.y, contributors[i].mins.y);
            mergedMins.z = std::min(mergedMins.z, contributors[i].mins.z);
            mergedMaxs.x = std::max(mergedMaxs.x, contributors[i].maxs.x);
            mergedMaxs.y = std::max(mergedMaxs.y, contributors[i].maxs.y);
            mergedMaxs.z = std::max(mergedMaxs.z, contributors[i].maxs.z);
            contributors.erase(contributors.begin() + static_cast<std::ptrdiff_t>(i));
            i = 0;
        } else {
            ++i;
        }
    }
    contributors.push_back(SplitPlaneBounds{mergedMins, mergedMaxs});

    if (contributors.size() > kMaxSplitPlaneContributors) {
        SplitPlaneBounds unioned = contributors.front();
        for (std::size_t i = 1; i < contributors.size(); ++i) {
            unioned.mins.x = std::min(unioned.mins.x, contributors[i].mins.x);
            unioned.mins.y = std::min(unioned.mins.y, contributors[i].mins.y);
            unioned.mins.z = std::min(unioned.mins.z, contributors[i].mins.z);
            unioned.maxs.x = std::max(unioned.maxs.x, contributors[i].maxs.x);
            unioned.maxs.y = std::max(unioned.maxs.y, contributors[i].maxs.y);
            unioned.maxs.z = std::max(unioned.maxs.z, contributors[i].maxs.z);
        }
        contributors.assign(1, unioned);
    }
}

void addUniqueSplit(
    std::vector<SplitPlane>& out,
    const BspPlane& plane,
    bool hintOnly,
    Vector3 brushMins,
    Vector3 brushMaxs) {
    if (length3(plane.normal) < 1e-6f) {
        return;
    }
    for (SplitPlane& existing : out) {
        if (planesEqual(existing.plane, plane)) {
            if (!hintOnly) {
                existing.hintOnly = false;
            }
            mergeContributorBounds(existing.contributors, brushMins, brushMaxs);
            return;
        }
    }
    out.push_back(SplitPlane{plane, hintOnly, {SplitPlaneBounds{brushMins, brushMaxs}}});
}

void collectSplits(const std::vector<Brush>& brushes, std::vector<SplitPlane>& out) {
    out.clear();
    for (const Brush& brush : brushes) {
        if (!brushRoleContributesSplits(brush.role)) {
            continue;
        }
        const bool hintOnly = brush.role == BrushRole::Hint;
        for (const BrushFace& face : brush.faces) {
            addUniqueSplit(out, planeFromFace(face), hintOnly, brush.mins, brush.maxs);
        }
    }
}

// Conservative (safe) filter: a plane can only correctly separate solid from
// open content within the AABB of the brush(es) it came from — outside that
// box the brush has no geometry, so the plane carries no information about
// where content actually is. Cheap AABB-vs-AABB test run before the O(faces)
// scoring below, so it also cuts scoring work, not just tree size.
bool splitPlaneRelevantToCell(const SplitPlane& split, const Polyhedron& poly) {
    constexpr float kRelevancePad = 1e-3f;
    for (const SplitPlaneBounds& b : split.contributors) {
        if (poly.mins.x <= b.maxs.x + kRelevancePad && poly.maxs.x >= b.mins.x - kRelevancePad
            && poly.mins.y <= b.maxs.y + kRelevancePad && poly.maxs.y >= b.mins.y - kRelevancePad
            && poly.mins.z <= b.maxs.z + kRelevancePad && poly.maxs.z >= b.mins.z - kRelevancePad) {
            return true;
        }
    }
    return false;
}

std::int32_t buildNode(
    BspTree& tree,
    const Polyhedron& poly,
    const BuildBrushes& brushes,
    const std::vector<SplitPlane>& splits,
    std::vector<std::uint8_t>& used) {
    const CellClassification classification = classifyAgainstSealing(poly, brushes.sealing);
    if (classification.cell == CellClass::WhollySolid) {
        std::uint32_t contents = BspContents::Solid;
        if (classification.preferGlass) {
            contents = BspContents::Glass;
        } else if (classification.preferDoor) {
            contents = BspContents::Solid | BspContents::Door;
        }
        return emitLeaf(tree, poly, contents);
    }

    BspPlane chosen{};
    bool found = false;
    float bestScore = -1e9f;
    std::size_t chosenIndex = 0;
    for (std::size_t pi = 0; pi < splits.size(); ++pi) {
        if (used[pi]) {
            continue;
        }
        if (!splitPlaneRelevantToCell(splits[pi], poly)) {
            continue;
        }
        if (!planeCutsPolyhedron(splits[pi].plane, poly)) {
            continue;
        }
        const SplitScore ss = scoreSplitPlane(splits[pi].plane, poly);
        float score = evaluateSplit(ss);
        score += 0.05f * axialAlignment(splits[pi].plane);
        score += contentSeparationBonus(splits[pi].plane, poly, brushes.sealing);
        if (splits[pi].hintOnly) {
            score -= 0.2f;
        }
        if (score > bestScore) {
            bestScore = score;
            chosen = splits[pi].plane;
            chosenIndex = pi;
            found = true;
        }
    }

    if (!found) {
        return emitLeaf(tree, poly, terminalLeafContents(poly, brushes, classification));
    }

    Polyhedron frontPoly;
    Polyhedron backPoly;
    if (!splitPolyhedron(poly, chosen, frontPoly, backPoly)) {
        return emitLeaf(tree, poly, terminalLeafContents(poly, brushes, classification));
    }

    used[chosenIndex] = 1;

    const std::int32_t nodeIndex = static_cast<std::int32_t>(tree.nodes.size());
    tree.nodes.push_back({});

    const std::int32_t front = buildNode(tree, frontPoly, brushes, splits, used);
    const std::int32_t back = buildNode(tree, backPoly, brushes, splits, used);

    used[chosenIndex] = 0;

    tree.nodes[static_cast<std::size_t>(nodeIndex)].plane = chosen;
    tree.nodes[static_cast<std::size_t>(nodeIndex)].front = front;
    tree.nodes[static_cast<std::size_t>(nodeIndex)].back = back;
    return nodeIndex;
}

bool polygonsOverlapCoplanar(
    const std::vector<Vector3>& a,
    const std::vector<Vector3>& b,
    Vector3 normal) {
    if (a.size() < 3 || b.size() < 3) {
        return false;
    }
    const Vector3 ca = polygonCentroid(a);
    const Vector3 cb = polygonCentroid(b);
    if (std::fabs(dot3(sub3(ca, cb), normal)) > kPlaneEps * 8.0f) {
        return false;
    }

    Vector3 tangent = std::fabs(normal.y) < 0.9f ? cross3(normal, {0.0f, 1.0f, 0.0f})
                                                 : cross3(normal, {1.0f, 0.0f, 0.0f});
    tangent = normalize3(tangent);
    const Vector3 bitangent = normalize3(cross3(normal, tangent));

    auto project = [&](const std::vector<Vector3>& poly, float& u0, float& u1, float& v0, float& v1) {
        u0 = u1 = dot3(poly[0], tangent);
        v0 = v1 = dot3(poly[0], bitangent);
        for (const Vector3& p : poly) {
            const float u = dot3(p, tangent);
            const float v = dot3(p, bitangent);
            u0 = std::min(u0, u);
            u1 = std::max(u1, u);
            v0 = std::min(v0, v);
            v1 = std::max(v1, v);
        }
    };

    float au0 = 0.0f;
    float au1 = 0.0f;
    float av0 = 0.0f;
    float av1 = 0.0f;
    float bu0 = 0.0f;
    float bu1 = 0.0f;
    float bv0 = 0.0f;
    float bv1 = 0.0f;
    project(a, au0, au1, av0, av1);
    project(b, bu0, bu1, bv0, bv1);
    return au0 < bu1 - kPlaneEps && bu0 < au1 - kPlaneEps && av0 < bv1 - kPlaneEps && bv0 < av1 - kPlaneEps;
}

std::vector<Vector3> intersectPortal(
    const std::vector<Vector3>& a,
    const std::vector<Vector3>& b,
    Vector3 normal) {
    if (a.size() < 3 || b.size() < 3) {
        return {};
    }

    auto clipToPoly = [&](std::vector<Vector3> subject, const std::vector<Vector3>& clip, Vector3 clipNormal) {
        for (std::size_t i = 0; i < clip.size(); ++i) {
            const Vector3& c0 = clip[i];
            const Vector3& c1 = clip[(i + 1) % clip.size()];
            const Vector3 edge = sub3(c1, c0);
            Vector3 inward = cross3(clipNormal, edge);
            const float inwardLen = length3(inward);
            if (inwardLen <= 1e-8f) {
                continue;
            }
            inward = scale3(inward, 1.0f / inwardLen);
            BspPlane edgePlane;
            edgePlane.normal = inward;
            edgePlane.distance = dot3(inward, c0);
            subject = clipPolygonAgainstPlane(subject, edgePlane, true);
            if (subject.size() < 3) {
                return std::vector<Vector3>{};
            }
        }
        return subject;
    };

    auto result = clipToPoly(a, b, normal);
    if (result.size() >= 3 && polygonArea(result) >= kMinFaceArea) {
        return result;
    }
    result = clipToPoly(a, b, scale3(normal, -1.0f));
    if (result.size() >= 3 && polygonArea(result) >= kMinFaceArea) {
        return result;
    }
    return {};
}

static bool boundsOverlap(const BspLeaf& a, const BspLeaf& b) {
    return a.mins.x <= b.maxs.x && a.maxs.x >= b.mins.x
        && a.mins.y <= b.maxs.y && a.maxs.y >= b.mins.y
        && a.mins.z <= b.maxs.z && a.maxs.z >= b.mins.z;
}

static bool portalConnectsLeaves(
    const BspTree& tree,
    std::int32_t leafA,
    std::int32_t leafB,
    const std::vector<Vector3>& portal,
    Vector3 normal) {
    if (portal.size() < 3) {
        return false;
    }
    const Vector3 center = polygonCentroid(portal);
    constexpr float kNudge = 0.02f;
    const Vector3 n = normalize3(normal);
    if (length3(n) < 1e-6f) {
        return false;
    }
    const Vector3 p0{center.x - n.x * kNudge, center.y - n.y * kNudge, center.z - n.z * kNudge};
    const Vector3 p1{center.x + n.x * kNudge, center.y + n.y * kNudge, center.z + n.z * kNudge};
    const std::int32_t l0 = pointLeaf(tree, p0);
    const std::int32_t l1 = pointLeaf(tree, p1);
    return (l0 == leafA && l1 == leafB) || (l0 == leafB && l1 == leafA);
}

static bool tryMakePortal(
    const BspTree& tree,
    std::int32_t leafA,
    std::int32_t leafB,
    const BspLeaf& a,
    const BspLeaf& b,
    std::vector<Vector3>& outPortal) {
    for (const auto& fa : a.faces) {
        const Vector3 na = polygonNormal(fa);
        if (length3(na) < 1e-6f) {
            continue;
        }
        for (const auto& fb : b.faces) {
            const Vector3 nb = polygonNormal(fb);
            if (dot3(na, nb) > -0.99f) {
                continue;
            }
            if (!polygonsOverlapCoplanar(fa, fb, na)) {
                continue;
            }
            std::vector<Vector3> portal = intersectPortal(fa, fb, na);
            if (portal.size() < 3) {
                portal = intersectPortal(fb, fa, nb);
            }
            if (portal.size() < 3) {
                continue;
            }
            if (!portalConnectsLeaves(tree, leafA, leafB, portal, na)) {
                continue;
            }
            outPortal = std::move(portal);
            return true;
        }
    }
    return false;
}

/** Open leaves plus Door-brush leaves: the set of leaves BspPortals should
 *  connect (see leafParticipatesInPortalGraph). */
static void collectPortalGraphLeaves(const BspTree& tree, std::int32_t child, std::vector<std::int32_t>& out) {
    if (bspIsLeafChild(child)) {
        const std::int32_t li = bspDecodeLeaf(child);
        if (leafParticipatesInPortalGraph(tree.leaves[static_cast<std::size_t>(li)].contents)) {
            out.push_back(li);
        }
        return;
    }
    const BspNode& node = tree.nodes[static_cast<std::size_t>(child)];
    collectPortalGraphLeaves(tree, node.front, out);
    collectPortalGraphLeaves(tree, node.back, out);
}

static void linkNeighbors(BspLeaf& a, BspLeaf& b, std::int32_t ai, std::int32_t bi) {
    for (std::int32_t n : a.neighbors) {
        if (n == bi) {
            return;
        }
    }
    a.neighbors.push_back(bi);
    b.neighbors.push_back(ai);
}

static void buildAdjacencyWalk(BspTree& tree, std::int32_t child) {
    if (bspIsLeafChild(child)) {
        return;
    }
    const BspNode& node = tree.nodes[static_cast<std::size_t>(child)];

    std::vector<std::int32_t> frontLeaves;
    std::vector<std::int32_t> backLeaves;
    collectPortalGraphLeaves(tree, node.front, frontLeaves);
    collectPortalGraphLeaves(tree, node.back, backLeaves);

    for (const std::int32_t fi : frontLeaves) {
        for (const std::int32_t bi : backLeaves) {
            BspLeaf& a = tree.leaves[static_cast<std::size_t>(fi)];
            BspLeaf& b = tree.leaves[static_cast<std::size_t>(bi)];
            if (!boundsOverlap(a, b)) {
                continue;
            }
            std::vector<Vector3> portalVerts;
            if (!tryMakePortal(tree, fi, bi, a, b, portalVerts)) {
                continue;
            }
            BspPortal portal;
            portal.leafA = fi;
            portal.leafB = bi;
            portal.vertices = std::move(portalVerts);
            tree.portals.push_back(std::move(portal));
            linkNeighbors(a, b, fi, bi);
        }
    }

    buildAdjacencyWalk(tree, node.front);
    buildAdjacencyWalk(tree, node.back);
}

void buildAdjacency(BspTree& tree) {
    tree.portals.clear();
    for (BspLeaf& leaf : tree.leaves) {
        leaf.neighbors.clear();
    }
    if (tree.root < 0) {
        return;
    }
    buildAdjacencyWalk(tree, tree.root);
}

bool hasInteriorOpenLeaf(const BspTree& tree, const std::vector<std::uint8_t>& exteriorEmpty) {
    for (std::size_t i = 0; i < tree.leaves.size(); ++i) {
        if (leafBlocksFlood(tree.leaves[i].contents)) {
            continue;
        }
        if (i >= exteriorEmpty.size() || exteriorEmpty[i] == 0) {
            return true;
        }
    }
    return false;
}

/** Bottom-up classification used by mergeExteriorLeaves: whether every leaf
 *  under a subtree is open and exterior, plus the subtree's combined bounds. */
struct SubtreeClass {
    bool fullyExteriorOpen = false;
    Vector3 mins{};
    Vector3 maxs{};
};

SubtreeClass classifyExteriorSubtree(
    const BspTree& tree,
    std::int32_t child,
    const std::vector<std::uint8_t>& exteriorEmpty,
    std::vector<SubtreeClass>& nodeClass) {
    if (bspIsLeafChild(child)) {
        const std::int32_t li = bspDecodeLeaf(child);
        const BspLeaf& leaf = tree.leaves[static_cast<std::size_t>(li)];
        SubtreeClass c;
        c.fullyExteriorOpen = leafIsOpen(leaf.contents)
            && static_cast<std::size_t>(li) < exteriorEmpty.size()
            && exteriorEmpty[static_cast<std::size_t>(li)] != 0;
        c.mins = leaf.mins;
        c.maxs = leaf.maxs;
        return c;
    }
    const BspNode& node = tree.nodes[static_cast<std::size_t>(child)];
    const SubtreeClass front = classifyExteriorSubtree(tree, node.front, exteriorEmpty, nodeClass);
    const SubtreeClass back = classifyExteriorSubtree(tree, node.back, exteriorEmpty, nodeClass);
    SubtreeClass c;
    c.fullyExteriorOpen = front.fullyExteriorOpen && back.fullyExteriorOpen;
    c.mins = {
        std::min(front.mins.x, back.mins.x),
        std::min(front.mins.y, back.mins.y),
        std::min(front.mins.z, back.mins.z),
    };
    c.maxs = {
        std::max(front.maxs.x, back.maxs.x),
        std::max(front.maxs.y, back.maxs.y),
        std::max(front.maxs.z, back.maxs.z),
    };
    nodeClass[static_cast<std::size_t>(child)] = c;
    return c;
}

std::int32_t rebuildMergedChild(
    const BspTree& src,
    std::int32_t child,
    const std::vector<SubtreeClass>& nodeClass,
    BspTree& dst) {
    if (bspIsLeafChild(child)) {
        BspLeaf copy = src.leaves[static_cast<std::size_t>(bspDecodeLeaf(child))];
        copy.neighbors.clear();
        const std::int32_t newIndex = static_cast<std::int32_t>(dst.leaves.size());
        dst.leaves.push_back(std::move(copy));
        return bspEncodeLeaf(newIndex);
    }

    const SubtreeClass& c = nodeClass[static_cast<std::size_t>(child)];
    if (c.fullyExteriorOpen) {
        // Whole subtree is unreachable exterior void: collapse it to a single
        // leaf. Faces are left empty — nothing downstream (pointLeaf, PVS,
        // nav) needs exact void polyhedron shape, and buildAdjacency will
        // skip generating portals for it since it has no faces to match.
        BspLeaf merged;
        merged.contents = 0;
        merged.mins = c.mins;
        merged.maxs = c.maxs;
        const std::int32_t newIndex = static_cast<std::int32_t>(dst.leaves.size());
        dst.leaves.push_back(std::move(merged));
        return bspEncodeLeaf(newIndex);
    }

    const BspNode& node = src.nodes[static_cast<std::size_t>(child)];
    const std::int32_t front = rebuildMergedChild(src, node.front, nodeClass, dst);
    const std::int32_t back = rebuildMergedChild(src, node.back, nodeClass, dst);
    const std::int32_t newNodeIndex = static_cast<std::int32_t>(dst.nodes.size());
    dst.nodes.push_back({});
    dst.nodes[static_cast<std::size_t>(newNodeIndex)].plane = node.plane;
    dst.nodes[static_cast<std::size_t>(newNodeIndex)].front = front;
    dst.nodes[static_cast<std::size_t>(newNodeIndex)].back = back;
    return newNodeIndex;
}

/** Collapses every maximal subtree that is entirely unreachable exterior
 *  void (per @p exteriorEmpty) into a single leaf. Only called once the hull
 *  is confirmed sealed, so exterior can never touch interior gameplay space —
 *  see hasInteriorOpenLeaf at the call site. Portals/neighbors are stale
 *  after this and must be rebuilt via buildAdjacency. */
void mergeExteriorLeaves(BspTree& tree, const std::vector<std::uint8_t>& exteriorEmpty) {
    if (tree.root < 0 || bspIsLeafChild(tree.root)) {
        return;
    }
    std::vector<SubtreeClass> nodeClass(tree.nodes.size());
    classifyExteriorSubtree(tree, tree.root, exteriorEmpty, nodeClass);

    BspTree rebuilt;
    rebuilt.boundsMins = tree.boundsMins;
    rebuilt.boundsMaxs = tree.boundsMaxs;
    rebuilt.root = rebuildMergedChild(tree, tree.root, nodeClass, rebuilt);
    tree.nodes = std::move(rebuilt.nodes);
    tree.leaves = std::move(rebuilt.leaves);
    tree.root = rebuilt.root;
}

void orientSurfaceWinding(BspSurfaceFace& face) {
    const Vector3 windingNormal = polygonNormal(face.vertices);
    if (dot3(windingNormal, face.normal) < 0.0f) {
        std::reverse(face.vertices.begin(), face.vertices.end());
    }
}

void buildSurfaceFaces(BspTree& tree, const std::vector<Brush>& surfaceBrushes) {
    tree.surfaceFaces.clear();
    std::vector<Vector3> probes;

    for (const Brush& brush : surfaceBrushes) {
        for (const BrushFace& brushFace : brush.faces) {
            if (brushFace.vertices.size() < 3) {
                continue;
            }
            collectFaceEmptyProbes(brushFace.vertices, brushFace.normal, probes);
            std::int32_t emptyLeaf = -1;
            for (const Vector3& probe : probes) {
                const std::int32_t leafIndex = pointLeaf(tree, probe);
                if (leafIndex < 0
                    || !leafIsOpen(tree.leaves[static_cast<std::size_t>(leafIndex)].contents)) {
                    continue;
                }
                emptyLeaf = leafIndex;
                break;
            }
            if (emptyLeaf < 0) {
                continue;
            }

            BspSurfaceFace face;
            face.vertices = brushFace.vertices;
            face.normal = brushFace.normal;
            face.emptyLeaf = emptyLeaf;
            face.id = brushFace.id;
            face.material = brushFace.material;
            face.uvShiftPixels = brushFace.uvShiftPixels;
            orientSurfaceWinding(face);
            tree.surfaceFaces.push_back(std::move(face));
        }
    }
}

} // namespace

void collectFaceEmptyProbes(
    const std::vector<Vector3>& vertices,
    Vector3 normal,
    std::vector<Vector3>& out) {
    out.clear();
    if (vertices.size() < 3) {
        return;
    }

    constexpr float kNudge = 0.05f;
    auto nudgePoint = [&](Vector3 p) {
        return Vector3{
            p.x + normal.x * kNudge,
            p.y + normal.y * kNudge,
            p.z + normal.z * kNudge,
        };
    };

    Vector3 sum{};
    for (const Vector3& v : vertices) {
        sum.x += v.x;
        sum.y += v.y;
        sum.z += v.z;
    }
    const float inv = 1.0f / static_cast<float>(vertices.size());
    out.push_back(nudgePoint(Vector3{sum.x * inv, sum.y * inv, sum.z * inv}));

    for (const Vector3& v : vertices) {
        out.push_back(nudgePoint(v));
    }

    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const Vector3& a = vertices[i];
        const Vector3& b = vertices[(i + 1) % vertices.size()];
        out.push_back(nudgePoint(Vector3{
            0.5f * (a.x + b.x),
            0.5f * (a.y + b.y),
            0.5f * (a.z + b.z),
        }));
    }
}

Vector3 leafCentroid(const BspLeaf& leaf) {
    Vector3 sum{};
    std::size_t count = 0;
    for (const auto& face : leaf.faces) {
        for (const Vector3& v : face) {
            sum.x += v.x;
            sum.y += v.y;
            sum.z += v.z;
            ++count;
        }
    }
    if (count == 0) {
        return {
            0.5f * (leaf.mins.x + leaf.maxs.x),
            0.5f * (leaf.mins.y + leaf.maxs.y),
            0.5f * (leaf.mins.z + leaf.maxs.z),
        };
    }
    const float inv = 1.0f / static_cast<float>(count);
    return {sum.x * inv, sum.y * inv, sum.z * inv};
}

/** Tags each BspPortal produced by a Door brush's closed shape with that brush's id,
 *  by matching portal-polygon centroids against the door brush's (padded) AABB. Lets
 *  downstream consumers (nav pathing, sound propagation) gate traversal on the door's
 *  live open/closed RigidMover state instead of treating every doorway as always-open. */
void linkDoorPortals(BspTree& tree, const std::vector<Brush>& brushes) {
    constexpr float kDoorPortalPad = 0.25f;
    int doorBrushCount = 0;
    int linkedPortalCount = 0;
    for (const Brush& brush : brushes) {
        if (brush.role != BrushRole::Door) {
            continue;
        }
        ++doorBrushCount;
        const Vector3 mins{
            brush.mins.x - kDoorPortalPad,
            brush.mins.y - kDoorPortalPad,
            brush.mins.z - kDoorPortalPad,
        };
        const Vector3 maxs{
            brush.maxs.x + kDoorPortalPad,
            brush.maxs.y + kDoorPortalPad,
            brush.maxs.z + kDoorPortalPad,
        };
        for (BspPortal& portal : tree.portals) {
            if (portal.vertices.empty()) {
                continue;
            }
            const Vector3 center = polygonCentroid(portal.vertices);
            if (center.x < mins.x || center.x > maxs.x
                || center.y < mins.y || center.y > maxs.y
                || center.z < mins.z || center.z > maxs.z) {
                continue;
            }
            portal.doorBrushId = brush.id;
            ++linkedPortalCount;
        }
    }
    TraceLog(
        LOG_INFO,
        "BSP: door portals linked=%d doorBrushes=%d",
        linkedPortalCount,
        doorBrushCount);
}

BspTree buildBspFromHullBrushes(const std::vector<Brush>& brushes) {
    BspTree tree;
    BuildBrushes buildBrushes;
    int detailCount = 0;
    int hintCount = 0;
    int triggerCount = 0;
    int waterCount = 0;
    int windowCount = 0;
    int doorCount = 0;
    int hullCount = 0;
    int boxCount = 0;
    int nocollideCount = 0;

    for (const Brush& brush : brushes) {
        if (brush.box) {
            ++boxCount;
        }
        if (brush.nocollide) {
            ++nocollideCount;
        }
        switch (brush.role) {
        case BrushRole::Hull:
            ++hullCount;
            buildBrushes.sealing.push_back(brush);
            buildBrushes.surface.push_back(brush);
            break;
        case BrushRole::Window:
            ++windowCount;
            buildBrushes.sealing.push_back(brush);
            buildBrushes.surface.push_back(brush);
            break;
        case BrushRole::Door:
            ++doorCount;
            // Sealing only, deliberately not .surface: a door's closed shape
            // should split leaf space (so the doorway gets a BspPortal like a
            // window does) but must not be baked as a permanent static
            // occluder into tree.surfaceFaces (lightmap bake / dynamic light
            // occlusion), since the door moves at runtime.
            buildBrushes.sealing.push_back(brush);
            break;
        case BrushRole::Transparent:
            ++detailCount;
            buildBrushes.sealing.push_back(brush);
            break;
        case BrushRole::Water:
            ++waterCount;
            buildBrushes.water.push_back(brush);
            break;
        case BrushRole::Trigger:
            ++triggerCount;
            buildBrushes.trigger.push_back(brush);
            break;
        case BrushRole::Hint:
            ++hintCount;
            break;
        case BrushRole::Detail:
            ++detailCount;
            break;
        }
    }

    TraceLog(
        LOG_INFO,
        "BSP: build start brushes=%d hull=%d window=%d door=%d water=%d hint=%d trigger=%d detail=%d box=%d nocollide=%d",
        static_cast<int>(brushes.size()),
        hullCount,
        windowCount,
        doorCount,
        waterCount,
        hintCount,
        triggerCount,
        detailCount,
        boxCount,
        nocollideCount);

    if (buildBrushes.sealing.empty()) {
        TraceLog(LOG_WARNING, "BSP: no sealing brushes; empty tree");
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
    for (const Brush& brush : buildBrushes.sealing) {
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

    TraceLog(
        LOG_INFO,
        "BSP: world bounds (%.2f,%.2f,%.2f)-(%.2f,%.2f,%.2f) pad=%.2f",
        mins.x,
        mins.y,
        mins.z,
        maxs.x,
        maxs.y,
        maxs.z,
        kBoundsPad);

    std::vector<SplitPlane> splits;
    collectSplits(brushes, splits);
    TraceLog(LOG_INFO, "BSP: split planes=%d", static_cast<int>(splits.size()));

    const Polyhedron world = makeBoundsPolyhedron(mins, maxs);
    std::vector<std::uint8_t> used(splits.size(), 0);
    TraceLog(LOG_INFO, "BSP: building nodes...");
    tree.root = buildNode(tree, world, buildBrushes, splits, used);
    TraceLog(LOG_INFO, "BSP: nodes=%d leaves=%d", static_cast<int>(tree.nodes.size()), static_cast<int>(tree.leaves.size()));
    TraceLog(LOG_INFO, "BSP: building adjacency...");
    buildAdjacency(tree);

    std::vector<std::uint8_t> exteriorFlood;
    floodExteriorLeaves(tree, exteriorFlood);
    if (hasInteriorOpenLeaf(tree, exteriorFlood)) {
        const int preNodes = static_cast<int>(tree.nodes.size());
        const int preLeaves = static_cast<int>(tree.leaves.size());
        mergeExteriorLeaves(tree, exteriorFlood);
        TraceLog(
            LOG_INFO,
            "BSP: exterior merge nodes=%d->%d leaves=%d->%d",
            preNodes,
            static_cast<int>(tree.nodes.size()),
            preLeaves,
            static_cast<int>(tree.leaves.size()));
        buildAdjacency(tree);
    } else {
        TraceLog(LOG_INFO, "BSP: exterior merge skipped (unsealed hull)");
    }

    linkDoorPortals(tree, brushes);
    TraceLog(LOG_INFO, "BSP: building surface faces...");
    buildSurfaceFaces(tree, buildBrushes.surface);

    int openLeaves = 0;
    int blockedLeaves = 0;
    for (const BspLeaf& leaf : tree.leaves) {
        if (leafBlocksFlood(leaf.contents)) {
            ++blockedLeaves;
        } else {
            ++openLeaves;
        }
    }

    TraceLog(
        LOG_INFO,
        "BSP: build done root=%d nodes=%d openLeaves=%d blockedLeaves=%d portals=%d surfaces=%d",
        tree.root,
        static_cast<int>(tree.nodes.size()),
        openLeaves,
        blockedLeaves,
        static_cast<int>(tree.portals.size()),
        static_cast<int>(tree.surfaceFaces.size()));
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
    while (!bspIsLeafChild(child)) {
        const BspNode& node = tree.nodes[static_cast<std::size_t>(child)];
        const float value = planeDistance(node.plane, point);
        child = value >= 0.0f ? node.front : node.back;
    }
    return bspDecodeLeaf(child);
}

bool leafIsEmpty(const BspTree& tree, std::int32_t leafIndex) {
    if (leafIndex < 0 || leafIndex >= static_cast<std::int32_t>(tree.leaves.size())) {
        return false;
    }
    return leafIsOpen(tree.leaves[static_cast<std::size_t>(leafIndex)].contents);
}

const std::vector<std::int32_t>& leafNeighbors(const BspTree& tree, std::int32_t leafIndex) {
    static const std::vector<std::int32_t> kEmpty;
    if (leafIndex < 0 || leafIndex >= static_cast<std::int32_t>(tree.leaves.size())) {
        return kEmpty;
    }
    return tree.leaves[static_cast<std::size_t>(leafIndex)].neighbors;
}

}
