#include "test_assert.hpp"
#include "map_fixtures.hpp"

#include "map/bsp.hpp"
#include "map/bsp_analyze.hpp"
#include "map/nav_bake.hpp"

#include <cmath>
#include <deque>
#include <limits>
#include <unordered_set>

namespace slopengine {

namespace {

Vector3 polyCentroidOf(const NavBakePoly& poly) {
    Vector3 sum{};
    for (const Vector3& v : poly.vertices) {
        sum.x += v.x;
        sum.y += v.y;
        sum.z += v.z;
    }
    const float n = static_cast<float>(poly.vertices.size());
    return {sum.x / n, sum.y / n, sum.z / n};
}

int closestPolyByZ(const NavPolyMesh& mesh, float targetZ) {
    int best = -1;
    float bestDist = std::numeric_limits<float>::infinity();
    for (std::size_t i = 0; i < mesh.polys.size(); ++i) {
        const Vector3 c = polyCentroidOf(mesh.polys[i]);
        const float d = std::fabs(c.z - targetZ);
        if (d < bestDist) {
            bestDist = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

bool polyReachable(const NavPolyMesh& mesh, int from, int to) {
    if (from < 0 || to < 0) {
        return false;
    }
    std::unordered_set<int> visited;
    std::deque<int> queue;
    queue.push_back(from);
    visited.insert(from);
    while (!queue.empty()) {
        const int current = queue.front();
        queue.pop_front();
        if (current == to) {
            return true;
        }
        for (int neighbor : mesh.polys[static_cast<std::size_t>(current)].neighbors) {
            if (neighbor >= 0 && visited.insert(neighbor).second) {
                queue.push_back(neighbor);
            }
        }
    }
    return false;
}

}

void runNavBakeTests() {
    {
        const BspTree emptyTree = buildBspFromHullBrushes({});
        const std::optional<NavPolyMesh> mesh = buildNavPolyMesh(emptyTree, {});
        CHECK_FALSE(mesh.has_value());
    }

    {
        const std::vector<Brush> brushes = mapfixtures::straightStaircaseHallway(13);
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);
        const std::optional<NavPolyMesh> mesh = buildNavPolyMesh(tree, brushes, &analysis.exteriorEmpty);
        CHECK(mesh.has_value());
        if (!mesh.has_value()) {
            return;
        }
        CHECK_FALSE(mesh->polys.empty());
        CHECK_EQ(mesh->polyDetail.size(), mesh->polys.size());

        // Bottom of the approach (z near -2) to the far end of the landing
        // (z near 12.4) -- the whole staircase run should bake into one
        // connected walkable surface, not fragment per-tread the way the
        // BSP leaf/portal graph does.
        const int startPoly = closestPolyByZ(*mesh, -2.0f);
        const int endPoly = closestPolyByZ(*mesh, 12.4f);
        CHECK(polyReachable(*mesh, startPoly, endPoly));
    }

    {
        // Doorway must bake through as an open, connected corridor even
        // though a solid Door brush fills the gap -- door geometry must be
        // excluded from the obstacle soup, not rasterized as a wall.
        const std::vector<Brush> brushes = mapfixtures::sealedRoomWithInteriorDoor();
        const BspTree tree = buildBspFromHullBrushes(brushes);
        const MapHullAnalysis analysis = analyzeMapHull(tree, brushes);
        CHECK(analysis.sealed);
        const std::optional<NavPolyMesh> mesh = buildNavPolyMesh(tree, brushes, &analysis.exteriorEmpty);
        CHECK(mesh.has_value());
        if (!mesh.has_value()) {
            return;
        }
        const int northPoly = closestPolyByZ(*mesh, -3.0f);
        const int southPoly = closestPolyByZ(*mesh, 3.0f);
        CHECK(polyReachable(*mesh, northPoly, southPoly));
    }
}

}
