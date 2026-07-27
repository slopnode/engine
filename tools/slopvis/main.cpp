#include "assets/asset_store.hpp"
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

    SetTraceLogLevel(LOG_INFO);
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

    auto facPath = assets.resolvePath(AssetKind::MapFac, virtualPath);
    if (!facPath) {
        std::cerr << "slopvis: missing maps/" << virtualPath << ".fac (run slopfac first)\n";
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

    const PvsFile pvs = buildPvs(*tree, &analysis.exteriorEmpty);
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
