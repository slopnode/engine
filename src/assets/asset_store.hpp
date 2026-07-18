#pragma once

#include "assets/anim_loader.hpp"
#include "assets/material_loader.hpp"
#include "assets/skeleton_loader.hpp"
#include "assets/sprite_anim_loader.hpp"
#include "assets/sprite_loader.hpp"
#include "core/vfs.hpp"
#include "game/app_config.hpp"

#include <raylib.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct s7_scheme;

namespace slopengine {

/** Cached asset loader backed by mounted packages. */
class AssetStore {
public:
    /** Mounts packages from @p config and prepares the asset store. */
    explicit AssetStore(const AppConfig& config);

    ~AssetStore();

    AssetStore(const AssetStore&) = delete;
    AssetStore& operator=(const AssetStore&) = delete;

    /** Returns true when a texture exists at @p path. */
    bool hasTexture(std::string_view path) const;

    /** Returns true when a material exists at @p path. */
    bool hasMaterial(std::string_view path) const;

    /** Returns true when a mesh exists at @p path. */
    bool hasMesh(std::string_view path) const;

    /** Returns true when a shader exists at @p path. */
    bool hasShader(std::string_view path) const;

    /** Returns true when a script exists at @p path. */
    bool hasScript(std::string_view path) const;

    /** Returns true when a map CSG script exists at @p path. */
    bool hasMapCsg(std::string_view path) const;

    /** Returns true when a map entities script exists at @p path. */
    bool hasMapEntities(std::string_view path) const;

    /** Returns true when a map meta file exists at @p path. */
    bool hasMapMeta(std::string_view path) const;

    /** Returns true when a compiled map BSP exists at @p path. */
    bool hasMapBsp(std::string_view path) const;

    /** Returns true when a compiled map radiosity file exists at @p path. */
    bool hasMapRad(std::string_view path) const;

    /** Resolves an asset to a filesystem path when present. */
    std::optional<std::filesystem::path> resolvePath(AssetKind kind, std::string_view path) const;

    /** Returns true when a skeleton exists at @p path. */
    bool hasSkeleton(std::string_view path) const;

    /** Returns true when a geometry asset exists at @p path. */
    bool hasGeo(std::string_view path) const;

    /** Returns true when a geometry vertex buffer exists at @p path. */
    bool hasGeoVert(std::string_view path) const;

    /** Returns true when a geometry weights buffer exists at @p path. */
    bool hasGeoWeights(std::string_view path) const;

    /** Returns true when an animation asset exists at @p path. */
    bool hasAnim(std::string_view path) const;

    /** Returns true when animation track data exists at @p path. */
    bool hasAnimTracks(std::string_view path) const;

    /** Returns true when a sprite asset exists at @p path. */
    bool hasSprite(std::string_view path) const;

    /** Returns true when a sprite animation bank exists at @p path. */
    bool hasSpriteAnim(std::string_view path) const;

    /** Returns a cached texture, loading it when needed. */
    Texture2D getTexture(std::string_view path);

    /** Returns a cached mesh model, loading it when needed. */
    Model getModel(std::string_view path);

    /** Returns a cached rigged geometry model, loading it when needed. */
    Model getGeoModel(std::string_view path);

    /** Resolves and returns a raylib material for @p path. */
    Material resolveMaterial(std::string_view path);

    /** Returns the cached parsed material asset for @p path, loading it when needed. */
    const MaterialAsset* getMaterialAsset(std::string_view path);

    /** Returns a cached skeleton asset, loading it when needed. */
    const SkeletonAsset* getSkeletonAsset(std::string_view path);

    /** Returns bind-pose matrices for @p skeletonPath, loading them when needed. */
    const std::vector<Matrix>* getSkeletonBindMatrices(std::string_view skeletonPath);

    /** Returns a cached animation bank, loading it when needed. */
    const AnimBank* getAnimBank(std::string_view path);

    /** Returns a cached sprite asset with runtime atlas, loading when needed. */
    const SpriteAsset* getSpriteAsset(std::string_view path);

