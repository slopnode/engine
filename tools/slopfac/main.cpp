#include "assets/asset_store.hpp"
#include "game/app_config.hpp"
#include "map/bsp_analyze.hpp"
#include "map/bsp_io.hpp"
#include "map/csg_script.hpp"
#include "map/fac.hpp"
#include "map/fac_io.hpp"
#include "map/mover_brushes.hpp"
#include "map/things_script.hpp"

#include <raylib.h>
#include <s7.h>

#include <iostream>
#include <unordered_set>

int main(int argc, char* argv[]) {
    using namespace slopengine;

    auto config = AppConfig::parse(argc, argv);
    if (!config || !config->map) {
        std::cerr << "Usage: slopfac --base-game <path> [--mod <path>]... --map <name>\n";
        return 1;
    }

    SetTraceLogLevel(LOG_INFO);
    AssetStore assets(*config);
    s7_scheme* scheme = s7_init();
    if (scheme == nullptr) {
        std::cerr << "slopfac: failed to init scheme\n";
        return 1;
    }
    loadPackageMapHandlers(scheme, assets);

    const std::string virtualPath = *config->map + "/static";
    auto bspPath = assets.resolvePath(AssetKind::MapBsp, virtualPath);
    if (!bspPath) {
        std::cerr << "slopfac: missing maps/" << virtualPath << ".bsp (run slopbsp first)\n";
        s7_quit(scheme);
        return 1;
    }

    auto tree = readBspFile(*bspPath);
    if (!tree) {
        std::cerr << "slopfac: failed to read " << *bspPath << "\n";
        s7_quit(scheme);
        return 1;
    }

    auto brushes = loadMapBrushes(scheme, assets, *config->map);
    if (!brushes) {
        std::cerr << "slopfac: failed to load map brushes for '" << *config->map << "'\n";
        s7_quit(scheme);
        return 1;
    }

    std::unordered_set<std::string> moverBrushIds;
    if (auto things = loadMapThings(scheme, assets, *config->map)) {
        moverBrushIds = collectMoverBrushIds(*things);
        if (!moverBrushIds.empty()) {
            TraceLog(
                LOG_INFO,
                "slopfac: omitting %d mover brush(es) from FAC",
                static_cast<int>(moverBrushIds.size()));
        }
    } else {
        TraceLog(LOG_WARNING, "slopfac: failed to load things.s7; mover brushes not omitted");
    }

    const MapHullAnalysis analysis = analyzeMapHull(*tree, *brushes);
    if (!analysis.sealed) {
        TraceLog(LOG_ERROR, "slopfac: LEAK — refuse to build faces for an unsealed hull");
        for (const std::string& step : analysis.leakPathFaceIds) {
            TraceLog(LOG_ERROR, "slopfac: leak path %s", step.c_str());
        }
        s7_quit(scheme);
        return 1;
    }

    const FacBuildResult built = buildVisibleFaces(
        *tree,
        analysis,
        *brushes,
        moverBrushIds.empty() ? nullptr : &moverBrushIds);
    const auto facPath = bspPath->parent_path() / "static.fac";
    if (!writeFacFile(facPath, built.fac)) {
        std::cerr << "slopfac: failed to write " << facPath << "\n";
        s7_quit(scheme);
        return 1;
    }

    TraceLog(
        LOG_INFO,
        "slopfac: wrote %s (faces=%d inferredNodraw=%d)",
        facPath.string().c_str(),
        static_cast<int>(built.fac.faces.size()),
        static_cast<int>(built.inferredNodrawFaceIds.size()));
    for (const std::string& warning : analysis.detailOutsideWarnings) {
        TraceLog(LOG_WARNING, "slopfac: %s", warning.c_str());
    }

    s7_quit(scheme);
    return 0;
}
