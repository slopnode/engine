#include "material_thumb_atlas.hpp"

#include "core/user_paths.hpp"
#include "core/vfs.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace slopmap {

namespace {

std::uint64_t fileMtimeNs(const std::filesystem::path& path) {
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return 0;
    }
    return static_cast<std::uint64_t>(stamp.time_since_epoch().count());
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

bool writeTextFile(const std::filesystem::path& path, const std::string& body) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << body;
    return static_cast<bool>(out);
}

Image makeThumbImage(slopengine::AssetStore& assets, const std::string& materialPath) {
    const slopengine::MaterialAsset* asset = assets.getMaterialAsset(materialPath);
    Color fill = WHITE;
    if (asset != nullptr) {
        fill = asset->baseColor;
        if (!asset->albedoTexture.empty()) {
            const auto disk = assets.resolvePath(slopengine::AssetKind::Texture, asset->albedoTexture);
            if (disk) {
                Image image = LoadImage(disk->string().c_str());
                if (image.data != nullptr) {
                    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
                    ImageResize(&image, MaterialThumbAtlas::kThumbSize, MaterialThumbAtlas::kThumbSize);
                    return image;
                }
            }
        }
    }
    Image swatch = GenImageColor(MaterialThumbAtlas::kThumbSize, MaterialThumbAtlas::kThumbSize, fill);
    ImageFormat(&swatch, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    return swatch;
}

} // namespace

void MaterialThumbAtlas::clear() {
    for (Texture2D& page : pages_) {
        if (page.id != 0) {
            UnloadTexture(page);
        }
    }
    pages_.clear();
    entries_.clear();
    loadedFingerprint_.clear();
    loadedDir_.clear();
}

std::filesystem::path MaterialThumbAtlas::mountCacheDir(
    const slopengine::AssetStore& assets,
    const std::filesystem::path& cacheRoot) const {
    std::string key;
    for (const slopengine::Package& package : assets.packages()) {
        if (!key.empty()) {
            key.push_back('+');
        }
        key += slopengine::sanitizeSaveSegment(package.meta().id);
        key.push_back('_');
        key += slopengine::sanitizeSaveSegment(package.meta().version);
    }
    if (key.empty()) {
        key = "unknown";
    }
    return cacheRoot / key;
}

std::string MaterialThumbAtlas::buildFingerprint(
    slopengine::AssetStore& assets,
    const std::vector<std::string>& materials) const {
    std::ostringstream out;
    out << "v1\n";
    out << "thumb=" << kThumbSize << '\n';
    for (const std::string& path : materials) {
        out << path;
        const auto matDisk = assets.resolvePath(slopengine::AssetKind::Material, path);
        out << '\t' << (matDisk ? fileMtimeNs(*matDisk) : 0ULL);
        const slopengine::MaterialAsset* asset = assets.getMaterialAsset(path);
        if (asset != nullptr && !asset->albedoTexture.empty()) {
            const auto texDisk =
                assets.resolvePath(slopengine::AssetKind::Texture, asset->albedoTexture);
            out << '\t' << asset->albedoTexture;
            out << '\t' << (texDisk ? fileMtimeNs(*texDisk) : 0ULL);
        } else if (asset != nullptr) {
            out << "\t#color\t" << static_cast<int>(asset->baseColor.r) << ','
                << static_cast<int>(asset->baseColor.g) << ','
                << static_cast<int>(asset->baseColor.b) << ','
                << static_cast<int>(asset->baseColor.a);
        } else {
            out << "\t#missing";
        }
        out << '\n';
    }
    return out.str();
}

bool MaterialThumbAtlas::loadFromDisk(
    const std::filesystem::path& dir,
    const std::string& fingerprint) {
    clear();
    const std::string onDisk = readTextFile(dir / "fingerprint");
    if (onDisk != fingerprint) {
        return false;
    }

    const std::string mapBody = readTextFile(dir / "thumbs.map");
    if (mapBody.empty()) {
        return false;
    }

    int pageCount = 0;
    std::istringstream in(mapBody);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("pages ", 0) == 0) {
            pageCount = std::atoi(line.c_str() + 6);
            continue;
        }
        if (line.rfind("thumb ", 0) != 0) {
            continue;
        }
        // thumb "path" page x y w h
        const std::size_t q0 = line.find('"');
        const std::size_t q1 = line.find('"', q0 == std::string::npos ? 0 : q0 + 1);
        if (q0 == std::string::npos || q1 == std::string::npos) {
            continue;
        }
        Entry entry{};
        const std::string path = line.substr(q0 + 1, q1 - q0 - 1);
        if (std::sscanf(
                line.c_str() + static_cast<std::ptrdiff_t>(q1 + 1),
                " %d %d %d %d %d",
                &entry.page,
                &entry.x,
                &entry.y,
                &entry.w,
                &entry.h) != 5) {
            continue;
        }
        entries_.emplace(path, entry);
    }

    if (pageCount < 0) {
        clear();
        return false;
    }

    pages_.resize(static_cast<std::size_t>(pageCount));
    for (int i = 0; i < pageCount; ++i) {
        const std::filesystem::path png = dir / ("atlas_" + std::to_string(i) + ".png");
        pages_[static_cast<std::size_t>(i)] = LoadTexture(png.string().c_str());
        if (pages_[static_cast<std::size_t>(i)].id == 0) {
            clear();
            return false;
        }
    }

    loadedFingerprint_ = fingerprint;
    loadedDir_ = dir;
    return true;
}

