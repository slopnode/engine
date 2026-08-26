#include "assets/asset_store.hpp"
#include "core/log.hpp"
#include "game/app_config.hpp"
#include "map/bsp_analyze.hpp"
#include "map/bsp_io.hpp"
#include "map/csg_script.hpp"
#include "map/pvs.hpp"
#include "map/pvs_io.hpp"

#include <raylib.h>
#include <s7.h>

#include <iostream>

int main(int argc, char* argv[]) {
    using namespace slopengine;

    auto config = AppConfig::parse(argc, argv);
    if (!config || !config->map) {
        std::cerr << "Usage: slopvis --base-game <path> [--mod <path>]... --map <name>\n";
        return 1;
    }

    Log::init(config->verbose ? LogLevel::Debug : LogLevel::Info);
    Log::addDefaultConsoleSink();
    AssetStore assets(*config);
    s7_scheme* scheme = s7_init();
    if (scheme == nullptr) {
        std::cerr << "slopvis: failed to init scheme\n";
        return 1;
    }
    loadPackageMapHandlers(scheme, assets);

    const std::string virtualPath = *config->map + "/static";
    auto bspPath = assets.resolvePath(AssetKind::MapBsp, virtualPath);
    if (!bspPath) {
        std::cerr << "slopvis: missing maps/" << virtualPath << ".bsp (run slopbsp first)\n";
        s7_quit(scheme);
        return 1;
    }

    auto tree = readBspFile(*bspPath);
    if (!tree) {
        std::cerr << "slopvis: failed to read " << *bspPath << "\n";
        s7_quit(scheme);
        return 1;
    }

    auto brushes = loadMapBrushes(scheme, assets, *config->map);
    if (!brushes) {
        std::cerr << "slopvis: failed to load map brushes for '" << *config->map << "'\n";
        s7_quit(scheme);
        return 1;
    }

    const MapHullAnalysis analysis = analyzeMapHull(*tree, *brushes);
    if (!analysis.sealed) {
        TraceLog(LOG_ERROR, "slopvis: LEAK — refuse to build PVS for an unsealed hull");
        for (const std::string& step : analysis.leakPathFaceIds) {
            TraceLog(LOG_ERROR, "slopvis: leak path %s", step.c_str());
        }
        s7_quit(scheme);
        return 1;
    }

    // analysis.sealed only catches a TOTAL leak (every open leaf flooding
    // exterior). A partial leak — real interior content reachable from the
    // "exterior" flood through an unsealed opening — leaves analysis.sealed
    // true while analysis.exteriorEmpty still wrongly excludes every leaf on
    // the leaked side from ever being a PVS source (isPvsSourceLeaf in
    // pvs_build.cpp), which is exactly what breaks sprite/actor visibility
    // there. detailOutsideWarnings is already the leak signal for that case;
    // if it's non-empty, the exterior classification isn't trustworthy
    // enough to use for exclusion, so fall back to treating every open leaf
    // as a valid source rather than silently blinding a chunk of the map.
    const std::vector<std::uint8_t>* exteriorForPvs = &analysis.exteriorEmpty;
    if (!analysis.detailOutsideWarnings.empty()) {
        TraceLog(
            LOG_ERROR,
            "slopvis: %d brush(es) needing interior placement resolve into leaves "
            "flagged exterior void — this map has an unsealed opening leaking real "
            "interior space to the outside. Building PVS without exterior exclusion "
            "(no culling optimization) so visibility stays correct; seal the opening "
            "and re-run slopvis to restore normal culling.",
            static_cast<int>(analysis.detailOutsideWarnings.size()));
        for (const std::string& warning : analysis.detailOutsideWarnings) {
            TraceLog(LOG_ERROR, "slopvis:   %s", warning.c_str());
        }
        exteriorForPvs = nullptr;
    }

    const PvsFile pvs = buildPvs(*tree, exteriorForPvs);
    const auto visPath = bspPath->parent_path() / "static.vis";
    if (!writePvsFile(visPath, pvs)) {
        std::cerr << "slopvis: failed to write " << visPath << "\n";
        s7_quit(scheme);
        return 1;
    }

    int openLeaves = 0;
    int visiblePairs = 0;
    for (int a = 0; a < pvs.leafCount; ++a) {
        if (!leafIsOpen(tree->leaves[static_cast<std::size_t>(a)].contents)) {
            continue;
        }
        ++openLeaves;
        for (int b = 0; b < pvs.leafCount; ++b) {
            if (pvsCanSee(pvs, a, b)) {
                ++visiblePairs;
            }
        }
    }

    TraceLog(
        LOG_INFO,
        "slopvis: wrote %s (leaves=%d openLeaves=%d visiblePairs=%d)",
        visPath.string().c_str(),
        pvs.leafCount,
        openLeaves,
        visiblePairs);

    s7_quit(scheme);
    return 0;
}
