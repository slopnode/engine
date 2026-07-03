#pragma once

#include "assets/anim_loader.hpp"
#include "assets/material_loader.hpp"
#include "assets/skeleton_loader.hpp"
#include "core/vfs.hpp"
#include "game/app_config.hpp"

#include <raylib.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct s7_scheme;

namespace daggerlike {

class AssetStore {
public:
    explicit AssetStore(const AppConfig& config);
    ~AssetStore();

    AssetStore(const AssetStore&) = delete;
    AssetStore& operator=(const AssetStore&) = delete;

    bool hasTexture(std::string_view path) const;
    bool hasMaterial(std::string_view path) const;
    bool hasMesh(std::string_view path) const;
    bool hasShader(std::string_view path) const;
    bool hasScript(std::string_view path) const;
    bool hasSkeleton(std::string_view path) const;
    bool hasGeo(std::string_view path) const;
    bool hasGeoVert(std::string_view path) const;
    bool hasGeoWeights(std::string_view path) const;
    bool hasAnim(std::string_view path) const;
    bool hasAnimTracks(std::string_view path) const;

    Texture2D getTexture(std::string_view path);
    Model getModel(std::string_view path);
    Model getGeoModel(std::string_view path);
    Material resolveMaterial(std::string_view path);
    const SkeletonAsset* getSkeletonAsset(std::string_view path);
    const std::vector<Matrix>* getSkeletonBindMatrices(std::string_view skeletonPath);
    const AnimBank* getAnimBank(std::string_view path);
    void unloadTexture(std::string_view path);

    std::string getShaderSource(std::string_view path);
    std::string getMaterialSource(std::string_view path);
    std::string getScriptSource(std::string_view path);
    std::string getSkeletonSource(std::string_view path);
    std::string getGeoSource(std::string_view path);
    std::string getAnimSource(std::string_view path);

    std::vector<std::byte> readBinary(std::string_view path, AssetKind kind);
    bool readBinary(std::string_view path, AssetKind kind, std::span<std::byte> out, std::size_t offset = 0) const;
    std::vector<std::byte> readGeoVert(std::string_view geoPath) const;
    std::vector<std::byte> readGeoWeights(std::string_view geoPath) const;
    std::vector<std::byte> readAnimTracks(std::string_view animPath) const;
    std::vector<std::byte> readAnimTracksForClip(std::string_view animPath, const AnimClip& clip) const;

    bool loadScript(s7_scheme* scheme, std::string_view path);

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
};

}
