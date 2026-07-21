#include "map/bsp.hpp"

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

bool leafCenterInsideAnyHull(const Polyhedron& poly, const std::vector<Brush>& hulls) {
    const Vector3 center = polyCentroid(poly);
    for (const Brush& brush : hulls) {
        if (pointInsideBrush(center, brush)) {
            return true;
        }
    }
    return false;
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

void collectSplits(const std::vector<Brush>& hulls, std::vector<BspPlane>& out) {
    out.clear();
    for (const Brush& brush : hulls) {
        for (const BrushFace& face : brush.faces) {
            BspPlane plane = planeFromFace(face);
            if (length3(plane.normal) < 1e-6f) {
                continue;
            }
            bool unique = true;
            for (const BspPlane& existing : out) {
                if (planesEqual(existing, plane)) {
                    unique = false;
                    break;
                }
            }
            if (unique) {
                out.push_back(plane);
            }
        }
    }
}

std::int32_t encodeLeaf(std::int32_t leafIndex) {
    return -leafIndex - 1;
}

bool isLeafChild(std::int32_t child) {
    return child < 0;
}

std::int32_t decodeLeaf(std::int32_t child) {
    return -child - 1;
}

std::int32_t buildNode(
    BspTree& tree,
    const Polyhedron& poly,
    const std::vector<Brush>& hulls,
    const std::vector<BspPlane>& splits) {
    BspPlane chosen{};
    bool found = false;
    for (const BspPlane& plane : splits) {
        if (!planeCutsPolyhedron(plane, poly)) {
            continue;
        }
        chosen = plane;
        found = true;
        break;
    }

    if (!found) {
        BspLeaf leaf;
        leaf.mins = poly.mins;
        leaf.maxs = poly.maxs;
        leaf.faces = poly.faces;
        leaf.solid = leafCenterInsideAnyHull(poly, hulls);
        const std::int32_t leafIndex = static_cast<std::int32_t>(tree.leaves.size());
        tree.leaves.push_back(std::move(leaf));
        return encodeLeaf(leafIndex);
    }

    Polyhedron frontPoly;
    Polyhedron backPoly;
    if (!splitPolyhedron(poly, chosen, frontPoly, backPoly)) {
        BspLeaf leaf;
        leaf.mins = poly.mins;
        leaf.maxs = poly.maxs;
        leaf.faces = poly.faces;
        leaf.solid = leafCenterInsideAnyHull(poly, hulls);
        const std::int32_t leafIndex = static_cast<std::int32_t>(tree.leaves.size());
        tree.leaves.push_back(std::move(leaf));
        return encodeLeaf(leafIndex);
    }

    const std::int32_t nodeIndex = static_cast<std::int32_t>(tree.nodes.size());
    tree.nodes.push_back({});

    const std::int32_t front = buildNode(tree, frontPoly, hulls, splits);
    const std::int32_t back = buildNode(tree, backPoly, hulls, splits);

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
    BspPlane plane;
    plane.normal = normal;
    plane.distance = dot3(normal, a[0]);

    auto clipToPoly = [&](std::vector<Vector3> subject, const std::vector<Vector3>& clip) {
        for (std::size_t i = 0; i < clip.size(); ++i) {
            const Vector3& c0 = clip[i];
            const Vector3& c1 = clip[(i + 1) % clip.size()];
            const Vector3 edge = sub3(c1, c0);
            const Vector3 inward = normalize3(cross3(normal, edge));
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

    auto result = clipToPoly(a, b);
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

void buildAdjacency(BspTree& tree) {
    const std::int32_t leafCount = static_cast<std::int32_t>(tree.leaves.size());
    for (std::int32_t i = 0; i < leafCount; ++i) {
        if (tree.leaves[static_cast<std::size_t>(i)].solid) {
            continue;
        }
        for (std::int32_t j = i + 1; j < leafCount; ++j) {
            if (tree.leaves[static_cast<std::size_t>(j)].solid) {
                continue;
            }
            if (!boundsOverlap(tree.leaves[static_cast<std::size_t>(i)], tree.leaves[static_cast<std::size_t>(j)])) {
                continue;
            }
            const BspLeaf& a = tree.leaves[static_cast<std::size_t>(i)];
            const BspLeaf& b = tree.leaves[static_cast<std::size_t>(j)];
            bool adjacent = false;
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
                    if (polygonsOverlapCoplanar(fa, fb, na)) {
                        adjacent = true;
                        break;
                    }
                }
                if (adjacent) {
                    break;
                }
            }
            if (adjacent) {
                tree.leaves[static_cast<std::size_t>(i)].neighbors.push_back(j);
                tree.leaves[static_cast<std::size_t>(j)].neighbors.push_back(i);
            }
        }
    }
}

void orientSurfaceWinding(BspSurfaceFace& face) {
    const Vector3 windingNormal = polygonNormal(face.vertices);
    if (dot3(windingNormal, face.normal) < 0.0f) {
        std::reverse(face.vertices.begin(), face.vertices.end());
    }
}

void assignSurfaceMaterials(BspTree& tree, const std::vector<Brush>& hulls) {
    for (std::size_t faceIndex = 0; faceIndex < tree.surfaceFaces.size(); ++faceIndex) {
        BspSurfaceFace& face = tree.surfaceFaces[faceIndex];
        const Vector3 faceCenter = polygonCentroid(face.vertices);
        const Vector3 solidProbe{
            faceCenter.x - face.normal.x * 0.02f,
            faceCenter.y - face.normal.y * 0.02f,
            faceCenter.z - face.normal.z * 0.02f,
        };

        const BrushFace* bestFace = nullptr;
        float bestScore = -1.0f;
        for (const Brush& brush : hulls) {
            if (!pointInsideBrushInclusive(solidProbe, brush)) {
                continue;
            }
            for (const BrushFace& brushFace : brush.faces) {
                const float alignment = dot3(brushFace.normal, face.normal);
                if (alignment < 0.5f) {
                    continue;
                }
                if (alignment > bestScore) {
                    bestScore = alignment;
                    bestFace = &brushFace;
                }
            }
        }

        if (bestFace != nullptr) {
            face.id = bestFace->id;
            face.material = bestFace->material;
            face.uvShiftPixels = bestFace->uvShiftPixels;
            face.normal = bestFace->normal;
        } else if (face.id.empty()) {
            face.id = "surface/" + std::to_string(faceIndex);
            face.material = "default/unassigned";
        }

        orientSurfaceWinding(face);
    }
}

void buildSurfaceFaces(BspTree& tree, const std::vector<Brush>& hulls) {
    tree.surfaceFaces.clear();
    std::vector<Vector3> probes;

    for (const Brush& brush : hulls) {
        for (const BrushFace& brushFace : brush.faces) {
            if (brushFace.vertices.size() < 3) {
                continue;
            }
            collectFaceEmptyProbes(brushFace.vertices, brushFace.normal, probes);
            std::int32_t emptyLeaf = -1;
            for (const Vector3& probe : probes) {
                const std::int32_t leafIndex = pointLeaf(tree, probe);
                if (leafIndex < 0 || tree.leaves[static_cast<std::size_t>(leafIndex)].solid) {
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

BspTree buildBspFromHullBrushes(const std::vector<Brush>& brushes) {
    BspTree tree;
    std::vector<Brush> hulls;
    hulls.reserve(brushes.size());
    int detailCount = 0;
    int boxCount = 0;
    int nocollideCount = 0;
    for (const Brush& brush : brushes) {
        if (brush.box) {
            ++boxCount;
        }
        if (brush.nocollide) {
            ++nocollideCount;
        }
        if (brush.role == BrushRole::Hull) {
            hulls.push_back(brush);
        } else {
            ++detailCount;
        }
    }

    TraceLog(
        LOG_INFO,
        "BSP: build start brushes=%d hull=%d detail=%d box=%d nocollide=%d",
        static_cast<int>(brushes.size()),
        static_cast<int>(hulls.size()),
        detailCount,
        boxCount,
        nocollideCount);

    if (hulls.empty()) {
        TraceLog(LOG_WARNING, "BSP: no hull brushes; empty tree");
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
    for (const Brush& brush : hulls) {
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

    std::vector<BspPlane> splits;
    collectSplits(hulls, splits);
    TraceLog(LOG_INFO, "BSP: split planes=%d", static_cast<int>(splits.size()));

    const Polyhedron world = makeBoundsPolyhedron(mins, maxs);
    TraceLog(LOG_INFO, "BSP: building nodes...");
    tree.root = buildNode(tree, world, hulls, splits);
    TraceLog(LOG_INFO, "BSP: nodes=%d leaves=%d", static_cast<int>(tree.nodes.size()), static_cast<int>(tree.leaves.size()));
    TraceLog(LOG_INFO, "BSP: building adjacency...");
    buildAdjacency(tree);
    TraceLog(LOG_INFO, "BSP: building surface faces...");
    buildSurfaceFaces(tree, hulls);

    int emptyLeaves = 0;
    int solidLeaves = 0;
    for (const BspLeaf& leaf : tree.leaves) {
        if (leaf.solid) {
            ++solidLeaves;
        } else {
            ++emptyLeaves;
        }
    }

    TraceLog(
        LOG_INFO,
        "BSP: build done root=%d nodes=%d emptyLeaves=%d solidLeaves=%d surfaces=%d",
        tree.root,
        static_cast<int>(tree.nodes.size()),
        emptyLeaves,
        solidLeaves,
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
    while (!isLeafChild(child)) {
        const BspNode& node = tree.nodes[static_cast<std::size_t>(child)];
        const float value = planeDistance(node.plane, point);
        child = value >= 0.0f ? node.front : node.back;
    }
    return decodeLeaf(child);
}

bool leafIsEmpty(const BspTree& tree, std::int32_t leafIndex) {
    if (leafIndex < 0 || leafIndex >= static_cast<std::int32_t>(tree.leaves.size())) {
        return false;
    }
    return !tree.leaves[static_cast<std::size_t>(leafIndex)].solid;
}

const std::vector<std::int32_t>& leafNeighbors(const BspTree& tree, std::int32_t leafIndex) {
    static const std::vector<std::int32_t> kEmpty;
    if (leafIndex < 0 || leafIndex >= static_cast<std::int32_t>(tree.leaves.size())) {
        return kEmpty;
    }
    return tree.leaves[static_cast<std::size_t>(leafIndex)].neighbors;
}

}
