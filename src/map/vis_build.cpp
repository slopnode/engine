#include "map/vis.hpp"

#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
    out.reserve(a.size() + bUse.size() - 2);
    for (std::size_t k = 1; k < a.size(); ++k) {
        out.push_back(a[(aEdge + k) % a.size()]);
    }
    for (std::size_t k = 2; k < bUse.size(); ++k) {
        out.push_back(bUse[(bEdgeUse + k) % bUse.size()]);
    }
    return out;
}

VisibleFace makeVisibleFromBrushFace(const BrushFace& face, std::string id) {
    VisibleFace out;
    out.id = std::move(id);
    out.sourceFaceId = face.id;
    out.material = face.material;
    out.normal = face.normal;
    out.vertices = face.vertices;
    out.uvShiftPixels = face.uvShiftPixels;
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
                auto combined = mergePolygonsAcrossEdge(
                    fragments[i].vertices,
                    fragments[j].vertices,
                    aEdge,
                    bEdge,
                    bReversed);
                if (combined.size() < 3 || polygonArea(combined) < kMinFaceArea) {
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
                auto combined = mergePolygonsAcrossEdge(
                    faces[i].vertices,
                    faces[j].vertices,
                    aEdge,
                    bEdge,
                    bReversed);
                if (combined.size() < 3 || polygonArea(combined) < kMinFaceArea) {
                    continue;
                }
                orientToNormal(combined, faces[i].normal);
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

    for (const Brush& brush : brushes) {
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
