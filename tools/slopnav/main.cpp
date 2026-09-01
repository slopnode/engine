#include "assets/asset_store.hpp"
#include "core/log.hpp"
#include "game/app_config.hpp"
#include "map/bsp_analyze.hpp"
#include "map/bsp_io.hpp"
#include "map/map_script.hpp"
#include "map/nav_bake.hpp"
#include "map/nav_graph.hpp"
#include "map/nav_io.hpp"
#include "map/nav_navmesh_build.hpp"

#include <raylib.h>
#include <raymath.h>
#include <s7.h>

#include <algorithm>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

using namespace slopengine;

// Nearest-centroid lookup for the diagnostic comparison only -- Stage 6's real runtime
// point-sampling (navSamplePoly) does proper XZ containment; this just needs two
// plausible, far-apart walkable points to eyeball route cost/hop-count differences.
int nearestWalkableLeafByHorizontalDist(const MapNavigation& nav, Vector3 point) {
    int best = -1;
    float bestDist = std::numeric_limits<float>::infinity();
    for (int i = 0; i < nav.leafCount; ++i) {
        if (!nav.walkable[static_cast<std::size_t>(i)]) {
            continue;
        }
        const float d = navHorizontalDistSq(nav.leafCentroids[static_cast<std::size_t>(i)], point);
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

void printRouteStats(const char* label, const MapNavigation& nav, Vector3 fromPoint, Vector3 toPoint) {
    const int fromLeaf = nearestWalkableLeafByHorizontalDist(nav, fromPoint);
    const int toLeaf = nearestWalkableLeafByHorizontalDist(nav, toPoint);
    if (fromLeaf < 0 || toLeaf < 0) {
        std::cout << "  " << label << ": no walkable leaves found\n";
        return;
    }
    const std::vector<int> path = findLeafPath(nav, fromLeaf, toLeaf);
    if (path.empty()) {
        std::cout << "  " << label << ": leafCount=" << nav.leafCount << " UNREACHABLE ("
                   << fromLeaf << " -> " << toLeaf << ")\n";
        return;
    }
    float cost = 0.0f;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        for (const NavPortalLink& link : nav.adjacency[static_cast<std::size_t>(path[i])]) {
            if (link.neighborLeaf == path[i + 1]) {
                cost += link.cost;
                break;
            }
        }
    }
    std::cout << "  " << label << ": leafCount=" << nav.leafCount << " hops=" << path.size()
               << " cost=" << cost << "\n";
}

}

int main(int argc, char* argv[]) {
    using namespace slopengine;

    // AppConfig::parse rejects any flag it doesn't recognize, so strip --compare
    // (a slopnav-only diagnostic flag) before handing argv to it.
    std::vector<char*> filteredArgv;
    bool compare = false;
    for (int i = 0; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--compare") {
            compare = true;
            continue;
        }
        filteredArgv.push_back(argv[i]);
    }

    auto config = AppConfig::parse(static_cast<int>(filteredArgv.size()), filteredArgv.data());
    if (!config || !config->map) {
        std::cerr << "Usage: slopnav --base-game <path> [--mod <path>]... --map <name> [--compare]\n";
        return 1;
    }

    Log::init(config->verbose ? LogLevel::Debug : LogLevel::Info);
    Log::addDefaultConsoleSink();
    AssetStore assets(*config);
    s7_scheme* scheme = s7_init();
    if (scheme == nullptr) {
        std::cerr << "slopnav: failed to init scheme\n";
        return 1;
    }
    loadPackageMapHandlers(scheme, assets);

    const std::string virtualPath = *config->map + "/static";
    auto bspPath = assets.resolvePath(AssetKind::MapBsp, virtualPath);
    if (!bspPath) {
        std::cerr << "slopnav: missing maps/" << virtualPath << ".bsp (run slopbsp first)\n";
        s7_quit(scheme);
        return 1;
    }

    auto tree = readBspFile(*bspPath);
    if (!tree) {
        std::cerr << "slopnav: failed to read " << *bspPath << "\n";
        s7_quit(scheme);
        return 1;
    }

    auto brushes = loadMapBrushes(scheme, assets, *config->map);
    if (!brushes) {
        std::cerr << "slopnav: failed to load map brushes for '" << *config->map << "'\n";
        s7_quit(scheme);
        return 1;
    }

    const MapHullAnalysis analysis = analyzeMapHull(*tree, *brushes);
    if (!analysis.sealed) {
        TraceLog(LOG_ERROR, "slopnav: LEAK — refuse to bake a navmesh for an unsealed hull");
        for (const std::string& step : analysis.leakPathFaceIds) {
            TraceLog(LOG_ERROR, "slopnav: leak path %s", step.c_str());
        }
        s7_quit(scheme);
        return 1;
    }

    const std::optional<NavPolyMesh> polyMesh = buildNavPolyMesh(*tree, *brushes, &analysis.exteriorEmpty);
    if (!polyMesh) {
        TraceLog(LOG_ERROR, "slopnav: bake produced no walkable area for '%s'", config->map->c_str());
        s7_quit(scheme);
        return 1;
    }

    const MapNavigation nav = buildMapNavigationFromPolyMesh(*polyMesh, *brushes);
    const auto navPath = bspPath->parent_path() / "static.nav";
    if (!writeNavFile(navPath, nav)) {
        std::cerr << "slopnav: failed to write " << navPath << "\n";
        s7_quit(scheme);
        return 1;
    }

    int linkCount = 0;
    for (const auto& links : nav.adjacency) {
        linkCount += static_cast<int>(links.size());
    }

    if (compare) {
        const MapNavigation legacyNav = buildMapNavigation(*tree, &analysis.exteriorEmpty);

        Vector3 minCorner{std::numeric_limits<float>::infinity(), 0, std::numeric_limits<float>::infinity()};
        Vector3 maxCorner{-std::numeric_limits<float>::infinity(), 0, -std::numeric_limits<float>::infinity()};
        for (int i = 0; i < nav.leafCount; ++i) {
            if (!nav.walkable[static_cast<std::size_t>(i)]) {
                continue;
            }
            const Vector3& c = nav.leafCentroids[static_cast<std::size_t>(i)];
            minCorner.x = std::min(minCorner.x, c.x);
            minCorner.z = std::min(minCorner.z, c.z);
            maxCorner.x = std::max(maxCorner.x, c.x);
            maxCorner.z = std::max(maxCorner.z, c.z);
        }

        std::cout << "slopnav --compare: '" << *config->map << "'\n";
        std::cout << "  BSP leaf/portal graph:  leaves=" << legacyNav.leafCount << "\n";
        std::cout << "  Navmesh polygon graph:  polys=" << nav.leafCount << "\n";
        printRouteStats("bsp   ", legacyNav, minCorner, maxCorner);
        printRouteStats("navmesh", nav, minCorner, maxCorner);
    }

    TraceLog(
        LOG_INFO,
        "slopnav: wrote %s (polys=%d links=%d)",
        navPath.string().c_str(),
        nav.leafCount,
        linkCount);

    s7_quit(scheme);
    return 0;
}
