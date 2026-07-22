#include "map/vis.hpp"

#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace slopengine {

namespace {

constexpr float kPlaneEps = 1e-4f;
constexpr float kMinFaceArea = 1e-6f;
constexpr float kNudge = 0.05f;
constexpr float kWeldEps = 1e-3f;
constexpr float kSliverAltitude = 1e-3f;
constexpr float kSliverAreaPerim2 = 1e-4f;

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

float polygonPerimeter(const std::vector<Vector3>& verts) {
    float peri = 0.0f;
    if (verts.size() < 2) {
        return peri;
    }
    for (std::size_t i = 0; i < verts.size(); ++i) {
        peri += length3(sub3(verts[(i + 1) % verts.size()], verts[i]));
    }
    return peri;
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

bool isInteriorEmpty(
    const BspTree& tree,
    const std::vector<std::uint8_t>& exteriorEmpty,
    std::int32_t leafIndex) {
    if (leafIndex < 0 || leafIndex >= static_cast<std::int32_t>(tree.leaves.size())) {
        return false;
    }
    if (leafBlocksFlood(tree.leaves[static_cast<std::size_t>(leafIndex)].contents)) {
        return false;
    }
    return exteriorEmpty[static_cast<std::size_t>(leafIndex)] == 0;
}

bool aabbOverlap(
    Vector3 aMins,
    Vector3 aMaxs,
    Vector3 bMins,
    Vector3 bMaxs,
    float pad) {
    return aMins.x <= bMaxs.x + pad && aMaxs.x >= bMins.x - pad
        && aMins.y <= bMaxs.y + pad && aMaxs.y >= bMins.y - pad
        && aMins.z <= bMaxs.z + pad && aMaxs.z >= bMins.z - pad;
}

void faceBounds(const std::vector<Vector3>& verts, Vector3& mins, Vector3& maxs) {
    mins = {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    maxs = {
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
    };
    for (const Vector3& v : verts) {
        mins.x = std::min(mins.x, v.x);
        mins.y = std::min(mins.y, v.y);
        mins.z = std::min(mins.z, v.z);
        maxs.x = std::max(maxs.x, v.x);
        maxs.y = std::max(maxs.y, v.y);
        maxs.z = std::max(maxs.z, v.z);
    }
}

BspPlane outwardPlaneFromLeafFace(const std::vector<Vector3>& face, Vector3 leafCentroid) {
    BspPlane plane;
    plane.normal = polygonNormal(face);
    if (face.empty() || length3(plane.normal) < 1e-8f) {
        return plane;
    }
    plane.distance = dot3(plane.normal, face[0]);
    if (planeDistance(plane, leafCentroid) > 0.0f) {
        plane.normal = scale3(plane.normal, -1.0f);
        plane.distance = dot3(plane.normal, face[0]);
    }
    return plane;
}

std::vector<Vector3> clipPolygonToLeaf(
    const std::vector<Vector3>& input,
    const BspLeaf& leaf) {
    std::vector<Vector3> current = input;
    const Vector3 centroid = leafCentroid(leaf);
    for (const std::vector<Vector3>& leafFace : leaf.faces) {
        if (leafFace.size() < 3) {
            continue;
        }
        const BspPlane plane = outwardPlaneFromLeafFace(leafFace, centroid);
        if (length3(plane.normal) < 1e-8f) {
            continue;
        }
        current = clipPolygonAgainstPlane(current, plane, false);
        if (current.size() < 3) {
            return {};
        }
    }
    if (polygonArea(current) < kMinFaceArea) {
        return {};
    }
    return current;
}

void orientToNormal(std::vector<Vector3>& verts, Vector3 desiredNormal) {
    const Vector3 n = polygonNormal(verts);
    if (dot3(n, desiredNormal) < 0.0f) {
        std::reverse(verts.begin(), verts.end());
    }
}

bool nearlyEqual(Vector3 a, Vector3 b, float eps) {
    return length3(sub3(a, b)) <= eps;
}

bool pointOnSegment(Vector3 p, Vector3 a, Vector3 b, float eps, float& tOut) {
    const Vector3 ab = sub3(b, a);
    const float abLenSq = dot3(ab, ab);
    if (abLenSq <= eps * eps) {
        return false;
    }
    const float t = dot3(sub3(p, a), ab) / abLenSq;
    if (t <= eps || t >= 1.0f - eps) {
        return false;
    }
    const Vector3 proj = add3(a, scale3(ab, t));
    if (length3(sub3(p, proj)) > eps) {
        return false;
    }
    tOut = t;
    return true;
}

bool sameUvFrame(const VisibleFace& a, const VisibleFace& b) {
    if (a.material != b.material || a.uvLock != b.uvLock) {
        return false;
    }
    if (std::fabs(a.uvShiftPixels.x - b.uvShiftPixels.x) > 1e-3f
        || std::fabs(a.uvShiftPixels.y - b.uvShiftPixels.y) > 1e-3f) {
        return false;
    }
    if (std::fabs(a.uvScale.x - b.uvScale.x) > 1e-3f
        || std::fabs(a.uvScale.y - b.uvScale.y) > 1e-3f) {
        return false;
    }
    if (dot3(a.normal, b.normal) < 0.999f) {
        return false;
    }
    if (a.uvLock) {
        if (dot3(a.uvUAxis, b.uvUAxis) < 0.999f || dot3(a.uvVAxis, b.uvVAxis) < 0.999f) {
            return false;
        }
    }
    return true;
}

bool findSharedEdge(
    const std::vector<Vector3>& a,
    const std::vector<Vector3>& b,
    std::size_t& aEdge,
    std::size_t& bEdge,
    bool& bReversed) {
    for (std::size_t i = 0; i < a.size(); ++i) {
        const Vector3& a0 = a[i];
        const Vector3& a1 = a[(i + 1) % a.size()];
        for (std::size_t j = 0; j < b.size(); ++j) {
            const Vector3& b0 = b[j];
            const Vector3& b1 = b[(j + 1) % b.size()];
            if (nearlyEqual(a0, b1, kWeldEps) && nearlyEqual(a1, b0, kWeldEps)) {
                aEdge = i;
                bEdge = j;
                bReversed = true;
                return true;
            }
            if (nearlyEqual(a0, b0, kWeldEps) && nearlyEqual(a1, b1, kWeldEps)) {
                aEdge = i;
                bEdge = j;
                bReversed = false;
                return true;
            }
        }
    }
    return false;
}

bool pointsColinear(Vector3 prev, Vector3 mid, Vector3 next, float eps) {
    const Vector3 ab = sub3(mid, prev);
    const Vector3 bc = sub3(next, mid);
    return length3(cross3(ab, bc)) <= eps * std::max(1.0f, length3(ab) * length3(bc));
}

std::vector<Vector3> mergePolygonsAcrossEdge(
    const std::vector<Vector3>& a,
    const std::vector<Vector3>& b,
    std::size_t aEdge,
    std::size_t bEdge,
    bool bReversed) {
    std::vector<Vector3> bUse = b;
    std::size_t bEdgeUse = bEdge;
    if (!bReversed) {
        std::reverse(bUse.begin(), bUse.end());
        bEdgeUse = static_cast<std::size_t>(bUse.size() - 1)
            - ((bEdge + 1) % b.size());
        bEdgeUse %= bUse.size();
    }

    std::vector<Vector3> out;
    out.reserve(a.size() + bUse.size() - 1);
    for (std::size_t k = 1; k < a.size(); ++k) {
        out.push_back(a[(aEdge + k) % a.size()]);
    }
    if (bUse.size() > 2 && !out.empty()) {
        const Vector3 mid = a[aEdge];
        const Vector3 next = bUse[(bEdgeUse + 2) % bUse.size()];
        if (!pointsColinear(out.back(), mid, next, kWeldEps)) {
            out.push_back(mid);
        }
    }
    for (std::size_t k = 2; k < bUse.size(); ++k) {
        out.push_back(bUse[(bEdgeUse + k) % bUse.size()]);
    }
    return out;
}

void collapseConsecutiveDuplicates(std::vector<Vector3>& verts, float eps) {
    if (verts.size() < 2) {
        return;
    }
    std::vector<Vector3> collapsed;
    collapsed.reserve(verts.size());
    for (const Vector3& vert : verts) {
        if (!collapsed.empty() && nearlyEqual(collapsed.back(), vert, eps)) {
            continue;
        }
        collapsed.push_back(vert);
    }
    if (collapsed.size() >= 2 && nearlyEqual(collapsed.front(), collapsed.back(), eps)) {
        collapsed.pop_back();
    }
    verts = std::move(collapsed);
}

bool removeOutAndBackSpikes(std::vector<Vector3>& verts, float eps) {
    if (verts.size() < 3) {
        return false;
    }
    bool removed = false;
    bool changed = true;
    while (changed && verts.size() >= 3) {
        changed = false;
        for (std::size_t i = 0; i < verts.size(); ++i) {
            const Vector3& a = verts[i];
            const Vector3& c = verts[(i + 2) % verts.size()];
            if (!nearlyEqual(a, c, eps)) {
                continue;
            }
            const std::size_t removeFirst = (i + 1) % verts.size();
            const std::size_t removeSecond = (i + 2) % verts.size();
            if (removeFirst < removeSecond) {
                verts.erase(verts.begin() + static_cast<std::ptrdiff_t>(removeSecond));
                verts.erase(verts.begin() + static_cast<std::ptrdiff_t>(removeFirst));
            } else {
                verts.erase(verts.begin() + static_cast<std::ptrdiff_t>(removeFirst));
                verts.erase(verts.begin() + static_cast<std::ptrdiff_t>(removeSecond));
            }
            removed = true;
            changed = true;
            break;
        }
        if (!changed) {
            for (std::size_t i = 0; i < verts.size(); ++i) {
                if (nearlyEqual(verts[i], verts[(i + 1) % verts.size()], eps)) {
                    verts.erase(verts.begin() + static_cast<std::ptrdiff_t>((i + 1) % verts.size()));
                    removed = true;
                    changed = true;
                    break;
                }
            }
        }
    }
    return removed;
}

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

Vec2 projectToFacePlane(Vector3 v, Vector3 normal) {
    const float ax = std::fabs(normal.x);
    const float ay = std::fabs(normal.y);
    const float az = std::fabs(normal.z);
    if (ax >= ay && ax >= az) {
        return {v.y, v.z};
    }
    if (ay >= ax && ay >= az) {
        return {v.x, v.z};
    }
    return {v.x, v.y};
}

float orient2d(Vec2 a, Vec2 b, Vec2 c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool segmentsProperlyIntersect2d(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    const float o1 = orient2d(a, b, c);
    const float o2 = orient2d(a, b, d);
    const float o3 = orient2d(c, d, a);
    const float o4 = orient2d(c, d, b);
    return (o1 * o2 < 0.0f) && (o3 * o4 < 0.0f);
}

bool hasOutAndBackSpike(const std::vector<Vector3>& verts, float eps) {
    if (verts.size() < 3) {
        return false;
    }
    for (std::size_t i = 0; i < verts.size(); ++i) {
        if (nearlyEqual(verts[i], verts[(i + 2) % verts.size()], eps)) {
            return true;
        }
    }
    return false;
}

bool hasNonAdjacentDuplicateVertices(const std::vector<Vector3>& verts, float eps) {
    const std::size_t n = verts.size();
    if (n < 4) {
        return false;
    }
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 2; j < n; ++j) {
            if (i == 0 && j == n - 1) {
                continue;
            }
            if (nearlyEqual(verts[i], verts[j], eps)) {
                return true;
            }
        }
    }
    return false;
}

bool isSimplePolygonRing(const std::vector<Vector3>& verts) {
    if (verts.size() < 3) {
        return false;
    }
    if (hasOutAndBackSpike(verts, kWeldEps)) {
        return false;
    }
    if (hasNonAdjacentDuplicateVertices(verts, kWeldEps)) {
        return false;
    }
    const Vector3 normal = polygonNormal(verts);
    if (length3(normal) < 1e-8f) {
        return false;
    }
    std::vector<Vec2> pts(verts.size());
    for (std::size_t i = 0; i < verts.size(); ++i) {
        pts[i] = projectToFacePlane(verts[i], normal);
    }
    const std::size_t n = pts.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = pts[i];
        const Vec2 b = pts[(i + 1) % n];
        for (std::size_t j = i + 1; j < n; ++j) {
            if ((j + 1) % n == i || (i + 1) % n == j) {
                continue;
            }
            const Vec2 c = pts[j];
            const Vec2 d = pts[(j + 1) % n];
            if (nearlyEqual(verts[i], verts[j], kWeldEps)
                || nearlyEqual(verts[i], verts[(j + 1) % n], kWeldEps)
                || nearlyEqual(verts[(i + 1) % n], verts[j], kWeldEps)
                || nearlyEqual(verts[(i + 1) % n], verts[(j + 1) % n], kWeldEps)) {
                continue;
            }
            if (segmentsProperlyIntersect2d(a, b, c, d)) {
                return false;
            }
        }
    }
    return true;
}

bool cleanupPolygonRing(std::vector<Vector3>& verts, Vector3 normal) {
    bool changed = true;
    int guard = 0;
    while (changed && guard < 64) {
        ++guard;
        changed = false;
        const std::size_t before = verts.size();
        collapseConsecutiveDuplicates(verts, kWeldEps);
        if (removeOutAndBackSpikes(verts, kWeldEps) || verts.size() != before) {
            changed = true;
        }
    }
    if (verts.size() < 3 || polygonArea(verts) < kMinFaceArea) {
        verts.clear();
        return false;
    }
    orientToNormal(verts, normal);
    return true;
}

bool acceptMergedPolygon(
    std::vector<Vector3>& combined,
    Vector3 normal,
    float areaA,
    float areaB) {
    if (!cleanupPolygonRing(combined, normal)) {
        return false;
    }
    if (!isSimplePolygonRing(combined)) {
        return false;
    }
    const float combinedArea = polygonArea(combined);
    const float sum = areaA + areaB;
    if (sum > kMinFaceArea && combinedArea < 0.5f * sum) {
        return false;
    }
    constexpr float kAreaInflateTol = 0.05f;
    if (sum > kMinFaceArea && combinedArea > sum * (1.0f + kAreaInflateTol)) {
        return false;
    }
    return true;
}

VisibleFace makeVisibleFromBrushFace(const BrushFace& face, std::string id) {
    VisibleFace out;
    out.id = std::move(id);
    out.sourceFaceId = face.id;
    out.material = face.material;
    out.normal = face.normal;
    out.vertices = face.vertices;
    out.uvShiftPixels = face.uvShiftPixels;
    out.uvScale = face.uvScale;
    out.uvUAxis = face.uvUAxis;
    out.uvVAxis = face.uvVAxis;
    out.uvLock = face.uvLock;
    return out;
}

struct ClippedFragment {
    std::vector<Vector3> vertices;
    std::int32_t interiorLeaf = -1;
};

std::vector<ClippedFragment> microMergeFragments(std::vector<ClippedFragment> fragments) {
    bool merged = true;
    while (merged) {
        merged = false;
        for (std::size_t i = 0; i < fragments.size() && !merged; ++i) {
            for (std::size_t j = i + 1; j < fragments.size(); ++j) {
                std::size_t aEdge = 0;
                std::size_t bEdge = 0;
                bool bReversed = false;
                if (!findSharedEdge(
                        fragments[i].vertices, fragments[j].vertices, aEdge, bEdge, bReversed)) {
                    continue;
                }
                const float areaA = polygonArea(fragments[i].vertices);
                const float areaB = polygonArea(fragments[j].vertices);
                auto combined = mergePolygonsAcrossEdge(
                    fragments[i].vertices,
                    fragments[j].vertices,
                    aEdge,
                    bEdge,
                    bReversed);
                const Vector3 normal = polygonNormal(fragments[i].vertices);
                if (!acceptMergedPolygon(combined, normal, areaA, areaB)) {
                    continue;
                }
                fragments[i].vertices = std::move(combined);
                if (fragments[i].interiorLeaf < 0) {
                    fragments[i].interiorLeaf = fragments[j].interiorLeaf;
                }
                fragments.erase(fragments.begin() + static_cast<std::ptrdiff_t>(j));
                merged = true;
                break;
            }
        }
    }
    return fragments;
}

bool pointOnSegmentInclusive(Vector3 p, Vector3 a, Vector3 b, float eps) {
    const Vector3 ab = sub3(b, a);
    const float abLenSq = dot3(ab, ab);
    if (abLenSq <= eps * eps) {
        return nearlyEqual(p, a, eps);
    }
    const float t = dot3(sub3(p, a), ab) / abLenSq;
    if (t < -eps || t > 1.0f + eps) {
        return false;
    }
    const Vector3 proj = add3(a, scale3(ab, std::clamp(t, 0.0f, 1.0f)));
    return length3(sub3(p, proj)) <= eps;
}

bool edgeOnPolygonBoundary(
    Vector3 e0,
    Vector3 e1,
    const std::vector<Vector3>& poly,
    float eps) {
    if (poly.size() < 2) {
        return false;
    }
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const Vector3& a = poly[i];
        const Vector3& b = poly[(i + 1) % poly.size()];
        if ((nearlyEqual(e0, a, eps) && nearlyEqual(e1, b, eps))
            || (nearlyEqual(e0, b, eps) && nearlyEqual(e1, a, eps))) {
            return true;
        }
        if (pointOnSegmentInclusive(e0, a, b, eps) && pointOnSegmentInclusive(e1, a, b, eps)) {
            return true;
        }
    }
    return false;
}

