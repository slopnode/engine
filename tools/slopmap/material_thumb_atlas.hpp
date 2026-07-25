#pragma once

#include "assets/asset_store.hpp"

#include <raylib.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace slopmap {

struct MaterialThumbLookup {
    const Texture2D* texture = nullptr;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    bool valid() const { return texture != nullptr && texture->id != 0; }
};

struct MaterialThumbAtlas {
    static constexpr int kThumbSize = 64;
    static constexpr int kMaxPageSize = 4096;

    void clear();
    bool ensure(
        slopengine::AssetStore& assets,
        const std::vector<std::string>& materials,
        const std::filesystem::path& cacheRoot);
    MaterialThumbLookup lookup(const std::string& materialPath) const;

private:
    struct Entry {
        int page = 0;
        int x = 0;
        int y = 0;
        int w = kThumbSize;
        int h = kThumbSize;
    };

    std::vector<Texture2D> pages_;
    std::unordered_map<std::string, Entry> entries_;
    std::string loadedFingerprint_;
    std::filesystem::path loadedDir_;

    std::string buildFingerprint(
        slopengine::AssetStore& assets,
        const std::vector<std::string>& materials) const;
    std::filesystem::path mountCacheDir(
        const slopengine::AssetStore& assets,
        const std::filesystem::path& cacheRoot) const;
    bool loadFromDisk(const std::filesystem::path& dir, const std::string& fingerprint);
    bool buildAndSave(
        slopengine::AssetStore& assets,
        const std::vector<std::string>& materials,
        const std::filesystem::path& dir,
        const std::string& fingerprint);
};

}
