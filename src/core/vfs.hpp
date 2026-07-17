#pragma once

#include "core/package.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

/** Asset categories mapped to package subdirectories. */
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
    MapCsg,
    MapMeta,
    MapBsp,
    MapRad,
    MapLightmap,
};

class AssetStore;

/** Layered virtual filesystem backed by mounted packages. */
class VirtualFileSystem {
    friend class AssetStore;

public:
    /** Replaces all mounted packages with a single base package. */
    void setBasePackage(Package package);

    /** Appends a package; later packages override earlier ones for asset lookup. */
    void addPackage(Package package);

    const std::vector<Package>& packages() const { return packages_; }

    bool exists(AssetKind kind, std::string_view virtualPath) const;
    std::optional<std::filesystem::path> resolve(AssetKind kind, std::string_view virtualPath) const;
    std::string readText(AssetKind kind, std::string_view virtualPath) const;
    std::vector<std::byte> readBinary(AssetKind kind, std::string_view virtualPath) const;

private:

    static const char* kindDirectory(AssetKind kind);
    static const char* implicitExtension(AssetKind kind);
    static std::filesystem::path normalizeVirtualPath(std::string_view virtualPath);
    static std::filesystem::path followSymlinks(const std::filesystem::path& path);

    std::vector<Package> packages_;
};

}