std::vector<Vector3> clipPolygonInsideBrush(
    const std::vector<Vector3>& input,
    const Brush& brush) {
    std::vector<Vector3> current = input;
    for (const BrushFace& face : brush.faces) {
        if (face.vertices.size() < 3 || length3(face.normal) < 1e-8f) {
            continue;
        }
        BspPlane plane;
        plane.normal = face.normal;
        plane.distance = dot3(face.normal, face.vertices[0]);
        current = clipPolygonAgainstPlane(current, plane, false);
        if (current.size() < 3) {
            return {};
        }
    }
    if (polygonArea(current) < kMinFaceArea) {
        return {};
    }
    return current;
}

std::vector<Vector3> clipPolygonInsideConvex(
    const std::vector<Vector3>& input,
    const std::vector<Vector3>& clip,
    Vector3 clipNormal) {
    std::vector<Vector3> subject = input;
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
            return {};
        }
    }
    if (polygonArea(subject) < kMinFaceArea) {
        return {};
    }
    return subject;
}

struct CuttingEdge {
    Vector3 a{};
    Vector3 b{};
};

std::vector<std::vector<Vector3>> subtractConvexHole(
    const std::vector<Vector3>& subject,
    const std::vector<Vector3>& hole,
    Vector3 faceNormal) {
    std::vector<std::vector<Vector3>> empty;
    if (subject.size() < 3 || hole.size() < 3) {
        return empty;
    }

    const float subjectArea = polygonArea(subject);
    const float holeArea = polygonArea(hole);
    if (subjectArea < kMinFaceArea) {
        return empty;
    }
    if (holeArea >= subjectArea - kMinFaceArea) {
        return empty;
    }

    std::vector<CuttingEdge> cuts;
    cuts.reserve(hole.size());
    bool allOnBoundary = true;
    for (std::size_t i = 0; i < hole.size(); ++i) {
        const Vector3& e0 = hole[i];
        const Vector3& e1 = hole[(i + 1) % hole.size()];
        if (edgeOnPolygonBoundary(e0, e1, subject, kWeldEps)) {
            continue;
        }
        allOnBoundary = false;
        cuts.push_back(CuttingEdge{e0, e1});
    }
    if (allOnBoundary) {
        return empty;
    }

    const Vector3 holeCentroid = polygonCentroid(hole);

    auto clipAwayFromHole = [&](const std::vector<Vector3>& poly, const CuttingEdge& cut, bool keepAway)
        -> std::vector<Vector3> {
        Vector3 edge = sub3(cut.b, cut.a);
        Vector3 n = cross3(faceNormal, edge);
        const float nLen = length3(n);
        if (nLen <= 1e-8f) {
            return keepAway ? poly : std::vector<Vector3>{};
        }
        n = scale3(n, 1.0f / nLen);
        BspPlane plane;
        plane.normal = n;
        plane.distance = dot3(n, cut.a);
        const bool holeInFront = planeDistance(plane, holeCentroid) > 0.0f;
        const bool keepFront = keepAway ? !holeInFront : holeInFront;
        return clipPolygonAgainstPlane(poly, plane, keepFront);
    };

    std::function<std::vector<std::vector<Vector3>>(
        const std::vector<Vector3>&,
        std::size_t)>
        recurse;
    recurse = [&](const std::vector<Vector3>& poly, std::size_t cutIndex)
        -> std::vector<std::vector<Vector3>> {
        if (poly.size() < 3 || polygonArea(poly) < kMinFaceArea) {
            return {};
        }
        if (cutIndex >= cuts.size()) {
            auto stillCovered = clipPolygonInsideConvex(poly, hole, faceNormal);
            if (stillCovered.size() >= 3
                && polygonArea(stillCovered) >= polygonArea(poly) - kMinFaceArea) {
                return {};
            }
            return {poly};
        }

        auto away = clipAwayFromHole(poly, cuts[cutIndex], true);
        auto toward = clipAwayFromHole(poly, cuts[cutIndex], false);
        std::vector<std::vector<Vector3>> out;
        if (away.size() >= 3 && polygonArea(away) >= kMinFaceArea) {
            orientToNormal(away, faceNormal);
            out.push_back(std::move(away));
        }
        if (toward.size() >= 3 && polygonArea(toward) >= kMinFaceArea) {
            auto nested = recurse(toward, cutIndex + 1);
            out.insert(
                out.end(),
                std::make_move_iterator(nested.begin()),
                std::make_move_iterator(nested.end()));
        }
        return out;
    };

    return recurse(subject, 0);
}

