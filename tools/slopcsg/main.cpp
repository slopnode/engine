#include "assets/asset_store.hpp"
#include "core/log.hpp"
#include "game/app_config.hpp"
#include "map/csg_write.hpp"
#include "map/map_script.hpp"

#include <raylib.h>
#include <s7.h>

#include <iostream>

int main(int argc, char* argv[]) {
    using namespace slopengine;

    auto config = AppConfig::parse(argc, argv);
    if (!config || !config->map) {
        std::cerr << "Usage: slopcsg --base-game <path> [--mod <path>]... --map <name>\n";
        return 1;
    }

    Log::init(config->verbose ? LogLevel::Debug : LogLevel::Info);
    Log::addDefaultConsoleSink();
    AssetStore assets(*config);
    s7_scheme* scheme = s7_init();
    if (scheme == nullptr) {
        std::cerr << "slopcsg: failed to init scheme\n";
        return 1;
    }
    loadPackageMapHandlers(scheme, assets);

    // No carving logic yet -- this is a pass-through scaffold proving the pipeline plumbing.
    // See docs/csg-bsp-vis-rad.md and the "slopcsg" plan for the real brush-vs-brush clipping
    // this stage will eventually do.
    auto brushes = loadMapBrushes(scheme, assets, *config->map);
    if (!brushes) {
        std::cerr << "slopcsg: failed to load map brushes for '" << *config->map << "'\n";
        s7_quit(scheme);
        return 1;
    }
    const std::size_t brushesIn = brushes->size();

    const std::string virtualPath = *config->map + "/static";
    auto sourcePath = assets.resolvePath(AssetKind::MapSource, virtualPath);
    if (!sourcePath) {
        std::cerr << "slopcsg: failed to resolve map path\n";
        s7_quit(scheme);
        return 1;
    }

    const auto csgPath = sourcePath->parent_path() / "static.csg";
    if (!writeMapBrushes(csgPath, *brushes)) {
        std::cerr << "slopcsg: failed to write " << csgPath << "\n";
        s7_quit(scheme);
        return 1;
    }

    TraceLog(
        LOG_INFO,
        "slopcsg: wrote %s (brushes in=%d out=%d)",
        csgPath.string().c_str(),
        static_cast<int>(brushesIn),
        static_cast<int>(brushes->size()));

    s7_quit(scheme);
    return 0;
}
