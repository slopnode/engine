#include "test_assert.hpp"

#include "map/brush.hpp"

#include <cmath>
#include <functional>
#include <string>

namespace slopengine {

namespace {

float signedDistance(Vector3 planePoint, Vector3 planeNormal, Vector3 p) {
    return (p.x - planePoint.x) * planeNormal.x + (p.y - planePoint.y) * planeNormal.y +
        (p.z - planePoint.z) * planeNormal.z;
}

bool allVertsOnSide(const Brush& brush, Vector3 planePoint, Vector3 planeNormal, bool front) {
    for (const BrushFace& face : brush.faces) {
        for (const Vector3& v : face.vertices) {
            const float d = signedDistance(planePoint, planeNormal, v);
            if (front) {
                if (d < -1e-3f) {
                    return false;
                }
            } else if (d > 1e-3f) {
                return false;
            }
        }
    }
    return true;
}

std::function<std::string()> makeIdAllocator(const char* prefix) {
    return [prefix, n = 0]() mutable { return std::string(prefix) + "-" + std::to_string(n++); };
}

bool hasFaceWithExactVerts(const Brush& brush, const std::vector<Vector3>& expected) {
    for (const BrushFace& face : brush.faces) {
        if (face.vertices.size() != expected.size()) {
            continue;
        }
        bool match = true;
        for (std::size_t i = 0; i < expected.size(); ++i) {
            const Vector3& a = face.vertices[i];
            const Vector3& b = expected[i];
            if (a.x != b.x || a.y != b.y || a.z != b.z) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

} // namespace

void runBrushSplitTests() {
    {
        const Brush box = makeBrushBox("box", {0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}, "mat/a", {});

        const Vector3 planePoint{1.0f, 0.0f, 0.0f};
        const Vector3 planeNormal{1.0f, 0.0f, 0.0f};
        std::string error;
        auto split = splitBrushByPlane(box, planePoint, planeNormal, makeIdAllocator("mid"), error);
        CHECK(split.has_value());
        if (!split) {
            return;
        }
        CHECK(error.empty());
        CHECK(!split->front.faces.empty());
        CHECK(!split->back.faces.empty());
        CHECK(allVertsOnSide(split->front, planePoint, planeNormal, true));
        CHECK(allVertsOnSide(split->back, planePoint, planeNormal, false));
        CHECK(split->front.faces.size() >= 4);
        CHECK(split->back.faces.size() >= 4);

        cleanupBrushGeometry(split->front, 0.25f);
        cleanupBrushGeometry(split->back, 0.25f);
        CHECK(!split->front.faces.empty());
        CHECK(!split->back.faces.empty());
        CHECK(allVertsOnSide(split->front, planePoint, planeNormal, true));
        CHECK(allVertsOnSide(split->back, planePoint, planeNormal, false));
    }

    {
        const Brush box = makeBrushBox("box", {0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}, "mat/a", {});
        const Vector3 planePoint{1.5f, 1.5f, 1.5f};
        const Vector3 planeNormal{
            1.0f / std::sqrt(3.0f),
            1.0f / std::sqrt(3.0f),
            1.0f / std::sqrt(3.0f),
        };
        std::string error;
        auto split =
            splitBrushByPlane(box, planePoint, planeNormal, makeIdAllocator("corner"), error);
        CHECK(split.has_value());
        if (!split) {
            return;
        }
        CHECK(error.empty());
        CHECK(!split->front.faces.empty());
        CHECK(!split->back.faces.empty());
        CHECK(allVertsOnSide(split->front, planePoint, planeNormal, true));
        CHECK(allVertsOnSide(split->back, planePoint, planeNormal, false));

        const bool frontIsTetra = split->front.faces.size() == 4;
        const bool backIsTetra = split->back.faces.size() == 4;
        CHECK(frontIsTetra || backIsTetra);
    }

    {
        Brush brush = makeBrushBox(
            "fin", {4.0f, 1.0f, -6.0f}, {6.0f, 3.0f, -4.0f}, "mat/a", {}, BrushRole::Detail);
        brush.box = false;
        brush.faces.push_back(BrushFace{});
        BrushFace& fin = brush.faces.back();
        fin.id = "fin/micro";
        fin.material = "mat/a";
        fin.vertices = {
            {4.0187f, 3.0f, -5.9626f},
            {4.0187f, 3.0f, -6.0f},
            {4.0f, 3.0f, -6.0f},
        };
        fin.normal = faceNormalFromVertices(fin.vertices);
        const std::size_t facesBefore = brush.faces.size();
        CHECK(hasFaceWithExactVerts(
            brush, {{4.0187f, 3.0f, -5.9626f}, {4.0187f, 3.0f, -6.0f}, {4.0f, 3.0f, -6.0f}}));

        cleanupBrushGeometry(brush, 0.25f);
        CHECK(brush.faces.size() == facesBefore);
        CHECK(hasFaceWithExactVerts(
            brush, {{4.0187f, 3.0f, -5.9626f}, {4.0187f, 3.0f, -6.0f}, {4.0f, 3.0f, -6.0f}}));
    }

    {
        std::string error;
        auto cylinder = makeBrushCylinder(
            "cyl",
            {0.0f, 0.0f, 0.0f},
            {1.0f, 2.0f, 1.0f},
            16,
            "mat/a",
            BrushRole::Detail,
            error);
        CHECK(cylinder.has_value());
        CHECK(error.empty());
        if (!cylinder) {
            return;
        }
        std::vector<Vector3> before;
        for (const BrushFace& face : cylinder->faces) {
            for (const Vector3& v : face.vertices) {
                before.push_back(v);
            }
        }
        CHECK(!before.empty());

        cleanupBrushGeometry(*cylinder, 0.1f);

        std::size_t i = 0;
        for (const BrushFace& face : cylinder->faces) {
            for (const Vector3& v : face.vertices) {
                CHECK(i < before.size());
                if (i >= before.size()) {
                    return;
                }
                CHECK(v.x == before[i].x);
                CHECK(v.y == before[i].y);
                CHECK(v.z == before[i].z);
                ++i;
            }
        }
        CHECK_EQ(i, before.size());
    }

    {
        // 2x2 footprint (square, per makeBrushSpiralStairs' contract), 4 units
        // tall, 0.5 step height -> 8 steps, 8 sides -> exactly one full turn.
        std::vector<Brush> spiral = makeBrushSpiralStairs(
            "spiral",
            {-1.0f, 0.0f, -1.0f},
            {1.0f, 4.0f, 1.0f},
            0.25f,
            0.5f,
            8,
            "mat/a",
            BrushRole::Detail);
        CHECK_EQ(spiral.size(), std::size_t{8});
        for (const Brush& step : spiral) {
            CHECK(!validateBrushConvex(step).has_value());
        }
        CHECK(spiral.back().maxs.y <= 4.0f + 1e-3f);
        CHECK(spiral.front().mins.y >= 0.0f - 1e-3f);

        // No inner radius: steps should still be valid (triangular bottom/top
        // instead of the trapezoid quad, no degenerate inner face).
        std::vector<Brush> solidCore = makeBrushSpiralStairs(
            "core",
            {-1.0f, 0.0f, -1.0f},
            {1.0f, 4.0f, 1.0f},
            0.0f,
            0.5f,
            8,
            "mat/a",
            BrushRole::Detail);
        CHECK_EQ(solidCore.size(), std::size_t{8});
        for (const Brush& step : solidCore) {
            CHECK(!validateBrushConvex(step).has_value());
        }

        // Inner radius at/past the footprint's derived outer radius is invalid.
        std::vector<Brush> badInner = makeBrushSpiralStairs(
            "bad",
            {-1.0f, 0.0f, -1.0f},
            {1.0f, 4.0f, 1.0f},
            1.0f,
            0.5f,
            8,
            "mat/a",
            BrushRole::Detail);
        CHECK(badInner.empty());
    }

    {
        std::vector<BrushFace> faces;
        BrushFace a{};
        a.vertices = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.5f, 0.0f, 1.0f}};
        faces.push_back(a);
        BrushFace b{};
        b.vertices = {{0.0f, 0.0f, 0.0f}, {0.5f, 1.0f, 0.5f}, {1.0f, 0.0f, 0.0f}};
        faces.push_back(b);
        BrushFace c{};
        c.vertices = {{1.0f, 0.0f, 0.0f}, {0.5f, 1.0f, 0.5f}, {0.5f, 0.0f, 1.0f}};
        faces.push_back(c);
        BrushFace d{};
        d.vertices = {{0.5f, 0.0f, 1.0f}, {0.5f, 1.0f, 0.5f}, {0.0f, 0.0f, 0.0f}};
        faces.push_back(d);
        Brush brush = finalizeBrushFaces("odd", std::move(faces), BrushRole::Detail);
        CHECK_EQ(brush.id, std::string("odd"));
        CHECK(brush.faces.size() == 4);
        CHECK(!brush.faces[0].id.empty());
    }

    {
        Brush brush = makeBrushBox("stale", {0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}, "mat/a", {});
        CHECK(!validateBrushConvex(brush).has_value());
        for (BrushFace& face : brush.faces) {
            face.normal = {-face.normal.x, -face.normal.y, -face.normal.z};
        }
        CHECK(!validateBrushConvex(brush).has_value());
    }

    {
        Brush neighbor = makeBrushBox("nbr", {4.0f, 0.0f, -6.0f}, {6.0f, 1.0f, -4.0f}, "mat/a", {});
        neighbor.box = false;
        neighbor.faces.clear();
        BrushFace nFace{};
        nFace.id = "nbr/0";
        nFace.material = "mat/a";
        nFace.vertices = {
            {5.58399f, 1.0f, -6.0f},
            {6.0f, 1.0f, -5.46513f},
            {6.0f, 1.0f, -4.0f},
            {4.0f, 1.0f, -6.0f},
        };
        nFace.normal = faceNormalFromVertices(nFace.vertices);
        neighbor.faces.push_back(nFace);

        Brush subject = makeBrushBox("subj", {4.0f, 1.0f, -6.0f}, {6.0f, 3.0f, -4.0f}, "mat/a", {});
        subject.box = false;
        for (BrushFace& face : subject.faces) {
            for (Vector3& v : face.vertices) {
                if (v.x == 4.0f && v.z == -6.0f && v.y == 1.0f) {
                    v = {5.6f, 1.0f, -6.0f};
                }
            }
        }
        bool hadDrift = false;
        for (const BrushFace& face : subject.faces) {
            for (const Vector3& v : face.vertices) {
                if (v.x == 5.6f && v.y == 1.0f && v.z == -6.0f) {
                    hadDrift = true;
                }
            }
        }
        CHECK(hadDrift);

        cleanupBrushGeometry(subject, 0.1f);

        bool matchedNeighbor = false;
        bool keptDrift = false;
        for (const BrushFace& face : subject.faces) {
            for (const Vector3& v : face.vertices) {
                if (v.x == 5.58399f && v.y == 1.0f && v.z == -6.0f) {
                    matchedNeighbor = true;
                }
                if (v.x == 5.6f && v.y == 1.0f && v.z == -6.0f) {
                    keptDrift = true;
                }
            }
        }
        CHECK(!matchedNeighbor);
        CHECK(keptDrift);
    }
}

}
