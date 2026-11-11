#include "map/map_areas.hpp"

#include <raylib.h>

#include <algorithm>
#include <tuple>
#include <utility>

namespace slopengine {

namespace {

bool isDoorLeaf(const BspTree& tree, std::int32_t leaf) {
    return leaf >= 0 && leaf < static_cast<std::int32_t>(tree.leaves.size()) &&
           (tree.leaves[static_cast<std::size_t>(leaf)].contents & BspContents::Door) != 0;
}

bool portalInGraph(const BspTree& tree, const BspPortal& portal) {
    const std::int32_t n = static_cast<std::int32_t>(tree.leaves.size());
    if (portal.leafA < 0 || portal.leafB < 0 || portal.leafA >= n || portal.leafB >= n) {
        return false;
    }
    return leafParticipatesInPortalGraph(tree.leaves[static_cast<std::size_t>(portal.leafA)].contents) &&
           leafParticipatesInPortalGraph(tree.leaves[static_cast<std::size_t>(portal.leafB)].contents);
}

} // namespace

MapAreas buildMapAreas(const BspTree& tree) {
    MapAreas areas;
    const std::int32_t n = static_cast<std::int32_t>(tree.leaves.size());
    areas.leafArea.assign(static_cast<std::size_t>(n), -1);

    std::vector<std::vector<std::int32_t>> links(static_cast<std::size_t>(n));
    for (const BspPortal& portal : tree.portals) {
        if (!portalInGraph(tree, portal) || isDoorLeaf(tree, portal.leafA) ||
            isDoorLeaf(tree, portal.leafB)) {
            continue;
        }
        links[static_cast<std::size_t>(portal.leafA)].push_back(portal.leafB);
        links[static_cast<std::size_t>(portal.leafB)].push_back(portal.leafA);
    }

    std::vector<std::int32_t> stack;
    for (std::int32_t seed = 0; seed < n; ++seed) {
        if (areas.leafArea[static_cast<std::size_t>(seed)] >= 0 ||
            !leafParticipatesInPortalGraph(tree.leaves[static_cast<std::size_t>(seed)].contents)) {
            continue;
        }
        const std::int32_t area = areas.areaCount++;
        areas.leafArea[static_cast<std::size_t>(seed)] = area;
        stack.assign(1, seed);
        while (!stack.empty()) {
            const std::int32_t leaf = stack.back();
            stack.pop_back();
            for (std::int32_t next : links[static_cast<std::size_t>(leaf)]) {
                if (areas.leafArea[static_cast<std::size_t>(next)] < 0) {
                    areas.leafArea[static_cast<std::size_t>(next)] = area;
                    stack.push_back(next);
                }
            }
        }
    }

    std::vector<std::tuple<std::int32_t, std::int32_t, std::string>> seen;
    for (const BspPortal& portal : tree.portals) {
        if (!portalInGraph(tree, portal) ||
            (!isDoorLeaf(tree, portal.leafA) && !isDoorLeaf(tree, portal.leafB))) {
            continue;
        }
        const std::int32_t a = areas.leafArea[static_cast<std::size_t>(portal.leafA)];
        const std::int32_t b = areas.leafArea[static_cast<std::size_t>(portal.leafB)];
        if (a < 0 || b < 0 || a == b) {
            continue;
        }
        auto key = std::make_tuple(std::min(a, b), std::max(a, b), portal.doorBrushId);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
            continue;
        }
        seen.push_back(std::move(key));
        areas.portals.push_back(MapAreaPortal{a, b, portal.doorBrushId});
    }
    return areas;
}

std::vector<std::uint8_t> floodOpenAreas(
    const MapAreas& areas,
    std::int32_t fromArea,
    const std::vector<std::uint8_t>& portalOpen) {
    std::vector<std::uint8_t> open(static_cast<std::size_t>(areas.areaCount), 0);
    if (fromArea < 0 || fromArea >= areas.areaCount) {
        return open;
    }
    open[static_cast<std::size_t>(fromArea)] = 1;
    std::vector<std::int32_t> stack{fromArea};
    while (!stack.empty()) {
        const std::int32_t area = stack.back();
        stack.pop_back();
        for (std::size_t i = 0; i < areas.portals.size(); ++i) {
            if (i < portalOpen.size() && portalOpen[i] == 0) {
                continue;
            }
            const MapAreaPortal& portal = areas.portals[i];
            std::int32_t other = -1;
            if (portal.areaA == area) {
                other = portal.areaB;
            } else if (portal.areaB == area) {
                other = portal.areaA;
            }
            if (other >= 0 && open[static_cast<std::size_t>(other)] == 0) {
                open[static_cast<std::size_t>(other)] = 1;
                stack.push_back(other);
            }
        }
    }
    return open;
}

bool areaGateAllows(const AreaGate& gate, const BspTree& tree, std::int32_t toLeaf) {
    if (gate.areas == nullptr || gate.open == nullptr) {
        return true;
    }
    if (toLeaf < 0 || toLeaf >= static_cast<std::int32_t>(gate.areas->leafArea.size())) {
        return true;
    }
    if (isDoorLeaf(tree, toLeaf)) {
        return true;
    }
    const std::int32_t area = gate.areas->leafArea[static_cast<std::size_t>(toLeaf)];
    if (area < 0 || area >= static_cast<std::int32_t>(gate.open->size())) {
        return true;
    }
    return (*gate.open)[static_cast<std::size_t>(area)] != 0;
}

}
