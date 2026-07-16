#include "assets/asset_store.hpp"

#include "assets/geo_loader.hpp"

#include <s7.h>

#include <fstream>
#include <functional>
#include <stdexcept>
#include <unordered_map>

namespace slopengine {

namespace {

std::string stripExtension(std::string_view path, std::string_view extension) {
    std::string value{path};
    if (value.size() >= extension.size() &&
        value.compare(value.size() - extension.size(), extension.size(), extension) == 0) {
        value.resize(value.size() - extension.size());
    }
    return value;
}

std::string parentVirtualPath(std::string_view path) {
    const std::string value{path};
    const auto slash = value.find_last_of('/');
    if (slash == std::string::npos) {
        return value;
    }
    return value.substr(0, slash);
}

std::string tracksVirtualPath(std::string_view animPath, const AnimClip& clip) {
    if (clip.tracksImplicit) {
        return std::string{animPath};
    }

    std::string tracks = stripExtension(clip.tracksFile, ".tracks");
    if (tracks.find('/') != std::string::npos) {
        return tracks;
    }

    const std::string parent = parentVirtualPath(animPath);
    if (parent.empty()) {
        return tracks;
    }
    return parent + "/" + tracks;
}

std::string resolveSkeletonVirtualPath(
    std::string_view skeletonId,
    std::string_view assetPath,
    const std::function<bool(std::string_view)>& exists) {
    auto tryPath = [&](std::string_view candidate) -> std::string {
        if (!candidate.empty() && exists(candidate)) {
            return std::string{candidate};
        }
        return {};
    };

    if (std::string found = tryPath(skeletonId); !found.empty()) {
        return found;
    }

    if (!assetPath.empty()) {
        if (std::string found = tryPath(parentVirtualPath(assetPath)); !found.empty()) {
            return found;
        }
        if (std::string found = tryPath(assetPath); !found.empty()) {
            return found;
        }
    }

    const std::string id{skeletonId};
    if (!id.empty() && id.find('/') == std::string::npos) {
        const std::string nested = id + "/" + id;
        if (std::string found = tryPath(nested); !found.empty()) {
            return found;
        }
    }

    return id;
}

} // namespace

AssetStore::AssetStore(const AppConfig& config) {
    mountPackages(config);
}

AssetStore::~AssetStore() {
    if (skinningShader_.id != 0) {
        UnloadShader(skinningShader_);
        skinningShader_ = {};
    }
    for (auto& [_, bank] : animBanks_) {
        unloadAnimBank(bank);
    }
    for (auto& [_, texture] : textures_) {
        UnloadTexture(texture);
    }
    for (auto& [_, model] : models_) {
        if (model.meshCount > 0) {
            UnloadModel(model);
        }
    }
    for (auto& [_, model] : geos_) {
        if (model.meshCount > 0) {
            UnloadModel(model);
        }
    }
}

void AssetStore::mountPackages(const AppConfig& config) {
    Package base{config.base_game};
    if (!base.valid()) {
        throw std::runtime_error("base game package not found: " + config.base_game.string());
    }
    if (!base.hasMeta()) {
        throw std::runtime_error("base game missing package.meta: " + config.base_game.string());
    }

    vfs_.setBasePackage(std::move(base));

    for (const auto& modPath : config.mods) {
        Package mod{modPath};
        if (!mod.valid()) {
            throw std::runtime_error("mod package not found: " + modPath.string());
        }
        if (!mod.hasMeta()) {
            throw std::runtime_error("mod missing package.meta: " + modPath.string());
        }
        vfs_.addPackage(std::move(mod));
    }

    std::unordered_map<std::string, std::filesystem::path> ids;
    for (const Package& package : vfs_.packages()) {
        const auto& meta = package.meta();
        if (ids.contains(meta.id)) {
            throw std::runtime_error(
                "duplicate package id '" + meta.id + "': " + package.root().string()
                + " conflicts with " + ids[meta.id].string());
        }
        ids.emplace(meta.id, package.root());
    }

    for (const Package& package : vfs_.packages()) {
        for (const std::string& depend : package.meta().depends) {
            if (!ids.contains(depend)) {
                throw std::runtime_error(
                    "package '" + package.meta().id + "' depends on missing package '" + depend + "'");
            }
        }
    }
}

std::string AssetStore::cacheKey(std::string_view path) {
    return std::string{path};
}

bool AssetStore::hasTexture(std::string_view path) const {
    return vfs_.exists(AssetKind::Texture, path);
}

bool AssetStore::hasMaterial(std::string_view path) const {
    return vfs_.exists(AssetKind::Material, path);
}

bool AssetStore::hasMesh(std::string_view path) const {
    return vfs_.exists(AssetKind::Mesh, path);
}

bool AssetStore::hasShader(std::string_view path) const {
    return vfs_.exists(AssetKind::Shader, path);
}

bool AssetStore::hasScript(std::string_view path) const {
    return vfs_.exists(AssetKind::Script, path);
}

bool AssetStore::hasMapCsg(std::string_view path) const {
    return vfs_.exists(AssetKind::MapCsg, path);
}

bool AssetStore::hasMapMeta(std::string_view path) const {
    return vfs_.exists(AssetKind::MapMeta, path);
}

bool AssetStore::hasSkeleton(std::string_view path) const {
    return vfs_.exists(AssetKind::Skeleton, path);
}

bool AssetStore::hasGeo(std::string_view path) const {
    return vfs_.exists(AssetKind::Geo, path);
}

bool AssetStore::hasGeoVert(std::string_view path) const {
    return vfs_.exists(AssetKind::GeoVert, path);
}

bool AssetStore::hasGeoWeights(std::string_view path) const {
    return vfs_.exists(AssetKind::GeoWeights, path);
}

bool AssetStore::hasAnim(std::string_view path) const {
    return vfs_.exists(AssetKind::Anim, path);
}

bool AssetStore::hasAnimTracks(std::string_view path) const {
    return vfs_.exists(AssetKind::AnimTracks, path);
}

Texture2D AssetStore::getTexture(std::string_view path) {
    const auto key = cacheKey(path);
    const auto existing = textures_.find(key);
    if (existing != textures_.end()) {
        return existing->second;
    }

    const auto resolved = vfs_.resolve(AssetKind::Texture, path);
    if (!resolved) {
        return {};
    }

    const Texture2D texture = LoadTexture(resolved->string().c_str());
    if (texture.id == 0) {
        return {};
    }

    textures_.emplace(key, texture);
    return texture;
}

Model AssetStore::getModel(std::string_view path) {
    const auto key = cacheKey(path);
    const auto existing = models_.find(key);
    if (existing != models_.end()) {
        return existing->second;
    }

    const auto resolved = vfs_.resolve(AssetKind::Mesh, path);
    if (!resolved) {
        return {};
    }

    const Model model = LoadModel(resolved->string().c_str());
    if (model.meshCount == 0) {
        return {};
    }

    models_.emplace(key, model);
    return model;
}

Model AssetStore::getGeoModel(std::string_view path) {
    const auto key = cacheKey(path);
    const auto existing = geos_.find(key);
    if (existing != geos_.end()) {
        return existing->second;
    }

    if (!hasGeo(path) || !hasGeoVert(path)) {
        return {};
    }

    GeoAsset asset{};
    if (!parseGeoAsset(getGeoSource(path), asset)) {
        return {};
    }

    const auto vertBytes = readGeoVert(path);
    VertBuffer buffer{};
    if (!loadVertBuffer(vertBytes, buffer)) {
        return {};
    }

    const SkeletonAsset* skeleton = nullptr;
    std::string skeletonPath = asset.skeletonId;
    if (!skeletonPath.empty()) {
        skeletonPath = resolveSkeletonVirtualPath(
            skeletonPath,
            path,
            [this](std::string_view candidate) { return hasSkeleton(candidate); });
        skeleton = getSkeletonAsset(skeletonPath);
    } else {
        const std::string inferred = parentVirtualPath(path);
        if (hasSkeleton(inferred)) {
            skeletonPath = inferred;
            skeleton = getSkeletonAsset(skeletonPath);
        } else if (hasSkeleton(path)) {
            skeletonPath = std::string{path};
            skeleton = getSkeletonAsset(skeletonPath);
        }
    }

    std::vector<VertexWeights> weights{};
    const std::vector<VertexWeights>* weightsPtr = nullptr;
    if (asset.weightsImplicit && hasGeoWeights(path)) {
        if (loadWeightsBuffer(readGeoWeights(path), weights)) {
            weightsPtr = &weights;
        }
    }

    Model model = buildModelFromGeo(
        asset,
        buffer,
        [this](std::string_view materialPath) {
            return resolveMaterial(materialPath);
        },
        weightsPtr,
        skeleton != nullptr ? static_cast<int>(skeleton->bones.size()) : 0);
    if (model.meshCount == 0) {
        return {};
    }

    if (skeleton != nullptr) {
        applySkeletonToModel(*skeleton, model);
        if (!skeletonPath.empty()) {
            const std::vector<Matrix>* bindMatrices = getSkeletonBindMatrices(skeletonPath);
            if (bindMatrices != nullptr && !bindMatrices->empty()) {
                applyBindPoseFromGlobalMatrices(model, *bindMatrices);
            }
        }
    }

    if (skeleton != nullptr && weightsPtr != nullptr) {
        applySkinningToModel(model);
    }

    geos_.emplace(key, model);
    return model;
}

Shader AssetStore::getSkinningShader() {
    if (skinningShader_.id != 0) {
        return skinningShader_;
    }

    if (!hasShader("default/skinning_vert") || !hasShader("default/skinning_frag")) {
        TraceLog(LOG_WARNING, "Skinning shaders not found in shaders/default/");
        return {};
    }

    const std::string vertexSource = getShaderSource("default/skinning_vert");
    const std::string fragmentSource = getShaderSource("default/skinning_frag");
    skinningShader_ = LoadShaderFromMemory(vertexSource.c_str(), fragmentSource.c_str());
    if (skinningShader_.id == 0) {
        TraceLog(LOG_WARNING, "Failed to compile skinning shader");
        return {};
    }

    return skinningShader_;
}

void AssetStore::applySkinningToModel(Model& model) {
    allocateModelSkinningBuffers(model);
}

void AssetStore::unloadTexture(std::string_view path) {
    const auto key = cacheKey(path);
    const auto it = textures_.find(key);
    if (it == textures_.end()) {
        return;
    }

    UnloadTexture(it->second);
    textures_.erase(it);
}

std::string AssetStore::getShaderSource(std::string_view path) {
    return vfs_.readText(AssetKind::Shader, path);
}

std::string AssetStore::getMaterialSource(std::string_view path) {
    return vfs_.readText(AssetKind::Material, path);
}

Material AssetStore::resolveMaterial(std::string_view path) {
    const TextureResolver resolveTexture = [this](std::string_view texturePath) {
        return getTexture(texturePath);
    };

    const MaterialAsset* asset = getMaterialAsset(path);
    if (asset == nullptr) {
        return createRaylibMaterial({}, resolveTexture);
    }
    return createRaylibMaterial(*asset, resolveTexture);
}

const MaterialAsset* AssetStore::getMaterialAsset(std::string_view path) {
    const std::string key = cacheKey(path.empty() ? "default/unassigned" : path);
    const auto existing = materialAssets_.find(key);
    if (existing != materialAssets_.end()) {
        return &existing->second;
    }

    MaterialAsset asset{};
    const std::string_view lookupPath = path.empty() ? "default/unassigned" : path;
    if (hasMaterial(lookupPath)) {
        if (!parseMaterialAsset(getMaterialSource(lookupPath), asset)) {
            TraceLog(LOG_WARNING, "Failed to parse material: %s", key.c_str());
            asset = {};
        }
    } else {
        TraceLog(LOG_WARNING, "Material not found: %s", key.c_str());
        asset = {};
    }

    return &materialAssets_.emplace(key, std::move(asset)).first->second;
}

const SkeletonAsset* AssetStore::getSkeletonAsset(std::string_view path) {
    const std::string key = cacheKey(path);
    const auto existing = skeletons_.find(key);
    if (existing != skeletons_.end()) {
        return &existing->second;
    }

    if (!hasSkeleton(path)) {
        return nullptr;
    }

    SkeletonAsset asset{};
    if (!parseSkeletonAsset(getSkeletonSource(path), asset)) {
        TraceLog(LOG_WARNING, "Failed to parse skeleton: %s", key.c_str());
        return nullptr;
    }

    const auto inserted = skeletons_.emplace(key, std::move(asset));
    ensureSkeletonBindLoaded(path);
    return &inserted.first->second;
}

std::string AssetStore::skeletonBindPath(std::string_view skeletonPath) {
    std::string path{skeletonPath};
    if (path.size() >= 5 && path.compare(path.size() - 5, 5, ".skel") == 0) {
        path.resize(path.size() - 5);
    }
    if (path.size() >= 5 && path.compare(path.size() - 5, 5, ".bind") == 0) {
        path.resize(path.size() - 5);
    }
    return path;
}

void AssetStore::ensureSkeletonBindLoaded(std::string_view skeletonPath) {
    const std::string key = cacheKey(skeletonPath);
    if (skeletonBindMatrices_.find(key) != skeletonBindMatrices_.end()) {
        return;
    }

    const std::string bindPath = skeletonBindPath(skeletonPath);
    if (!vfs_.exists(AssetKind::SkeletonBind, bindPath)) {
        skeletonBindMatrices_.emplace(key, std::vector<Matrix>{});
        return;
    }

    const std::vector<std::byte> bindBytes = vfs_.readBinary(AssetKind::SkeletonBind, bindPath);
    std::vector<Matrix> bindMatrices{};
    if (!loadSkeletonBindMatrices(bindBytes, bindMatrices)) {
        TraceLog(LOG_WARNING, "Failed to load skeleton bind matrices: %s", bindPath.c_str());
        skeletonBindMatrices_.emplace(key, std::vector<Matrix>{});
        return;
    }

    skeletonBindMatrices_.emplace(key, std::move(bindMatrices));
}

const std::vector<Matrix>* AssetStore::getSkeletonBindMatrices(std::string_view skeletonPath) {
    ensureSkeletonBindLoaded(skeletonPath);
    const std::string key = cacheKey(skeletonPath);
    const auto it = skeletonBindMatrices_.find(key);
    if (it == skeletonBindMatrices_.end() || it->second.empty()) {
        return nullptr;
    }
    return &it->second;
}

const AnimBank* AssetStore::getAnimBank(std::string_view path) {
    const std::string key = cacheKey(path);
    const auto existing = animBanks_.find(key);
    if (existing != animBanks_.end()) {
        return &existing->second;
    }

    if (!hasAnim(path)) {
        return nullptr;
    }

    AnimAsset asset{};
    if (!parseAnimAsset(getAnimSource(path), asset)) {
        TraceLog(LOG_WARNING, "Failed to parse animation bank: %s", key.c_str());
        return nullptr;
    }

    std::string skeletonPath = resolveSkeletonVirtualPath(
        asset.skeletonId,
        path,
        [this](std::string_view candidate) { return hasSkeleton(candidate); });

    const SkeletonAsset* skeleton = getSkeletonAsset(skeletonPath);
    if (skeleton == nullptr) {
        TraceLog(LOG_WARNING, "Animation bank '%s' missing skeleton '%s'", key.c_str(), skeletonPath.c_str());
        return nullptr;
    }

    AnimBank bank{};
    const std::string animPath{path};
    if (!buildAnimBank(
            asset,
            *skeleton,
            [this, animPath](const AnimClip& clip) {
                return readAnimTracksForClip(animPath, clip);
            },
            bank)) {
        TraceLog(LOG_WARNING, "Failed to build animation bank: %s", key.c_str());
        return nullptr;
    }

    bank.skeletonId = skeletonPath;

    const auto inserted = animBanks_.emplace(key, std::move(bank));
    return &inserted.first->second;
}

std::string AssetStore::getScriptSource(std::string_view path) {
    return vfs_.readText(AssetKind::Script, path);
}

std::string AssetStore::getMapMetaSource(std::string_view path) {
    return vfs_.readText(AssetKind::MapMeta, path);
}

const std::vector<Package>& AssetStore::packages() const {
    return vfs_.packages();
}

bool AssetStore::hasPackageId(std::string_view packageId) const {
    for (const Package& package : vfs_.packages()) {
        if (package.meta().id == packageId) {
            return true;
        }
    }
    return false;
}

std::string AssetStore::getSkeletonSource(std::string_view path) {
    return vfs_.readText(AssetKind::Skeleton, path);
}

std::string AssetStore::getGeoSource(std::string_view path) {
    return vfs_.readText(AssetKind::Geo, path);
}

std::string AssetStore::getAnimSource(std::string_view path) {
    return vfs_.readText(AssetKind::Anim, path);
}

std::vector<std::byte> AssetStore::readBinary(std::string_view path, AssetKind kind) {
    return vfs_.readBinary(kind, path);
}

bool AssetStore::readBinary(std::string_view path, AssetKind kind, std::span<std::byte> out, std::size_t offset) const {
    const auto resolved = vfs_.resolve(kind, path);
    if (!resolved) {
        return false;
    }

    std::ifstream file{*resolved, std::ios::binary};
    if (!file) {
        return false;
    }

    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return static_cast<std::size_t>(file.gcount()) == out.size();
}

std::vector<std::byte> AssetStore::readGeoVert(std::string_view geoPath) const {
    return vfs_.readBinary(AssetKind::GeoVert, geoPath);
}

std::vector<std::byte> AssetStore::readGeoWeights(std::string_view geoPath) const {
    return vfs_.readBinary(AssetKind::GeoWeights, geoPath);
}

std::vector<std::byte> AssetStore::readAnimTracks(std::string_view animPath) const {
    return vfs_.readBinary(AssetKind::AnimTracks, animPath);
}

std::vector<std::byte> AssetStore::readAnimTracksForClip(std::string_view animPath, const AnimClip& clip) const {
    return vfs_.readBinary(AssetKind::AnimTracks, tracksVirtualPath(animPath, clip));
}

bool AssetStore::loadScript(s7_scheme* scheme, std::string_view path) {
    const auto resolved = vfs_.resolve(AssetKind::Script, path);
    if (!resolved) {
        return false;
    }

    s7_load(scheme, resolved->string().c_str());
    return true;
}

bool AssetStore::loadMapCsg(s7_scheme* scheme, std::string_view path) {
    const auto resolved = vfs_.resolve(AssetKind::MapCsg, path);
    if (!resolved) {
        return false;
    }

    s7_load(scheme, resolved->string().c_str());
    return true;
}

}