    /** Returns the runtime atlas for a previously loaded sprite asset. */
    const SpriteAtlas* getSpriteAtlas(std::string_view path);

    /** Returns a cached sprite animation bank, loading it when needed. */
    const SpriteAnimBank* getSpriteAnimBank(std::string_view path);

    /** Removes a cached texture and unloads its GPU resources. */
    void unloadTexture(std::string_view path);

    /** Returns the text source of the shader at @p path. */
    std::string getShaderSource(std::string_view path);

    /** Returns the text source of the material at @p path. */
    std::string getMaterialSource(std::string_view path);

    /** Returns the text source of the script at @p path. */
    std::string getScriptSource(std::string_view path);

    /** Returns the text source of the map meta at @p path. */
    std::string getMapMetaSource(std::string_view path);

    /** Returns mounted package metadata in mount order. */
    const std::vector<Package>& packages() const;

    /** Returns true when a mounted package with @p packageId exists. */
    bool hasPackageId(std::string_view packageId) const;

    /** Returns the text source of the skeleton at @p path. */
    std::string getSkeletonSource(std::string_view path);

    /** Returns the text source of the geometry asset at @p path. */
    std::string getGeoSource(std::string_view path);

    /** Returns the text source of the animation asset at @p path. */
    std::string getAnimSource(std::string_view path);

    /** Returns the text source of the sprite asset at @p path. */
    std::string getSpriteSource(std::string_view path);

    /** Returns the text source of the sprite animation bank at @p path. */
    std::string getSpriteAnimSource(std::string_view path);

    /** Reads the full binary contents of an asset identified by @p kind. */
    std::vector<std::byte> readBinary(std::string_view path, AssetKind kind);

    /** Reads up to @p out.size() bytes from an asset at @p offset into @p out. */
    bool readBinary(std::string_view path, AssetKind kind, std::span<std::byte> out, std::size_t offset = 0) const;

    /** Reads the vertex buffer associated with the geometry at @p geoPath. */
    std::vector<std::byte> readGeoVert(std::string_view geoPath) const;

    /** Reads the skinning weights associated with the geometry at @p geoPath. */
    std::vector<std::byte> readGeoWeights(std::string_view geoPath) const;

    /** Reads animation track data associated with the animation at @p animPath. */
    std::vector<std::byte> readAnimTracks(std::string_view animPath) const;

    /** Reads track data for a specific @p clip within the animation at @p animPath. */
    std::vector<std::byte> readAnimTracksForClip(std::string_view animPath, const AnimClip& clip) const;

    /** Loads and evaluates the Scheme script at @p path in @p scheme. */
    bool loadScript(s7_scheme* scheme, std::string_view path);

    /** Loads and evaluates the map CSG script at @p path in @p scheme. */
    bool loadMapCsg(s7_scheme* scheme, std::string_view path);

    /** Loads and evaluates the map entities script at @p path in @p scheme. */
    bool loadMapEntities(s7_scheme* scheme, std::string_view path);

private:
    static std::string cacheKey(std::string_view path);
    void mountPackages(const AppConfig& config);
    Shader getSkinningShader();
    void applySkinningToModel(Model& model);
    void ensureSkeletonBindLoaded(std::string_view skeletonPath);
    static std::string skeletonBindPath(std::string_view skeletonPath);

    VirtualFileSystem vfs_;
    Shader skinningShader_{};
    std::unordered_map<std::string, Texture2D> textures_;
    std::unordered_map<std::string, Model> models_;
    std::unordered_map<std::string, Model> geos_;
    std::unordered_map<std::string, MaterialAsset> materialAssets_;
    std::unordered_map<std::string, SkeletonAsset> skeletons_;
    std::unordered_map<std::string, std::vector<Matrix>> skeletonBindMatrices_;
    std::unordered_map<std::string, AnimBank> animBanks_;
    std::unordered_map<std::string, SpriteAsset> spriteAssets_;
    std::unordered_map<std::string, SpriteAtlas> spriteAtlases_;
    std::unordered_map<std::string, SpriteAnimBank> spriteAnimBanks_;
};

}