void occludeFragmentsByBrushes(
    std::vector<ClippedFragment>& fragments,
    std::size_t sourceBrushIndex,
    const std::vector<Brush>& brushes,
    Vector3 faceNormal) {
    for (std::size_t bi = 0; bi < brushes.size(); ++bi) {
        if (bi == sourceBrushIndex) {
            continue;
        }
        const Brush& occluder = brushes[bi];
        if (!brushRoleEmitsVisFaces(occluder.role)) {
            continue;
        }

        std::vector<ClippedFragment> next;
        next.reserve(fragments.size());
        for (ClippedFragment& fragment : fragments) {
            Vector3 fMins{};
            Vector3 fMaxs{};
            faceBounds(fragment.vertices, fMins, fMaxs);
            if (!aabbOverlap(fMins, fMaxs, occluder.mins, occluder.maxs, kPlaneEps)) {
                next.push_back(std::move(fragment));
                continue;
            }

            auto covered = clipPolygonInsideBrush(fragment.vertices, occluder);
            if (covered.size() < 3 || polygonArea(covered) < kMinFaceArea) {
                next.push_back(std::move(fragment));
                continue;
            }

            const float subjectArea = polygonArea(fragment.vertices);
            const float coveredArea = polygonArea(covered);
            if (coveredArea >= subjectArea - kMinFaceArea) {
                continue;
            }

            orientToNormal(covered, faceNormal);
            auto remainders = subtractConvexHole(fragment.vertices, covered, faceNormal);
            for (std::vector<Vector3>& rem : remainders) {
                if (!cleanupPolygonRing(rem, faceNormal)) {
                    continue;
                }
                ClippedFragment out;
                out.vertices = std::move(rem);
                out.interiorLeaf = fragment.interiorLeaf;
                next.push_back(std::move(out));
            }
        }
        fragments = std::move(next);
        if (fragments.empty()) {
            return;
        }
    }
}

