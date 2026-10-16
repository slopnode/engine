#include "thumb_cache.hpp"

#include <algorithm>

namespace slopmat {

std::string ThumbCache::key(DocKind kind, const std::string& path) {
    return std::to_string(static_cast<int>(kind)) + "|" + path;
}

void ThumbCache::beginFrame() {
    budget_ = buildsPerFrame;
}

void ThumbCache::clear() {
    for (auto& [_, entry] : entries_) {
        if (entry.texture.id != 0) {
            UnloadTexture(entry.texture);
        }
    }
    entries_.clear();
}

void ThumbCache::invalidate(const std::string& path) {
    const std::string suffix = "|" + path;
    for (auto it = entries_.begin(); it != entries_.end();) {
        const bool matches = it->first.size() >= suffix.size() &&
            it->first.compare(it->first.size() - suffix.size(), suffix.size(), suffix) == 0;
        if (matches) {
            if (it->second.texture.id != 0) {
                UnloadTexture(it->second.texture);
            }
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

bool ThumbCache::buildImage(slopengine::AssetStore& assets, DocKind kind, const std::string& path, Image& out) {
    auto loadTexture = [&assets](const std::string& texturePath, Image& image) {
        const auto resolved = assets.resolvePath(slopengine::AssetKind::Texture, texturePath);
        if (!resolved) {
            return false;
        }
        image = LoadImage(resolved->string().c_str());
        return image.data != nullptr;
    };
    auto loadAnimFrame = [&](const std::string& animPath, Image& image) {
        const slopengine::TextureAnimBank* bank = assets.getTextureAnimBank(animPath);
        if (bank == nullptr || bank->clips.empty() || bank->clips[0].frames.empty()) {
            return false;
        }
        return loadTexture(bank->clips[0].frames[0].texture, image);
    };
    auto loadGraph = [&](const std::string& graphPath, Image& image) {
        const slopengine::TextureGraph* graph = assets.getTextureGraph(graphPath);
        if (graph == nullptr || graph->outputs.empty()) {
            return false;
        }
        const char* output = graph->findOutput("albedo") != nullptr ? "albedo" : graph->outputs[0].name.c_str();
        std::string error;
        return assets.evaluateTextureGraphImage(graphPath, output, image, error);
    };

    switch (kind) {
    case DocKind::Texture:
        return loadTexture(path, out);
    case DocKind::TextureAnim:
        return loadAnimFrame(path, out);
    case DocKind::TextureGraph:
        return loadGraph(path, out);
    case DocKind::Material: {
        const slopengine::MaterialAsset* asset = assets.getMaterialAsset(path);
        if (asset == nullptr) {
            return false;
        }
        if (!asset->albedoTexture.empty()) {
            return loadTexture(asset->albedoTexture, out);
        }
        if (!asset->textureAnimPath.empty()) {
            return loadAnimFrame(asset->textureAnimPath, out);
        }
        if (!asset->textureGraphPath.empty()) {
            return loadGraph(asset->textureGraphPath, out);
        }
        out = GenImageColor(kSize, kSize, asset->baseColor);
        return out.data != nullptr;
    }
    }
    return false;
}

Texture2D ThumbCache::get(slopengine::AssetStore& assets, DocKind kind, const std::string& path) {
    const std::string k = key(kind, path);
    const auto existing = entries_.find(k);
    if (existing != entries_.end()) {
        return existing->second.texture;
    }
    if (budget_ <= 0) {
        return {};
    }
    --budget_;

    Entry entry{};
    entry.attempted = true;
    Image image{};
    if (buildImage(assets, kind, path, image) && image.data != nullptr) {
        ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        int w = kSize;
        int h = kSize;
        if (image.width >= image.height) {
            h = std::max(1, (kSize * image.height) / std::max(1, image.width));
        } else {
            w = std::max(1, (kSize * image.width) / std::max(1, image.height));
        }
        ImageResizeNN(&image, w, h);
        entry.texture = LoadTextureFromImage(image);
        UnloadImage(image);
    }
    entries_.emplace(k, entry);
    return entry.texture;
}

}
