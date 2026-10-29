#pragma once

#include "editor.hpp"

#include "assets/asset_store.hpp"

#include <raylib.h>

#include <string>
#include <unordered_map>

namespace slopmat {

/** Small lazily built thumbnails for browser rows; a bounded number are built per frame. */
struct ThumbCache {
    static constexpr int kSize = 40;
    int buildsPerFrame = 3;

    void beginFrame();
    Texture2D get(slopengine::AssetStore& assets, DocKind kind, const std::string& path);
    void invalidate(const std::string& path);
    void clear();

private:
    struct Entry {
        Texture2D texture{};
        bool attempted = false;
    };
    std::unordered_map<std::string, Entry> entries_;
    int budget_ = 0;

    static std::string key(DocKind kind, const std::string& path);
    static bool buildImage(slopengine::AssetStore& assets, DocKind kind, const std::string& path, Image& out);
};

}