struct GridKey {
    int x = 0;
    int y = 0;
    int z = 0;

    bool operator==(const GridKey& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct GridKeyHash {
    std::size_t operator()(const GridKey& key) const {
        const std::size_t hx = static_cast<std::size_t>(key.x) * 73856093u;
        const std::size_t hy = static_cast<std::size_t>(key.y) * 19349663u;
        const std::size_t hz = static_cast<std::size_t>(key.z) * 83492791u;
        return hx ^ hy ^ hz;
    }
};

GridKey gridKeyOf(Vector3 v) {
    return GridKey{
        static_cast<int>(std::floor(v.x / kWeldEps)),
        static_cast<int>(std::floor(v.y / kWeldEps)),
        static_cast<int>(std::floor(v.z / kWeldEps)),
    };
}

void snapWeldVisibleFaceVertices(std::vector<VisibleFace>& faces) {
    std::unordered_map<GridKey, Vector3, GridKeyHash> reps;

    auto resolveRep = [&](Vector3 v) -> Vector3 {
        const GridKey center = gridKeyOf(v);
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const GridKey key{center.x + dx, center.y + dy, center.z + dz};
                    const auto it = reps.find(key);
                    if (it != reps.end() && nearlyEqual(it->second, v, kWeldEps)) {
                        return it->second;
                    }
                }
            }
        }
        reps.emplace(center, v);
        return v;
    };

    for (VisibleFace& face : faces) {
        for (Vector3& vert : face.vertices) {
            vert = resolveRep(vert);
        }
        std::vector<Vector3> collapsed;
        collapsed.reserve(face.vertices.size());
        for (const Vector3& vert : face.vertices) {
            if (!collapsed.empty() && nearlyEqual(collapsed.back(), vert, kWeldEps)) {
                continue;
            }
            collapsed.push_back(vert);
        }
        if (collapsed.size() >= 2 && nearlyEqual(collapsed.front(), collapsed.back(), kWeldEps)) {
            collapsed.pop_back();
        }
        face.vertices = std::move(collapsed);
    }

    faces.erase(
        std::remove_if(
            faces.begin(),
            faces.end(),
            [](const VisibleFace& face) { return face.vertices.size() < 3; }),
        faces.end());
}

