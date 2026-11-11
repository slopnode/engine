#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace slopdocs {

struct AssetCount {
    std::string kind;
    std::string folder;
    std::string extension;
    std::size_t count = 0;
};

struct CatalogCount {
    std::string catalog;
    std::string symbol;
    std::string file;
    std::size_t count = 0;
    bool present = false;
};

struct AssetCounts {
    std::vector<AssetCount> assets;
    std::vector<CatalogCount> catalogs;
    std::size_t scriptFiles = 0;
    std::size_t scriptDefinitions = 0;
};

AssetCounts countAssets(const std::filesystem::path& packageRoot, bool verbose);

}
