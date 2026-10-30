#pragma once

#include "assets/asset_store.hpp"
#include "editor.hpp"

#include <string>
#include <vector>

namespace slopmodel {

struct GeoEntry {
    std::string virtualPath;
    std::string packageId;
};

/** Lists every .geo under the mounted packages' geometry/ folders. */
std::vector<GeoEntry> scanGeos(const slopengine::AssetStore& assets);

struct ModelBrowser {
    std::vector<GeoEntry> entries;
    std::string filter;

    void rescan(const slopengine::AssetStore& assets);
    void draw(Editor& editor, slopengine::AssetStore& assets);
};

/** Modal picker for the geo shown on an attachment. */
struct GeoPicker {
    std::vector<GeoEntry> entries;
    std::string filter;
    bool open = false;

    void rescan(const slopengine::AssetStore& assets);
    bool drawModal(slopengine::AssetStore& assets, std::string& outPath);
};

}