bool isDegenerateOrSliver(const std::vector<Vector3>& verts) {
    if (verts.size() < 3) {
        return true;
    }
    const float area = polygonArea(verts);
    if (area < kMinFaceArea) {
        return true;
    }
    const float peri = polygonPerimeter(verts);
    if (peri > 1e-8f && (area / (peri * peri)) < kSliverAreaPerim2) {
        return true;
    }

    bool anyGoodAltitude = false;
    for (std::size_t i = 1; i + 1 < verts.size(); ++i) {
        const Vector3 ab = sub3(verts[i], verts[0]);
        const Vector3 ac = sub3(verts[i + 1], verts[0]);
        const Vector3 bc = sub3(verts[i + 1], verts[i]);
        const float triArea = 0.5f * length3(cross3(ab, ac));
        const float maxEdge = std::max(length3(ab), std::max(length3(ac), length3(bc)));
        if (maxEdge <= 1e-8f) {
            continue;
        }
        const float altitude = (2.0f * triArea) / maxEdge;
        if (altitude >= kSliverAltitude) {
            anyGoodAltitude = true;
            break;
        }
    }
    return !anyGoodAltitude;
}

void collectSourceParts(const std::string& sourceFaceId, std::unordered_set<std::string>& out) {
    std::string remaining = sourceFaceId;
    while (!remaining.empty()) {
        const auto plus = remaining.find('+');
        if (plus == std::string::npos) {
            out.insert(remaining);
            break;
        }
        out.insert(remaining.substr(0, plus));
        remaining = remaining.substr(plus + 1);
    }
}

