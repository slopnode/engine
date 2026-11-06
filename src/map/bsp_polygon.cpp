#include "map/bsp_polygon.hpp"

#include <algorithm>
#include <cmath>

namespace slopengine {

namespace {

constexpr float kSideEps = 1e-4f;

float planeDistanceOf(const BspPlane& plane, Vector3 p) {
    return plane.normal.x * p.x + plane.normal.y * p.y + plane.normal.z * p.z - plane.distance;
}

std::vector<Vector3> clipSide(const std::vector<Vector3>& input, const BspPlane& plane, bool keepFront) {
    std::vector<Vector3> output;
    output.reserve(input.size() + 2);
    for (std::size_t i = 0; i < input.size(); ++i) {
        const Vector3& a = input[i];
        const Vector3& b = input[(i + 1) % input.size()];
        const float da = planeDistanceOf(plane, a);
        const float db = planeDistanceOf(plane, b);
        const bool aIn = keepFront ? da >= -kSideEps : da <= kSideEps;
        const bool bIn = keepFront ? db >= -kSideEps : db <= kSideEps;
        if (aIn) {
            output.push_back(a);
        }
        if (aIn != bIn) {
            const float t = da / (da - db);
            output.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t});
        }
    }
    return output;
}

void descend(
    const BspTree& tree,
    std::int32_t child,
    std::vector<Vector3> polygon,
    std::vector<std::uint8_t>& seen,
    std::vector<std::int32_t>& out) {
    if (polygon.size() < 3) {
        return;
    }
    if (bspIsLeafChild(child)) {
        const std::int32_t leaf = bspDecodeLeaf(child);
        if (leaf < 0 || leaf >= static_cast<std::int32_t>(tree.leaves.size())) {
            return;
        }
        if (!leafParticipatesInPortalGraph(tree.leaves[static_cast<std::size_t>(leaf)].contents)) {
            return;
        }
        if (seen[static_cast<std::size_t>(leaf)] == 0) {
            seen[static_cast<std::size_t>(leaf)] = 1;
            out.push_back(leaf);
        }
        return;
    }
    const BspNode& node = tree.nodes[static_cast<std::size_t>(child)];
    bool anyFront = false;
    bool anyBack = false;
    bool anyOn = false;
    for (const Vector3& v : polygon) {
        const float d = planeDistanceOf(node.plane, v);
        if (d > kSideEps) {
            anyFront = true;
        } else if (d < -kSideEps) {
            anyBack = true;
        } else {
            anyOn = true;
        }
    }
    if (anyFront && !anyBack && !anyOn) {
        descend(tree, node.front, std::move(polygon), seen, out);
        return;
    }
    if (anyBack && !anyFront && !anyOn) {
        descend(tree, node.back, std::move(polygon), seen, out);
        return;
    }
    if (!anyFront && !anyBack) {
        descend(tree, node.front, polygon, seen, out);
        descend(tree, node.back, std::move(polygon), seen, out);
        return;
    }
    descend(tree, node.front, clipSide(polygon, node.plane, true), seen, out);
    descend(tree, node.back, clipSide(polygon, node.plane, false), seen, out);
}

}

void collectPolygonLeaves(
    const BspTree& tree,
    const std::vector<Vector3>& polygon,
    std::vector<std::int32_t>& out) {
    if (tree.root < 0 || tree.leaves.empty() || polygon.size() < 3) {
        return;
    }
    std::vector<std::uint8_t> seen(tree.leaves.size(), 0);
    for (std::int32_t leaf : out) {
        if (leaf >= 0 && leaf < static_cast<std::int32_t>(tree.leaves.size())) {
            seen[static_cast<std::size_t>(leaf)] = 1;
        }
    }
    descend(tree, tree.root, polygon, seen, out);
}

}
