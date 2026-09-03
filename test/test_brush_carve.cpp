#include "test_assert.hpp"

#include "map/brush.hpp"
#include "map/brush_carve.hpp"

#include <cmath>

namespace slopengine {

namespace {

bool approxEq(float a, float b) {
    return std::fabs(a - b) < 1e-3f;
}

bool vecApproxEq(Vector3 a, Vector3 b) {
    return approxEq(a.x, b.x) && approxEq(a.y, b.y) && approxEq(a.z, b.z);
}

bool facesExactlyEqual(const BrushFace& a, const BrushFace& b) {
    if (a.id != b.id || a.vertices.size() != b.vertices.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.vertices.size(); ++i) {
        if (!vecApproxEq(a.vertices[i], b.vertices[i])) {
            return false;
        }
    }
    return true;
}

bool brushExactlyEqual(const Brush& a, const Brush& b) {
    if (a.faces.size() != b.faces.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.faces.size(); ++i) {
        if (!facesExactlyEqual(a.faces[i], b.faces[i])) {
            return false;
        }
    }
    return true;
}

bool faceAllXNear(const BrushFace& face, float x) {
    if (face.vertices.empty()) {
        return false;
    }
    for (const Vector3& v : face.vertices) {
        if (!approxEq(v.x, x)) {
            return false;
        }
    }
    return true;
}

bool brushHasFaceAllXNear(const Brush& brush, float x) {
    for (const BrushFace& face : brush.faces) {
        if (faceAllXNear(face, x)) {
            return true;
        }
    }
    return false;
}

float polygonArea(const std::vector<Vector3>& verts) {
    if (verts.size() < 3) {
        return 0.0f;
    }
    Vector3 accum{0.0f, 0.0f, 0.0f};
    for (std::size_t i = 1; i + 1 < verts.size(); ++i) {
        const Vector3 e1{
            verts[i].x - verts[0].x, verts[i].y - verts[0].y, verts[i].z - verts[0].z};
        const Vector3 e2{
            verts[i + 1].x - verts[0].x, verts[i + 1].y - verts[0].y, verts[i + 1].z - verts[0].z};
        const Vector3 c{
            e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
        accum.x += c.x;
        accum.y += c.y;
        accum.z += c.z;
    }
    return 0.5f * std::sqrt(accum.x * accum.x + accum.y * accum.y + accum.z * accum.z);
}

} // namespace

void runBrushCarveTests() {
    // clipPolygonOutsideBrush: fully outside a box carver -> unchanged single fragment.
    {
        const Brush carver = makeBrushBox("carver", {10.0f, 10.0f, 10.0f}, {12.0f, 12.0f, 12.0f}, "mat", {});
        const std::vector<Vector3> square{
            {0.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 4.0f}, {0.0f, 0.0f, 4.0f}};
        auto result = clipPolygonOutsideBrush(square, carver);
        CHECK_EQ(result.size(), 1u);
        if (!result.empty()) {
            CHECK_EQ(result[0].size(), square.size());
            CHECK(approxEq(polygonArea(result[0]), polygonArea(square)));
        }
    }

    // clipPolygonOutsideBrush: fully inside a box carver -> empty.
    {
        const Brush carver = makeBrushBox("carver", {-10.0f, -10.0f, -10.0f}, {10.0f, 10.0f, 10.0f}, "mat", {});
        const std::vector<Vector3> square{
            {0.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 4.0f}, {0.0f, 0.0f, 4.0f}};
        auto result = clipPolygonOutsideBrush(square, carver);
        CHECK(result.empty());
    }

    // clipPolygonOutsideBrush: half overlap -> single clipped rectangle.
    {
        const Brush carver = makeBrushBox("carver", {2.0f, -1.0f, -10.0f}, {6.0f, 1.0f, 10.0f}, "mat", {});
        const std::vector<Vector3> square{
            {0.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 4.0f}, {0.0f, 0.0f, 4.0f}};
        auto result = clipPolygonOutsideBrush(square, carver);
        CHECK_EQ(result.size(), 1u);
        if (!result.empty()) {
            const float expectedArea = 2.0f * 4.0f;
            CHECK(approxEq(polygonArea(result[0]), expectedArea));
            for (const Vector3& v : result[0]) {
                CHECK(v.x <= 2.0f + 1e-3f);
            }
        }
    }

    // clipPolygonOutsideBrush: corner overlap -> two fragments (L-shape), total area preserved
    // minus the embedded corner.
    {
        const Brush carver = makeBrushBox("carver", {2.0f, -1.0f, 2.0f}, {6.0f, 1.0f, 6.0f}, "mat", {});
        const std::vector<Vector3> square{
            {0.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 4.0f}, {0.0f, 0.0f, 4.0f}};
        auto result = clipPolygonOutsideBrush(square, carver);
        CHECK_EQ(result.size(), 2u);
        float totalArea = 0.0f;
        for (const auto& frag : result) {
            totalArea += polygonArea(frag);
        }
        const float expectedArea = 16.0f - 4.0f;
        CHECK(approxEq(totalArea, expectedArea));
    }

    // carveBrushes: two Hull boxes sharing an exact boundary plane -> exactly one of the two
    // coincident boundary faces survives (the later brush's, since the earlier one gets carved
    // away by it).
    {
        Brush a = makeBrushBox("a", {0.0f, 0.0f, 0.0f}, {4.0f, 4.0f, 4.0f}, "mat", {});
        Brush b = makeBrushBox("b", {4.0f, 0.0f, 0.0f}, {8.0f, 4.0f, 4.0f}, "mat", {});
        auto carved = carveBrushes({a, b});
        CHECK_EQ(carved.size(), 2u);
        if (carved.size() == 2) {
            CHECK_FALSE(brushHasFaceAllXNear(carved[0], 4.0f));
            CHECK(brushHasFaceAllXNear(carved[1], 4.0f));
        }
    }

    // carveBrushes: no surviving face vertex of any brush lies inside another brush's volume.
    {
        Brush a = makeBrushBox("a", {0.0f, 0.0f, 0.0f}, {4.0f, 4.0f, 4.0f}, "mat", {});
        Brush b = makeBrushBox("b", {2.0f, 0.0f, 0.0f}, {6.0f, 4.0f, 4.0f}, "mat", {});
        auto carved = carveBrushes({a, b});
        CHECK_EQ(carved.size(), 2u);
        for (std::size_t i = 0; i < carved.size(); ++i) {
            for (std::size_t j = 0; j < carved.size(); ++j) {
                if (i == j) {
                    continue;
                }
                for (const BrushFace& face : carved[i].faces) {
                    for (const Vector3& v : face.vertices) {
                        // Boundary-touching vertices are expected (that's the clip seam
                        // itself); only reject vertices strictly, meaningfully inside.
                        CHECK_FALSE(pointInsideBrush(v, carved[j], -1e-3f));
                    }
                }
            }
        }
    }

    // carveBrushes: fully-nested Hull-in-Hull -- inner (declared first, so it's carved against
    // the later, containing outer) ends up with zero faces; outer is untouched.
    {
        Brush inner = makeBrushBox("inner", {-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, "mat", {});
        Brush outer = makeBrushBox("outer", {-2.0f, -2.0f, -2.0f}, {2.0f, 2.0f, 2.0f}, "mat", {});
        auto carved = carveBrushes({inner, outer});
        CHECK_EQ(carved.size(), 2u);
        if (carved.size() == 2) {
            CHECK(carved[0].faces.empty());
            CHECK(brushExactlyEqual(carved[1], outer));
        }
    }

    // carveBrushes: Trigger/Detail/Hint/Door overlapping a Hull are all left byte-unchanged --
    // non-eligible roles neither carve nor get carved.
    {
        Brush floor = makeBrushBox("floor", {0.0f, 0.0f, 0.0f}, {4.0f, 1.0f, 4.0f}, "mat", {}, BrushRole::Hull);
        Brush trig =
            makeBrushBox("trig", {0.0f, 0.0f, 0.0f}, {4.0f, 1.0f, 4.0f}, "mat", {}, BrushRole::Trigger);
        Brush detail =
            makeBrushBox("detail", {0.0f, 0.0f, 0.0f}, {4.0f, 1.0f, 4.0f}, "mat", {}, BrushRole::Detail);
        Brush hint =
            makeBrushBox("hint", {0.0f, 0.0f, 0.0f}, {4.0f, 1.0f, 4.0f}, "mat", {}, BrushRole::Hint);
        Brush door =
            makeBrushBox("door", {0.0f, 0.0f, 0.0f}, {4.0f, 1.0f, 4.0f}, "mat", {}, BrushRole::Door);
        auto carved = carveBrushes({floor, trig, detail, hint, door});
        CHECK_EQ(carved.size(), 5u);
        if (carved.size() == 5) {
            CHECK(brushExactlyEqual(carved[0], floor));
            CHECK(brushExactlyEqual(carved[1], trig));
            CHECK(brushExactlyEqual(carved[2], detail));
            CHECK(brushExactlyEqual(carved[3], hint));
            CHECK(brushExactlyEqual(carved[4], door));
        }
    }

    // carveBrushes: a doorway -- floor authored before a Door brush whose closed
    // volume overlaps the floor at the threshold, matching how doors are placed
    // in practice (extending down to floor level within the wall opening).
    // The floor must stay fully intact so the doorway remains walkable/rendered
    // while the door is open; a door must never carve a hole in the floor.
    {
        Brush floor = makeBrushBox("floor", {-4.0f, 0.0f, -4.0f}, {4.0f, 0.25f, 4.0f}, "mat", {}, BrushRole::Hull);
        Brush door =
            makeBrushBox("door", {-0.5f, 0.0f, -0.1f}, {0.5f, 3.0f, 0.1f}, "mat", {}, BrushRole::Door);
        auto carved = carveBrushes({floor, door});
        CHECK_EQ(carved.size(), 2u);
        if (carved.size() == 2) {
            CHECK(brushExactlyEqual(carved[0], floor));
            CHECK(brushExactlyEqual(carved[1], door));
        }
    }

    // carveBrushes: non-overlapping brushes -> no-op, output identical to input.
    {
        Brush a = makeBrushBox("a", {0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}, "mat", {});
        Brush b = makeBrushBox("b", {10.0f, 0.0f, 0.0f}, {12.0f, 2.0f, 2.0f}, "mat", {});
        auto carved = carveBrushes({a, b});
        CHECK_EQ(carved.size(), 2u);
        if (carved.size() == 2) {
            CHECK(brushExactlyEqual(carved[0], a));
            CHECK(brushExactlyEqual(carved[1], b));
            CHECK(carved[0].box);
            CHECK(carved[1].box);
        }
    }
}

}
