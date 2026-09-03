#include "map/brush_carve.hpp"

#include <cmath>
#include <string>
#include <utility>

namespace slopengine {

namespace {

Vector3 sub3(Vector3 a, Vector3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vector3 cross3(Vector3 a, Vector3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

// Fan-triangulated polygon area; used to reject degenerate slivers (collinear or
// duplicated points) that clipping precision can produce but that carry no real
// surface area, matching splitBrushByPlane's own degenerate-fragment guard.
float polygonArea(const std::vector<Vector3>& verts) {
    if (verts.size() < 3) {
        return 0.0f;
    }
    Vector3 accum{0.0f, 0.0f, 0.0f};
    for (std::size_t i = 1; i + 1 < verts.size(); ++i) {
        const Vector3 c = cross3(sub3(verts[i], verts[0]), sub3(verts[i + 1], verts[0]));
        accum.x += c.x;
        accum.y += c.y;
        accum.z += c.z;
    }
    return 0.5f * std::sqrt(accum.x * accum.x + accum.y * accum.y + accum.z * accum.z);
}

constexpr float kMinFragmentArea = 1e-6f;

bool verticesEqual(const std::vector<Vector3>& a, const std::vector<Vector3>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].z != b[i].z) {
            return false;
        }
    }
    return true;
}

constexpr float kCarveEps = 1e-4f;

bool brushAabbOverlap(const Brush& a, const Brush& b) {
    return a.mins.x <= b.maxs.x + kCarveEps && a.maxs.x >= b.mins.x - kCarveEps
        && a.mins.y <= b.maxs.y + kCarveEps && a.maxs.y >= b.mins.y - kCarveEps
        && a.mins.z <= b.maxs.z + kCarveEps && a.maxs.z >= b.mins.z - kCarveEps;
}

bool carveEligible(BrushRole role) {
    // brushRoleSeals also includes Door, but a door's closed-volume bounding box
    // routinely overlaps the floor at its threshold (and the walls it's set
    // into) -- carving would eat a hole in the floor/wall right where players
    // and AI need to walk through, since nothing else fills that gap while the
    // door is open. Doors are excluded from carving entirely (neither carver
    // nor carvee); their own faces never feed tree.surfaceFaces or the nav
    // walkable soup anyway, so leaving them untouched costs nothing.
    return role == BrushRole::Hull || role == BrushRole::Window || role == BrushRole::Transparent;
}

// A fragment lying exactly on the clip plane (every vertex within kCarveEps) has
// distance 0 everywhere, which satisfies both clipPolygonAgainstPlane's front and
// back epsilon bands at once. Without this check an exactly-coincident boundary
// (two brushes sharing a wall, or matching Y/Z extents) would wrongly survive as
// "outside" instead of being treated as fully embedded.
bool hasStrictlyOutsideVertex(
    const std::vector<Vector3>& fragment,
    Vector3 planePoint,
    Vector3 planeNormal) {
    for (const Vector3& v : fragment) {
        if (planeSignedDistance(planePoint, planeNormal, v) > kCarveEps) {
            return true;
        }
    }
    return false;
}

} // namespace

std::vector<std::vector<Vector3>> clipPolygonOutsideBrush(
    const std::vector<Vector3>& polygon,
    const Brush& carver) {
    std::vector<std::vector<Vector3>> outside;
    std::vector<std::vector<Vector3>> inside{polygon};

    for (const BrushFace& face : carver.faces) {
        if (face.vertices.size() < 3 || inside.empty()) {
            continue;
        }
        const Vector3 planePoint = face.vertices.front();
        const Vector3 planeNormal = face.normal;

        std::vector<std::vector<Vector3>> nextInside;
        for (const std::vector<Vector3>& fragment : inside) {
            if (hasStrictlyOutsideVertex(fragment, planePoint, planeNormal)) {
                auto outsidePart = clipPolygonAgainstPlane(fragment, planePoint, planeNormal, true);
                if (outsidePart.size() >= 3 && polygonArea(outsidePart) >= kMinFragmentArea) {
                    outside.push_back(std::move(outsidePart));
                }
            }
            auto insidePart = clipPolygonAgainstPlane(fragment, planePoint, planeNormal, false);
            if (insidePart.size() >= 3 && polygonArea(insidePart) >= kMinFragmentArea) {
                nextInside.push_back(std::move(insidePart));
            }
        }
        inside = std::move(nextInside);
    }

    // Whatever's left in `inside` after every carver plane is fully embedded
    // in carver's solid volume -- discarded, not appended to `outside`.
    return outside;
}

std::vector<Brush> carveBrushes(const std::vector<Brush>& brushes) {
    std::vector<Brush> result;
    result.reserve(brushes.size());

    for (std::size_t i = 0; i < brushes.size(); ++i) {
        const Brush& source = brushes[i];
        if (!carveEligible(source.role)) {
            result.push_back(source);
            continue;
        }

        std::vector<BrushFace> newFaces;
        newFaces.reserve(source.faces.size());
        bool brushChanged = false;

        for (const BrushFace& face : source.faces) {
            std::vector<std::vector<Vector3>> fragments{face.vertices};

            for (std::size_t j = i + 1; j < brushes.size(); ++j) {
                const Brush& carver = brushes[j];
                if (!carveEligible(carver.role) || !brushAabbOverlap(source, carver)) {
                    continue;
                }
                std::vector<std::vector<Vector3>> next;
                for (const std::vector<Vector3>& fragment : fragments) {
                    auto clipped = clipPolygonOutsideBrush(fragment, carver);
                    for (auto& piece : clipped) {
                        next.push_back(std::move(piece));
                    }
                }
                fragments = std::move(next);
                if (fragments.empty()) {
                    break;
                }
            }

            if (fragments.size() == 1 && verticesEqual(fragments.front(), face.vertices)) {
                newFaces.push_back(face);
                continue;
            }

            brushChanged = true;
            for (std::size_t f = 0; f < fragments.size(); ++f) {
                BrushFace fragmentFace = face;
                fragmentFace.vertices = fragments[f];
                fragmentFace.normal = faceNormalFromVertices(fragmentFace.vertices);
                fragmentFace.id = face.id + "/" + std::to_string(f);
                newFaces.push_back(std::move(fragmentFace));
            }
        }

        if (!brushChanged) {
            result.push_back(source);
            continue;
        }

        Brush carved = source;
        carved.faces = std::move(newFaces);
        carved.box = false;
        if (!carved.faces.empty()) {
            // recomputeBrushBounds collapses mins/maxs to the origin for a faceless
            // brush; leave a fully-carved-away brush's bounds as the original solid's
            // instead, since that's the more conservative (and non-degenerate) answer.
            recomputeBrushBounds(carved);
            reclassifyBrushAsBox(carved);
        }
        result.push_back(std::move(carved));
    }

    return result;
}

}
