#include "assets/asset_store.hpp"
#include "core/log.hpp"
#include "game/app_config.hpp"
#include "map/bsp.hpp"
#include "map/bsp_analyze.hpp"
#include "map/bsp_io.hpp"
#include "map/map_script.hpp"

#include <raylib.h>
#include <s7.h>

#include <iostream>

int main(int argc, char* argv[]) {
    using namespace slopengine;

    auto config = AppConfig::parse(argc, argv);
    if (!config || !config->map) {
        std::cerr << "Usage: slopbsp --base-game <path> [--mod <path>]... --map <name>\n";
        return 1;
    }

    Log::init(config->verbose ? LogLevel::Debug : LogLevel::Info);
    Log::addDefaultConsoleSink();
    AssetStore assets(*config);
    s7_scheme* scheme = s7_init();
    if (scheme == nullptr) {
        std::cerr << "slopbsp: failed to init scheme\n";
        return 1;
    }
    loadPackageMapHandlers(scheme, assets);

    // Tree structure and solidity classification need the brushes' full, uncarved
    // convex shapes -- pointInsideBrush-style containment tests require every one
    // of a brush's faces to still bound it, and a carved (decapitated) brush's
    // remaining faces describe an unbounded region instead, corrupting leaf
    // classification map-wide. Only the emitted surface geometry wants carving.
    auto rawBrushes = loadMapBrushes(scheme, assets, *config->map);
    if (!rawBrushes) {
        std::cerr << "slopbsp: failed to load map brushes for '" << *config->map << "'\n";
        s7_quit(scheme);
        return 1;
    }

    auto brushes = loadCarvedMapBrushes(scheme, assets, *config->map);
    if (!brushes) {
        std::cerr << "slopbsp: failed to load carved map brushes for '" << *config->map
                   << "' (run slopcsg first)\n";
        s7_quit(scheme);
        return 1;
    }

    BspTree tree = buildBspFromHullBrushes(*rawBrushes, &*brushes);
    const std::string virtualPath = *config->map + "/compiled/csg";
    auto csgPath = assets.resolvePath(AssetKind::MapCarved, virtualPath);
    if (!csgPath) {
        std::cerr << "slopbsp: failed to resolve map path\n";
        s7_quit(scheme);
        return 1;
    }

    const auto bspPath = csgPath->parent_path() / "bsp";
    if (!writeBspFile(bspPath, tree)) {
        std::cerr << "slopbsp: failed to write " << bspPath << "\n";
        s7_quit(scheme);
        return 1;
    }

    TraceLog(
        LOG_INFO,
        "slopbsp: wrote %s (nodes=%d leaves=%d surfaces=%d)",
        bspPath.string().c_str(),
        static_cast<int>(tree.nodes.size()),
        static_cast<int>(tree.leaves.size()),
        static_cast<int>(tree.surfaceFaces.size()));

    const MapHullAnalysis analysis = analyzeMapHull(tree, *brushes);
    int exteriorEmpty = 0;
    int interiorEmpty = 0;
    for (std::size_t i = 0; i < tree.leaves.size(); ++i) {
        if (leafBlocksFlood(tree.leaves[i].contents)) {
            continue;
        }
        if (analysis.exteriorEmpty[i] != 0) {
            ++exteriorEmpty;
        } else {
            ++interiorEmpty;
        }
    }

    if (!analysis.sealed) {
        TraceLog(LOG_ERROR, "slopbsp: LEAK — exterior empty reaches the playable volume");
        TraceLog(
            LOG_ERROR,
            "slopbsp: exteriorEmpty=%d interiorEmpty=%d",
            exteriorEmpty,
            interiorEmpty);
        for (const std::string& step : analysis.leakPathFaceIds) {
            TraceLog(LOG_ERROR, "slopbsp: leak path %s", step.c_str());
        }
        s7_quit(scheme);
        return 1;
    }

    TraceLog(
        LOG_INFO,
        "slopbsp: sealed exteriorEmpty=%d interiorEmpty=%d",
        exteriorEmpty,
        interiorEmpty);
    for (const std::string& warning : analysis.detailOutsideWarnings) {
        TraceLog(LOG_WARNING, "slopbsp: %s", warning.c_str());
    }

    s7_quit(scheme);
    return 0;
}
