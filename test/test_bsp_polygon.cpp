#include "test_assert.hpp"
#include "map_fixtures.hpp"

#include "map/bsp.hpp"
#include "map/bsp_polygon.hpp"
#include "map/pvs.hpp"

#include <algorithm>
#include <vector>

namespace slopengine {

namespace {

Vector3 nudged(Vector3 p, Vector3 normal, float amount) {
    return {p.x + normal.x * amount, p.y + normal.y * amount, p.z + normal.z * amount};
}

void checkFaceLeavesCoverSamples(const BspTree& tree, int& facesChecked, int& samplesChecked) {
    for (const BspSurfaceFace& face : tree.surfaceFaces) {
        if (face.vertices.size() < 3) {
            continue;
        }
        std::vector<Vector3> polygon;
        polygon.reserve(face.vertices.size());
        for (const Vector3& v : face.vertices) {
            polygon.push_back(nudged(v, face.normal, 0.01f));
        }
        std::vector<std::int32_t> leaves;
        collectPolygonLeaves(tree, polygon, leaves);
        ++facesChecked;

        const Vector3& a = face.vertices[0];
        for (std::size_t i = 1; i + 1 < face.vertices.size(); ++i) {
            const Vector3& b = face.vertices[i];
            const Vector3& c = face.vertices[i + 1];
            for (int u = 0; u <= 8; ++u) {
                for (int v = 0; u + v <= 8; ++v) {
                    const float fu = static_cast<float>(u) / 8.0f;
                    const float fv = static_cast<float>(v) / 8.0f;
                    const float fw = 1.0f - fu - fv;
                    const Vector3 point{
                        a.x * fw + b.x * fu + c.x * fv,
                        a.y * fw + b.y * fu + c.y * fv,
                        a.z * fw + b.z * fu + c.z * fv,
                    };
                    const std::int32_t leaf = pointLeaf(tree, nudged(point, face.normal, 0.01f));
                    if (leaf < 0 ||
                        !leafParticipatesInPortalGraph(tree.leaves[static_cast<std::size_t>(leaf)].contents)) {
                        continue;
                    }
                    ++samplesChecked;
                    CHECK(std::find(leaves.begin(), leaves.end(), leaf) != leaves.end());
                }
            }
        }
    }
}

}

void runBspPolygonTests() {
    {
        const std::vector<Brush> brushes = mapfixtures::straightStaircaseHallway(13);
        const BspTree tree = buildBspFromHullBrushes(brushes);
        int faces = 0;
        int samples = 0;
        checkFaceLeavesCoverSamples(tree, faces, samples);
        CHECK(faces > 0);
        CHECK(samples > 0);
    }

    {
        const std::vector<Brush> brushes = mapfixtures::sealedRoomWithWindow();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        int faces = 0;
        int samples = 0;
        checkFaceLeavesCoverSamples(tree, faces, samples);
        CHECK(faces > 0);
        CHECK(samples > 0);
    }

    {
        BspTree empty;
        std::vector<std::int32_t> leaves;
        collectPolygonLeaves(empty, {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}, leaves);
        CHECK(leaves.empty());
    }
}

}
