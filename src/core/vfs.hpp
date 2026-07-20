#pragma once

#include "core/package.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

/** Asset categories mapped to package subdirectories and file extensions. */
enum class AssetKind {
    Texture,       /**< PNG under textures/ */
    Material,      /**< .mat under materials/ */
    Mesh,          /**< .glb under meshes/ */
    Shader,        /**< .glsl under shaders/ */
    Script,        /**< .s7 under scripts/ */
    Skeleton,      /**< .skel under skeletons/ */
    SkeletonBind,  /**< .bind under skeletons/ */
    Geo,           /**< .geo under geometry/ */
    GeoVert,       /**< .vert under geometry/ */
    GeoWeights,    /**< .weights under geometry/ */
    Anim,          /**< .anim under animations/ */
    AnimTracks,    /**< .tracks under animations/ */
    MapCsg,        /**< maps/<name>/static.csg */
    MapMeta,       /**< maps/<name>/map.meta */
    MapBsp,        /**< maps/<name>/static.bsp */
    MapRad,        /**< maps/<name>/rad/static.rad */
    MapLightmap,   /**< maps/<name>/rad/atlas PNGs */
    MapEntities,   /**< maps/<name>/entities.s7 */
    MapGraphs,     /**< maps/<name>/graphs.s7 */
    PrefabCsg,     /**< .csg under prefabs/ */
    PrefabEntities, /**< .s7 sidecar under prefabs/ */
    Sprite,        /**< .spr under sprites/ */
    SpriteAnim,    /**< .spanim under sprites/ */
    Icon,          /**< packed PNG under icons/ */
    IconMap,       /**< .iconmap under icons/ */
    Data,          /**< .s7 under data/ */
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

    /** Returns mounted packages in mount order. */
    const std::vector<Package>& packages() const { return packages_; }

    /** Returns true when @p virtualPath exists for @p kind. */
    bool exists(AssetKind kind, std::string_view virtualPath) const;

    /** Resolves @p virtualPath to a filesystem path when present. */
    std::optional<std::filesystem::path> resolve(AssetKind kind, std::string_view virtualPath) const;

    /** Reads a text asset; empty string if missing. */
    std::string readText(AssetKind kind, std::string_view virtualPath) const;

    /** Reads a binary asset; empty vector if missing. */
    std::vector<std::byte> readBinary(AssetKind kind, std::string_view virtualPath) const;

private:

    static const char* kindDirectory(AssetKind kind);
    static const char* implicitExtension(AssetKind kind);
    static std::filesystem::path normalizeVirtualPath(std::string_view virtualPath);
    static std::filesystem::path followSymlinks(const std::filesystem::path& path);

    std::vector<Package> packages_;
};

}