bool MaterialThumbAtlas::buildAndSave(
    slopengine::AssetStore& assets,
    const std::vector<std::string>& materials,
    const std::filesystem::path& dir,
    const std::string& fingerprint) {
    clear();

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return false;
    }

    const int cell = kThumbSize;
    const int perRow = kMaxPageSize / cell;
    const int perPage = perRow * perRow;

    struct Packed {
        std::string path;
        int page = 0;
        int x = 0;
        int y = 0;
    };
    std::vector<Packed> packed;
    packed.reserve(materials.size());

    const int pageCount =
        materials.empty() ? 0 : static_cast<int>((materials.size() + perPage - 1) / perPage);
    std::vector<Image> pageImages(static_cast<std::size_t>(std::max(pageCount, 0)));
    for (int p = 0; p < pageCount; ++p) {
        pageImages[static_cast<std::size_t>(p)] = GenImageColor(kMaxPageSize, kMaxPageSize, BLANK);
        ImageFormat(&pageImages[static_cast<std::size_t>(p)], PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    }

    for (std::size_t i = 0; i < materials.size(); ++i) {
        const int page = static_cast<int>(i / static_cast<std::size_t>(perPage));
        const int indexInPage = static_cast<int>(i % static_cast<std::size_t>(perPage));
        const int col = indexInPage % perRow;
        const int row = indexInPage / perRow;
        const int x = col * cell;
        const int y = row * cell;

        Image thumb = makeThumbImage(assets, materials[i]);
        ImageDrawImage(&pageImages[static_cast<std::size_t>(page)], thumb, x, y, WHITE);
        UnloadImage(thumb);

        Packed entry{};
        entry.path = materials[i];
        entry.page = page;
        entry.x = x;
        entry.y = y;
        packed.push_back(std::move(entry));
    }

    for (int p = 0; p < pageCount; ++p) {
        int usedW = 0;
        int usedH = 0;
        for (const Packed& entry : packed) {
            if (entry.page != p) {
                continue;
            }
            usedW = std::max(usedW, entry.x + cell);
            usedH = std::max(usedH, entry.y + cell);
        }
        if (usedW <= 0 || usedH <= 0) {
            usedW = cell;
            usedH = cell;
        }
        Image& image = pageImages[static_cast<std::size_t>(p)];
        if (usedW < image.width || usedH < image.height) {
            ImageCrop(
                &image,
                Rectangle{0.0f, 0.0f, static_cast<float>(usedW), static_cast<float>(usedH)});
        }
        const std::filesystem::path png = dir / ("atlas_" + std::to_string(p) + ".png");
        if (!ExportImage(image, png.string().c_str())) {
            for (Image& img : pageImages) {
                UnloadImage(img);
            }
            return false;
        }
    }

    for (Image& img : pageImages) {
        UnloadImage(img);
    }

    std::ostringstream mapBody;
    mapBody << "pages " << pageCount << '\n';
    for (const Packed& entry : packed) {
        mapBody << "thumb \"" << entry.path << "\" " << entry.page << ' ' << entry.x << ' '
                << entry.y << ' ' << cell << ' ' << cell << '\n';
    }
    if (!writeTextFile(dir / "thumbs.map", mapBody.str())) {
        return false;
    }
    if (!writeTextFile(dir / "fingerprint", fingerprint)) {
        return false;
    }

    return loadFromDisk(dir, fingerprint);
}

bool MaterialThumbAtlas::ensure(
    slopengine::AssetStore& assets,
    const std::vector<std::string>& materials,
    const std::filesystem::path& cacheRoot) {
    const std::filesystem::path dir = mountCacheDir(assets, cacheRoot);
    const std::string fingerprint = buildFingerprint(assets, materials);
    if (dir == loadedDir_ && fingerprint == loadedFingerprint_) {
        return true;
    }
    if (loadFromDisk(dir, fingerprint)) {
        return true;
    }
    return buildAndSave(assets, materials, dir, fingerprint);
}

MaterialThumbLookup MaterialThumbAtlas::lookup(const std::string& materialPath) const {
    MaterialThumbLookup result{};
    const auto it = entries_.find(materialPath);
    if (it == entries_.end()) {
        return result;
    }
    const Entry& entry = it->second;
    if (entry.page < 0 || entry.page >= static_cast<int>(pages_.size())) {
        return result;
    }
    const Texture2D& tex = pages_[static_cast<std::size_t>(entry.page)];
    if (tex.id == 0 || tex.width <= 0 || tex.height <= 0) {
        return result;
    }
    result.texture = &tex;
    result.u0 = static_cast<float>(entry.x) / static_cast<float>(tex.width);
    result.v0 = static_cast<float>(entry.y) / static_cast<float>(tex.height);
    result.u1 = static_cast<float>(entry.x + entry.w) / static_cast<float>(tex.width);
    result.v1 = static_cast<float>(entry.y + entry.h) / static_cast<float>(tex.height);
    return result;
}

}
