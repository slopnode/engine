#include "test_assert.hpp"

#include "map/brush.hpp"

#include <cmath>
#include <functional>
#include <string>

namespace slopengine {

namespace {

Vector3 add3(Vector3 a, Vector3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

float dot3(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 normalize3(Vector3 v) {
    const float len = std::sqrt(dot3(v, v));
    if (len <= 1e-8f) {
        return {};
    }
    return {v.x / len, v.y / len, v.z / len};
}

bool approxEq(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}

bool approxEq(Vector3 a, Vector3 b, float eps = 1e-3f) {
    return approxEq(a.x, b.x, eps) && approxEq(a.y, b.y, eps) && approxEq(a.z, b.z, eps);
}

bool loopContainsVertexNear(const std::vector<Vector3>& loop, Vector3 v) {
    for (const Vector3& p : loop) {
        if (approxEq(p, v)) {
            return true;
        }
    }
    return false;
}

const BrushFace* findFaceByNormal(const Brush& brush, Vector3 normal) {
    for (const BrushFace& face : brush.faces) {
        if (approxEq(face.normal, normal, 1e-2f)) {
            return &face;
        }
    }
    return nullptr;
}

} // namespace

void runBrushExtrudeTests() {
    // Straight (normal) extrude of a single box face: sweeping the top face
    // of a 2x2x2 box up by 1 should produce a valid convex brush occupying
    // exactly the [0,2]x[2,3]x[0,2] slab sitting on top of the source.
    {
        const Brush box = makeBrushBox("box", {0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}, "mat/a", {});
        const BrushFace* top = findFaceByNormal(box, {0.0f, 1.0f, 0.0f});
        CHECK(top != nullptr);
        if (!top) {
            return;
        }

        std::vector<const BrushFace*> selected{top};
        std::string traceError;
        auto loops = traceCoplanarFaceBoundary(selected, traceError);
        CHECK_EQ(loops.size(), std::size_t{1});
        CHECK(traceError.empty());
        if (loops.empty()) {
            return;
        }
        CHECK_EQ(loops[0].size(), top->vertices.size());

        std::vector<Vector3> directions(loops[0].size(), top->normal);
        std::string error;
        auto extruded = extrudeFacePolygon(
            "ext", loops[0], directions, top->normal, 1.0f, "mat/a", BrushRole::Detail, error);
        CHECK(extruded.has_value());
        CHECK(error.empty());
        if (!extruded) {
            return;
        }
        CHECK(!validateBrushConvex(*extruded).has_value());
        CHECK_EQ(extruded->faces.size(), std::size_t{6});
        recomputeBrushBounds(*extruded);
        CHECK(approxEq(extruded->mins, {0.0f, 2.0f, 0.0f}));
        CHECK(approxEq(extruded->maxs, {2.0f, 3.0f, 2.0f}));
    }

    // Two box faces flush against each other on the same plane should merge
    // into one hexagonal boundary loop (the shared edge cancels) rather than
    // two independent/overlapping polygons — this is the "coplanar faces"
    // extrude case: selecting adjacent faces on the same wall and extruding
    // them as one unit instead of two overlapping slivers.
    {
        const Brush boxA = makeBrushBox("a", {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, "mat/a", {});
        const Brush boxB = makeBrushBox("b", {1.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 1.0f}, "mat/a", {});
        const BrushFace* topA = findFaceByNormal(boxA, {0.0f, 1.0f, 0.0f});
        const BrushFace* topB = findFaceByNormal(boxB, {0.0f, 1.0f, 0.0f});
        CHECK(topA != nullptr);
        CHECK(topB != nullptr);
        if (!topA || !topB) {
            return;
        }

        std::vector<const BrushFace*> selected{topA, topB};
        std::string traceError;
        auto loops = traceCoplanarFaceBoundary(selected, traceError);
        CHECK_EQ(loops.size(), std::size_t{1});
        CHECK(traceError.empty());
        if (loops.empty()) {
            return;
        }
        // Shared edge at x=1 cancels; the merged outline still visits the two
        // collinear midpoints (1,1,0)/(1,1,1) since edges aren't simplified,
        // so the hexagon has 6 vertices, not 4.
        CHECK_EQ(loops[0].size(), std::size_t{6});
        for (Vector3 corner : {Vector3{0.0f, 1.0f, 0.0f},
                 Vector3{2.0f, 1.0f, 0.0f},
                 Vector3{2.0f, 1.0f, 1.0f},
                 Vector3{0.0f, 1.0f, 1.0f}}) {
            CHECK(loopContainsVertexNear(loops[0], corner));
        }

        std::vector<Vector3> directions(loops[0].size(), Vector3{0.0f, 1.0f, 0.0f});
        std::string error;
        auto extruded = extrudeFacePolygon(
            "merged",
            loops[0],
            directions,
            Vector3{0.0f, 1.0f, 0.0f},
            0.5f,
            "mat/a",
            BrushRole::Detail,
            error);
        CHECK(extruded.has_value());
        CHECK(error.empty());
        if (!extruded) {
            return;
        }
        CHECK(!validateBrushConvex(*extruded).has_value());
        recomputeBrushBounds(*extruded);
        CHECK(approxEq(extruded->mins, {0.0f, 1.0f, 0.0f}));
        CHECK(approxEq(extruded->maxs, {2.0f, 1.5f, 1.0f}));
    }

    // Non-coplanar faces are rejected with a clear error instead of silently
    // producing a bogus merge.
    {
        const Brush box = makeBrushBox("box", {0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}, "mat/a", {});
        const BrushFace* top = findFaceByNormal(box, {0.0f, 1.0f, 0.0f});
        const BrushFace* east = findFaceByNormal(box, {1.0f, 0.0f, 0.0f});
        CHECK(top != nullptr);
        CHECK(east != nullptr);
        if (!top || !east) {
            return;
        }
        std::vector<const BrushFace*> selected{top, east};
        std::string error;
        auto loops = traceCoplanarFaceBoundary(selected, error);
        CHECK(loops.empty());
        CHECK(!error.empty());
    }

    // Profile/taper extrude: a uniform per-vertex direction tilted off the
    // face normal must still land every far-cap vertex on the plane offset
    // by exactly `depth` along the normal (t = depth / dot(dir, normal)),
    // producing a sheared but still-planar, still-convex result.
    {
        const std::vector<Vector3> polygon{
            {0.0f, 0.0f, 0.0f},
            {2.0f, 0.0f, 0.0f},
            {2.0f, 0.0f, 2.0f},
            {0.0f, 0.0f, 2.0f},
        };
        const Vector3 normal{0.0f, 1.0f, 0.0f};
        const Vector3 tiltedDir = normalize3(Vector3{0.0f, 2.0f, 1.0f});
        const std::vector<Vector3> directions(polygon.size(), tiltedDir);

        std::string error;
        auto extruded =
            extrudeFacePolygon("taper", polygon, directions, normal, 2.0f, "mat/a", BrushRole::Detail, error);
        CHECK(extruded.has_value());
        CHECK(error.empty());
        if (!extruded) {
            return;
        }
        CHECK(!validateBrushConvex(*extruded).has_value());

        const BrushFace* far = nullptr;
        for (const BrushFace& face : extruded->faces) {
            if (face.vertices.size() == polygon.size() && dot3(face.normal, normal) > 0.5f) {
                far = &face;
                break;
            }
        }
        CHECK(far != nullptr);
        if (!far) {
            return;
        }
        // dir normalized to (0, 2, 1)/sqrt(5); t = 2 / (2/sqrt(5)) = sqrt(5),
        // so each far-cap vertex is the near vertex shifted by exactly (0, 2, 1).
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            const Vector3 expected = add3(polygon[i], Vector3{0.0f, 2.0f, 1.0f});
            bool matched = false;
            for (const Vector3& v : far->vertices) {
                if (approxEq(v, expected, 1e-2f)) {
                    matched = true;
                    break;
                }
            }
            CHECK(matched);
        }
    }

    // Direction count mismatch, non-positive depth, and a taper direction
    // parallel to the face plane must all fail cleanly rather than crash or
    // silently produce garbage geometry.
    {
        const std::vector<Vector3> polygon{
            {0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
        };
        const Vector3 normal{0.0f, 1.0f, 0.0f};

        std::string error;
        auto mismatched = extrudeFacePolygon(
            "bad-count",
            polygon,
            std::vector<Vector3>(polygon.size() - 1, normal),
            normal,
            1.0f,
            "mat/a",
            BrushRole::Detail,
            error);
        CHECK(!mismatched.has_value());
        CHECK(!error.empty());

        error.clear();
        auto zeroDepth = extrudeFacePolygon(
            "bad-depth",
            polygon,
            std::vector<Vector3>(polygon.size(), normal),
            normal,
            0.0f,
            "mat/a",
            BrushRole::Detail,
            error);
        CHECK(!zeroDepth.has_value());
        CHECK(!error.empty());

        error.clear();
        auto parallelDir = extrudeFacePolygon(
            "bad-dir",
            polygon,
            std::vector<Vector3>(polygon.size(), Vector3{1.0f, 0.0f, 0.0f}),
            normal,
            1.0f,
            "mat/a",
            BrushRole::Detail,
            error);
        CHECK(!parallelDir.has_value());
        CHECK(!error.empty());
    }

    // In-place reshape: moveBrushVertices must propagate a moved corner to
    // every face that shares it (faces store their own vertex copies, not
    // shared indices), not just the face the seed came from.
    {
        Brush box = makeBrushBox("box", {0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}, "mat/a", {});
        const BrushFace* top = findFaceByNormal(box, {0.0f, 1.0f, 0.0f});
        CHECK(top != nullptr);
        if (!top) {
            return;
        }
        const std::vector<Vector3> seeds = top->vertices;
        const std::vector<Vector3> deltas(seeds.size(), Vector3{0.0f, 1.0f, 0.0f});

        Brush moved = moveBrushVertices(box, seeds, deltas);
        CHECK(!validateBrushConvex(moved).has_value());
        recomputeBrushBounds(moved);
        CHECK(approxEq(moved.mins, {0.0f, 0.0f, 0.0f}));
        CHECK(approxEq(moved.maxs, {2.0f, 3.0f, 2.0f}));

        for (const Vector3& oldTop : seeds) {
            CHECK(!loopContainsVertexNear(
                [&] {
                    std::vector<Vector3> all;
                    for (const BrushFace& face : moved.faces) {
                        for (const Vector3& v : face.vertices) {
                            all.push_back(v);
                        }
                    }
                    return all;
                }(),
                oldTop));
        }

        // A side face (which shares two top corners with the moved face)
        // must reflect the new corner position too.
        const BrushFace* east = findFaceByNormal(moved, {1.0f, 0.0f, 0.0f});
        CHECK(east != nullptr);
        if (east) {
            bool sawRaisedCorner = false;
            for (const Vector3& v : east->vertices) {
                if (approxEq(v.y, 3.0f)) {
                    sawRaisedCorner = true;
                }
            }
            CHECK(sawRaisedCorner);
        }
    }
}

}