void cullDegenerateVisibleFaces(
    std::vector<VisibleFace>& faces,
    std::vector<std::string>& inferredNodrawFaceIds) {
    std::unordered_set<std::string> before;
    for (const VisibleFace& face : faces) {
        collectSourceParts(face.sourceFaceId, before);
    }

    faces.erase(
        std::remove_if(
            faces.begin(),
            faces.end(),
            [](const VisibleFace& face) { return isDegenerateOrSliver(face.vertices); }),
        faces.end());

    std::unordered_set<std::string> after;
    for (const VisibleFace& face : faces) {
        collectSourceParts(face.sourceFaceId, after);
    }
    for (const std::string& source : before) {
        if (!after.contains(source)) {
            inferredNodrawFaceIds.push_back(source);
        }
    }
}

void sortVisibleFacesByMaterial(std::vector<VisibleFace>& faces) {
    std::sort(faces.begin(), faces.end(), [](const VisibleFace& a, const VisibleFace& b) {
        if (a.material != b.material) {
            return a.material < b.material;
        }
        return a.id < b.id;
    });
}

void finalizeInferredNodraw(std::vector<std::string>& inferredNodrawFaceIds) {
    std::sort(inferredNodrawFaceIds.begin(), inferredNodrawFaceIds.end());
    inferredNodrawFaceIds.erase(
        std::unique(inferredNodrawFaceIds.begin(), inferredNodrawFaceIds.end()),
        inferredNodrawFaceIds.end());
}

