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
}

}
