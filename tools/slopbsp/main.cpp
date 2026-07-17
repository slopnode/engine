#include "assets/asset_store.hpp"
#include "game/app_config.hpp"
#include "map/bsp.hpp"
#include "map/bsp_io.hpp"
#include "map/csg_script.hpp"

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

    SetTraceLogLevel(LOG_INFO);
    AssetStore assets(*config);
    s7_scheme* scheme = s7_init();
    if (scheme == nullptr) {
        std::cerr << "slopbsp: failed to init scheme\n";
        return 1;
    }

    auto brushes = loadMapBrushes(scheme, assets, *config->map);
    if (!brushes) {
        std::cerr << "slopbsp: failed to load map brushes for '" << *config->map << "'\n";
        s7_quit(scheme);
        return 1;
    }

    BspTree tree = buildBspFromHullBrushes(*brushes);
    const std::string virtualPath = *config->map + "/static";
    auto csgPath = assets.resolvePath(AssetKind::MapCsg, virtualPath);
    if (!csgPath) {
        std::cerr << "slopbsp: failed to resolve map path\n";
        s7_quit(scheme);
        return 1;
    }

    const auto bspPath = csgPath->parent_path() / "static.bsp";
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

    s7_quit(scheme);
    return 0;
}
