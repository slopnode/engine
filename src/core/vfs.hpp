#pragma once

#include "core/package.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace daggerlike {

enum class AssetKind {
    Texture,
    Material,
    Mesh,
    Shader,
    Script,
    Skeleton,
    SkeletonBind,
    Geo,
    GeoVert,
    GeoWeights,
    Anim,
    AnimTracks,
};

class AssetStore;

class VirtualFileSystem {
    friend class AssetStore;

public:
    void setBasePackage(Package package);
    void addPackage(Package package);

private:
    bool exists(AssetKind kind, std::string_view virtualPath) const;
    std::optional<std::filesystem::path> resolve(AssetKind kind, std::string_view virtualPath) const;
    std::string readText(AssetKind kind, std::string_view virtualPath) const;
    std::vector<std::byte> readBinary(AssetKind kind, std::string_view virtualPath) const;

    static const char* kindDirectory(AssetKind kind);
    static const char* implicitExtension(AssetKind kind);
    static std::filesystem::path normalizeVirtualPath(std::string_view virtualPath);
    static std::filesystem::path followSymlinks(const std::filesystem::path& path);

    std::vector<Package> packages_;
};

}