std::vector<ClippedFragment> clipFaceToInteriorLeaves(
    const BrushFace& face,
    const BspTree& tree,
    const MapHullAnalysis& analysis,
    const std::vector<std::int32_t>& interiorLeaves) {
    Vector3 faceMins{};
    Vector3 faceMaxs{};
    faceBounds(face.vertices, faceMins, faceMaxs);

    std::vector<ClippedFragment> fragments;
    for (std::int32_t leafIndex : interiorLeaves) {
        const BspLeaf& leaf = tree.leaves[static_cast<std::size_t>(leafIndex)];
        if (!aabbOverlap(faceMins, faceMaxs, leaf.mins, leaf.maxs, kNudge)) {
            continue;
        }
        auto clipped = clipPolygonToLeaf(face.vertices, leaf);
        if (clipped.size() < 3) {
            continue;
        }
        orientToNormal(clipped, face.normal);
        const Vector3 probe = add3(polygonCentroid(clipped), scale3(face.normal, kNudge));
        const std::int32_t probeLeaf = pointLeaf(tree, probe);
        if (!isInteriorEmpty(tree, analysis.exteriorEmpty, probeLeaf)) {
            continue;
        }
        if (polygonArea(clipped) < kMinFaceArea) {
            continue;
        }
        ClippedFragment fragment;
        fragment.vertices = std::move(clipped);
        fragment.interiorLeaf = probeLeaf;
        fragments.push_back(std::move(fragment));
    }
    return fragments;
}

} // namespace

void weldVisibleFaceTJunctions(std::vector<VisibleFace>& faces) {
    bool changed = true;
    int guard = 0;
    while (changed && guard < 64) {
        ++guard;
        changed = false;
        for (std::size_t fi = 0; fi < faces.size(); ++fi) {
            for (std::size_t vi = 0; vi < faces[fi].vertices.size(); ++vi) {
                const Vector3 p = faces[fi].vertices[vi];
                for (std::size_t fj = 0; fj < faces.size(); ++fj) {
                    if (fi == fj) {
                        continue;
                    }
                    auto& verts = faces[fj].vertices;
                    for (std::size_t ei = 0; ei < verts.size(); ++ei) {
                        const Vector3 a = verts[ei];
                        const Vector3 b = verts[(ei + 1) % verts.size()];
                        float t = 0.0f;
                        if (!pointOnSegment(p, a, b, kWeldEps, t)) {
                            continue;
                        }
                        bool already = false;
                        for (const Vector3& existing : verts) {
                            if (nearlyEqual(existing, p, kWeldEps)) {
                                already = true;
                                break;
                            }
                        }
                        if (already) {
                            continue;
                        }
                        verts.insert(verts.begin() + static_cast<std::ptrdiff_t>(ei + 1), p);
                        changed = true;
                    }
                }
            }
        }
    }
}

