#include "assets/asset_store.hpp"
#include "game/app_config.hpp"
#include "map/bsp_analyze.hpp"
#include "map/bsp_io.hpp"
#include "map/csg_script.hpp"
#include "map/vis.hpp"
#include "map/vis_io.hpp"

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
        TraceLog(LOG_ERROR, "slopvis: LEAK — refuse to build visible faces for an unsealed hull");
        for (const std::string& step : analysis.leakPathFaceIds) {
            TraceLog(LOG_ERROR, "slopvis: leak path %s", step.c_str());
        }
        s7_quit(scheme);
        return 1;
    }

    const VisBuildResult built = buildVisibleFaces(*tree, analysis, *brushes);
    const auto visPath = bspPath->parent_path() / "static.vis";
    if (!writeVisFile(visPath, built.vis)) {
        std::cerr << "slopvis: failed to write " << visPath << "\n";
        s7_quit(scheme);
        return 1;
    }

    TraceLog(
        LOG_INFO,
        "slopvis: wrote %s (faces=%d inferredNodraw=%d)",
        visPath.string().c_str(),
        static_cast<int>(built.vis.faces.size()),
        static_cast<int>(built.inferredNodrawFaceIds.size()));
    for (const std::string& warning : analysis.detailOutsideWarnings) {
        TraceLog(LOG_WARNING, "slopvis: %s", warning.c_str());
    }

    s7_quit(scheme);
    return 0;
}
