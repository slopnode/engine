#include "map/sound_propagation.hpp"

#include <algorithm>
#include <limits>
#include <queue>

namespace slopengine {

namespace {

struct SoundFrontierNode {
    int leaf = -1;
    float dist = 0.0f;
    bool operator>(const SoundFrontierNode& other) const { return dist > other.dist; }
};

} // namespace

std::vector<float> floodSound(
    const MapNavigation& nav,
    int originLeaf,
    float loudness,
    float falloffPerUnit,
    const DoorOpenQuery& isDoorOpen) {
    std::vector<float> perceived(
        static_cast<std::size_t>(std::max(nav.leafCount, 0)), 0.0f);
    if (originLeaf < 0 || originLeaf >= nav.leafCount || loudness <= 0.0f) {
        return perceived;
    }

    // Dijkstra from the origin leaf over the nav portal graph, converting
    // shortest cumulative distance into attenuated loudness as each leaf is
    // settled. Bounded by the distance at which loudness would hit zero, so
    // the frontier never expands past the point sound stops mattering.
    const float maxDist = falloffPerUnit > 0.0f
        ? loudness / falloffPerUnit
        : std::numeric_limits<float>::infinity();

    std::vector<float> bestDist(
        static_cast<std::size_t>(nav.leafCount), std::numeric_limits<float>::infinity());
    std::priority_queue<SoundFrontierNode, std::vector<SoundFrontierNode>, std::greater<>> open;
    bestDist[static_cast<std::size_t>(originLeaf)] = 0.0f;
    open.push(SoundFrontierNode{originLeaf, 0.0f});

    while (!open.empty()) {
        const SoundFrontierNode current = open.top();
        open.pop();
        if (current.dist > bestDist[static_cast<std::size_t>(current.leaf)]) {
            continue; // stale queue entry, a shorter path already settled this leaf
        }

        const float remaining = loudness - falloffPerUnit * current.dist;
        if (remaining <= 0.0f) {
            continue;
        }
        perceived[static_cast<std::size_t>(current.leaf)] =
            std::max(perceived[static_cast<std::size_t>(current.leaf)], remaining);

        for (const NavPortalLink& link : nav.adjacency[static_cast<std::size_t>(current.leaf)]) {
            const int next = link.neighborLeaf;
            if (next < 0 || next >= nav.leafCount) {
                continue;
            }
            if (isDoorOpen && !link.doorBrushId.empty() && !isDoorOpen(link.doorBrushId)) {
                continue;
            }
            const float tentative = current.dist + link.cost;
            if (tentative > maxDist) {
                continue;
            }
            if (tentative < bestDist[static_cast<std::size_t>(next)]) {
                bestDist[static_cast<std::size_t>(next)] = tentative;
                open.push(SoundFrontierNode{next, tentative});
            }
        }
    }

    return perceived;
}

}