void mergeCoplanarVisibleFaces(std::vector<VisibleFace>& faces) {
    bool merged = true;
    while (merged) {
        merged = false;
        for (std::size_t i = 0; i < faces.size() && !merged; ++i) {
            for (std::size_t j = i + 1; j < faces.size(); ++j) {
                if (!sameUvFrame(faces[i], faces[j])) {
                    continue;
                }
                std::size_t aEdge = 0;
                std::size_t bEdge = 0;
                bool bReversed = false;
                if (!findSharedEdge(faces[i].vertices, faces[j].vertices, aEdge, bEdge, bReversed)) {
                    continue;
                }
                const float areaA = polygonArea(faces[i].vertices);
                const float areaB = polygonArea(faces[j].vertices);
                auto combined = mergePolygonsAcrossEdge(
                    faces[i].vertices,
                    faces[j].vertices,
                    aEdge,
                    bEdge,
                    bReversed);
                if (!acceptMergedPolygon(combined, faces[i].normal, areaA, areaB)) {
                    continue;
                }
                faces[i].vertices = std::move(combined);
                if (faces[i].sourceFaceId != faces[j].sourceFaceId) {
                    std::string left = faces[i].sourceFaceId;
                    std::string right = faces[j].sourceFaceId;
                    if (left > right) {
                        std::swap(left, right);
                    }
                    faces[i].sourceFaceId = left + "+" + right;
                }
                faces.erase(faces.begin() + static_cast<std::ptrdiff_t>(j));
                merged = true;
                break;
            }
        }
    }

    for (VisibleFace& face : faces) {
        cleanupPolygonRing(face.vertices, face.normal);
    }
    faces.erase(
        std::remove_if(
            faces.begin(),
            faces.end(),
            [](const VisibleFace& face) {
                return face.vertices.size() < 3 || polygonArea(face.vertices) < kMinFaceArea;
            }),
        faces.end());

    std::sort(faces.begin(), faces.end(), [](const VisibleFace& a, const VisibleFace& b) {
        if (a.sourceFaceId != b.sourceFaceId) {
            return a.sourceFaceId < b.sourceFaceId;
        }
        const Vector3 ca = polygonCentroid(a.vertices);
        const Vector3 cb = polygonCentroid(b.vertices);
        if (ca.x != cb.x) {
            return ca.x < cb.x;
        }
        if (ca.y != cb.y) {
            return ca.y < cb.y;
        }
        return ca.z < cb.z;
    });

    std::unordered_map<std::string, int> counts;
    for (VisibleFace& face : faces) {
        const int index = counts[face.sourceFaceId]++;
        if (face.sourceFaceId.find('+') != std::string::npos) {
            face.id = "merge/" + std::to_string(index) + "/" + face.sourceFaceId;
        } else {
            face.id = face.sourceFaceId + "#" + std::to_string(index);
        }
    }
}

VisBuildResult buildVisibleFaces(
    const BspTree& tree,
    const MapHullAnalysis& analysis,
    const std::vector<Brush>& brushes) {
    VisBuildResult result;

    std::vector<std::int32_t> interiorLeaves;
    if (analysis.sealed) {
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(tree.leaves.size()); ++i) {
            if (isInteriorEmpty(tree, analysis.exteriorEmpty, i)) {
                interiorLeaves.push_back(i);
            }
        }
    }

    const bool canClip = analysis.sealed && !interiorLeaves.empty();

    for (std::size_t brushIndex = 0; brushIndex < brushes.size(); ++brushIndex) {
        const Brush& brush = brushes[brushIndex];
        if (!brushRoleEmitsVisFaces(brush.role)) {
            continue;
        }
        for (const BrushFace& face : brush.faces) {
            if (face.nodraw || face.vertices.size() < 3 || face.id.empty()) {
                continue;
            }

            if (!canClip) {
                result.vis.faces.push_back(makeVisibleFromBrushFace(face, face.id));
                continue;
            }

            std::vector<ClippedFragment> fragments =
                clipFaceToInteriorLeaves(face, tree, analysis, interiorLeaves);

            if (fragments.empty()) {
                result.inferredNodrawFaceIds.push_back(face.id);
                continue;
            }

            fragments = microMergeFragments(std::move(fragments));
            occludeFragmentsByBrushes(fragments, brushIndex, brushes, face.normal);

            if (fragments.empty()) {
                result.inferredNodrawFaceIds.push_back(face.id);
                continue;
            }

            std::sort(fragments.begin(), fragments.end(), [](const ClippedFragment& a, const ClippedFragment& b) {
                const Vector3 ca = polygonCentroid(a.vertices);
                const Vector3 cb = polygonCentroid(b.vertices);
                if (ca.x != cb.x) {
                    return ca.x < cb.x;
                }
                if (ca.y != cb.y) {
                    return ca.y < cb.y;
                }
                return ca.z < cb.z;
            });

            for (std::size_t i = 0; i < fragments.size(); ++i) {
                VisibleFace visible = makeVisibleFromBrushFace(face, face.id + "#" + std::to_string(i));
                visible.vertices = std::move(fragments[i].vertices);
                visible.interiorLeaf = fragments[i].interiorLeaf;
                result.vis.faces.push_back(std::move(visible));
            }
        }
    }

    weldVisibleFaceTJunctions(result.vis.faces);
    snapWeldVisibleFaceVertices(result.vis.faces);
    cullDegenerateVisibleFaces(result.vis.faces, result.inferredNodrawFaceIds);
    mergeCoplanarVisibleFaces(result.vis.faces);
    sortVisibleFacesByMaterial(result.vis.faces);
    finalizeInferredNodraw(result.inferredNodrawFaceIds);

    return result;
}

}
