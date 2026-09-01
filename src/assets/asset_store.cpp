#include "assets/asset_store.hpp"

#include "assets/geo_loader.hpp"
#include "assets/saudio_loader.hpp"
#include "core/engine_package.hpp"
#include "core/package_search.hpp"
#include "core/semver.hpp"
#include "core/user_paths.hpp"
#include "map/map_meta.hpp"
#include "script/package_load_context.hpp"
#include "script/proc_role.hpp"

#include <rlgl.h>
#include "external/glad.h"

#include <s7.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <system_error>
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
    releaseGpuResources();
}

void AssetStore::releaseGpuResources() {
    if (skinningShader_.id != 0) {
        UnloadShader(skinningShader_);
        skinningShader_ = {};
    }
    for (auto& [_, bank] : animBanks_) {
        unloadAnimBank(bank);
    }
    animBanks_.clear();
    for (auto& [_, atlas] : spriteAtlases_) {
        unloadSpriteAtlas(atlas);
    }
    spriteAtlases_.clear();
    for (auto& [_, atlas] : iconAtlases_) {
        unloadIconAtlas(atlas);
    }
    iconAtlases_.clear();
    for (auto& [_, texture] : textures_) {
        UnloadTexture(texture);
    }
    textures_.clear();
    for (auto& [_, cubemap] : cubemaps_) {
        UnloadTexture(cubemap);
    }
    cubemaps_.clear();
    for (auto& [_, model] : models_) {
        if (model.meshCount > 0) {
            UnloadModel(model);
        }
    }
    models_.clear();
    for (auto& [_, model] : geos_) {
        if (model.meshCount > 0) {
            UnloadModel(model);
        }
    }
    geos_.clear();
}

void AssetStore::mountPackages(const AppConfig& config) {
    const auto enginePath = resolveEnginePackage();
    if (!enginePath) {
        throw std::runtime_error(
            "engine package not found (tried ./packages/engine, $SLOPENGINE_ENGINE, and the "
            "configured install path)");
    }

    Package engine{*enginePath};
    if (!engine.valid()) {
        throw std::runtime_error("engine package not found: " + enginePath->string());
    }
    if (!engine.hasMeta()) {
        throw std::runtime_error("engine package missing package.meta: " + enginePath->string());
    }
    engine.setRole(PackageRole::Engine);

    const std::vector<std::filesystem::path> searchPaths =
        applicationSearchPaths(userConfiguredSearchPaths());

    const std::filesystem::path baseGamePath =
        resolveApplicationPackagePath(config.base_game, searchPaths);
    Package base{baseGamePath};
    if (!base.valid()) {
        throw std::runtime_error(
            "base game package not found: " + baseGamePath.string()
            + " (looked up as a path, then by name under the configured search paths)");
    }
    if (!base.hasMeta()) {
        throw std::runtime_error("base game missing package.meta: " + baseGamePath.string());
    }
    base.setRole(PackageRole::Base);

    vfs_.setBasePackage(std::move(engine));
    vfs_.addPackage(std::move(base));

    for (const auto& modArg : config.mods) {
        const std::filesystem::path modPath = resolveApplicationPackagePath(modArg, searchPaths);
        Package mod{modPath};
        if (!mod.valid()) {
            throw std::runtime_error(
                "mod package not found: " + modPath.string()
                + " (looked up as a path, then by name under the configured search paths)");
        }
        if (!mod.hasMeta()) {
            throw std::runtime_error("mod missing package.meta: " + modPath.string());
        }
        mod.setRole(PackageRole::Mod);
        vfs_.addPackage(std::move(mod));
    }

    std::unordered_map<std::string, const Package*> ids;
    for (const Package& package : vfs_.packages()) {
        const auto& meta = package.meta();
        if (ids.contains(meta.id)) {
            throw std::runtime_error(
                "duplicate package id '" + meta.id + "': " + package.root().string()
                + " conflicts with " + ids[meta.id]->root().string());
        }
        ids.emplace(meta.id, &package);
    }

    for (const Package& package : vfs_.packages()) {
        for (const PackageDependency& depend : package.meta().depends) {
            const auto it = ids.find(depend.id);
            if (it == ids.end()) {
                throw std::runtime_error(
                    "package '" + package.meta().id + "' depends on missing package '" + depend.id
                    + "'");
            }
            if (!depend.versionConstraint.empty()
                && !satisfiesVersionConstraint(it->second->meta().version, depend.versionConstraint)) {
                throw std::runtime_error(
                    "package '" + package.meta().id + "' requires '" + depend.id + "@"
                    + depend.versionConstraint + "' but found version '" + it->second->meta().version
                    + "'");
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

bool AssetStore::hasFont(std::string_view path) const {
    return vfs_.exists(AssetKind::Font, path);
}

bool AssetStore::hasSound(std::string_view path) const {
    return vfs_.exists(AssetKind::Sound, path) || vfs_.exists(AssetKind::SoundWav, path);
}

std::optional<std::filesystem::path> AssetStore::resolveSoundPath(std::string_view path) const {
    if (auto ogg = vfs_.resolve(AssetKind::Sound, path)) {
        return ogg;
    }
    return vfs_.resolve(AssetKind::SoundWav, path);
}

bool AssetStore::hasAudio(std::string_view path) const {
    return vfs_.exists(AssetKind::AudioSaudio, path) || vfs_.exists(AssetKind::Audio, path);
}

bool AssetStore::commitAudioDef(AudioDef def) {
    if (!audioDefLoadActive_) {
        return false;
    }
    if (def.kind == AudioDefKind::Sample && def.source.empty()) {
        return false;
    }
    audioDefStaging_ = std::move(def);
    audioDefRegistered_ = true;
    return true;
}

const AudioDef* AssetStore::getAudioDef(s7_scheme* scheme, std::string_view path) {
    const std::string key = cacheKey(path);
    if (auto it = audioDefs_.find(key); it != audioDefs_.end()) {
        return &it->second;
    }

    if (vfs_.exists(AssetKind::AudioSaudio, path)) {
        const std::string text = vfs_.readText(AssetKind::AudioSaudio, path);
        AudioDef def{};
        if (!parseSaudioAsset(text, def)) {
            return nullptr;
        }
        auto [it, _] = audioDefs_.emplace(key, std::move(def));
        return &it->second;
    }

    if (scheme == nullptr) {
        return nullptr;
    }

    const auto resolved = vfs_.resolve(AssetKind::Audio, path);
    if (!resolved) {
        return nullptr;
    }

    audioDefLoadPath_ = key;
    audioDefStaging_ = {};
    audioDefLoadActive_ = true;
    audioDefRegistered_ = false;
    s7_load(scheme, resolved->string().c_str());
    audioDefLoadActive_ = false;

    if (!audioDefRegistered_) {
        audioDefLoadPath_.clear();
        return nullptr;
    }

    audioDefStaging_.kind = AudioDefKind::Sample;
    auto [it, _] = audioDefs_.emplace(key, std::move(audioDefStaging_));
    audioDefLoadPath_.clear();
    audioDefStaging_ = {};
    audioDefRegistered_ = false;
    return &it->second;
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

bool AssetStore::hasMapSource(std::string_view path) const {
    return vfs_.exists(AssetKind::MapSource, path);
}

bool AssetStore::hasMapCarved(std::string_view path) const {
    return vfs_.exists(AssetKind::MapCarved, path);
}

bool AssetStore::hasMapThings(std::string_view path) const {
    return vfs_.exists(AssetKind::MapThings, path);
}

bool AssetStore::hasMapGraphs(std::string_view path) const {
    return vfs_.exists(AssetKind::MapGraphs, path);
}

bool AssetStore::hasData(std::string_view path) const {
    return vfs_.exists(AssetKind::Data, path);
}

bool AssetStore::hasPrefabSource(std::string_view path) const {
    return vfs_.exists(AssetKind::PrefabSource, path);
}

bool AssetStore::hasPrefabThings(std::string_view path) const {
    return vfs_.exists(AssetKind::PrefabThings, path);
}

bool AssetStore::hasMapMeta(std::string_view path) const {
    return vfs_.exists(AssetKind::MapMeta, path);
}

std::vector<AssetStore::MapListEntry> AssetStore::listMaps() const {
    std::unordered_map<std::string, MapListEntry> byId;
    for (const Package& package : vfs_.packages()) {
        const std::filesystem::path mapsRoot = package.root() / "maps";
        if (!std::filesystem::exists(mapsRoot)) {
            continue;
        }
        std::error_code ec;
        for (std::filesystem::directory_iterator it(mapsRoot, ec), end; it != end && !ec;
             it.increment(ec)) {
            if (ec || !it->is_directory()) {
                continue;
            }
            const std::filesystem::path metaPath = it->path() / "map.meta";
            if (!std::filesystem::exists(metaPath)) {
                continue;
            }
            const std::string id = it->path().filename().generic_string();
            if (id.empty()) {
                continue;
            }
            MapListEntry entry{};
            entry.id = id;
            entry.name = id;
            const std::string source = vfs_.readText(AssetKind::MapMeta, id + "/map");
            MapMeta meta{};
            if (parseMapMeta(source, meta) && !meta.name.empty()) {
                entry.name = meta.name;
            }
            byId[id] = std::move(entry);
        }
    }

    std::vector<MapListEntry> maps;
    maps.reserve(byId.size());
    for (auto& [_, entry] : byId) {
        maps.push_back(std::move(entry));
    }
    std::sort(maps.begin(), maps.end(), [](const MapListEntry& a, const MapListEntry& b) {
        return a.name < b.name;
    });
    return maps;
}

bool AssetStore::hasMapBsp(std::string_view path) const {
    return vfs_.exists(AssetKind::MapBsp, path);
}

bool AssetStore::hasMapVis(std::string_view path) const {
    return vfs_.exists(AssetKind::MapVis, path);
}

bool AssetStore::hasMapNav(std::string_view path) const {
    return vfs_.exists(AssetKind::MapNav, path);
}

bool AssetStore::hasMapRad(std::string_view path) const {
    return vfs_.exists(AssetKind::MapRad, path);
}

std::optional<std::filesystem::path> AssetStore::resolvePath(AssetKind kind, std::string_view path) const {
    return vfs_.resolve(kind, path);
}

std::optional<ResolvedAsset> AssetStore::resolveOwned(AssetKind kind, std::string_view path) const {
    return vfs_.resolveOwned(kind, path);
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

bool AssetStore::hasSprite(std::string_view path) const {
    return vfs_.exists(AssetKind::Sprite, path);
}

bool AssetStore::hasSpriteAnim(std::string_view path) const {
    return vfs_.exists(AssetKind::SpriteAnim, path);
}

bool AssetStore::hasTextureAnim(std::string_view path) const {
    return vfs_.exists(AssetKind::TextureAnim, path);
}

bool AssetStore::hasParticle(std::string_view path) const {
    return vfs_.exists(AssetKind::Particle, path);
}

bool AssetStore::hasIconAtlas(std::string_view set) const {
    return vfs_.exists(AssetKind::IconMap, set);
}

const IconAtlas* AssetStore::getIconAtlas(std::string_view set) {
    const std::string key = cacheKey(set);
    const auto existing = iconAtlases_.find(key);
    if (existing != iconAtlases_.end()) {
        return &existing->second;
    }

    if (!hasIconAtlas(set)) {
        TraceLog(LOG_WARNING, "Icon atlas not found: %s", key.c_str());
        return nullptr;
    }

    IconAtlas atlas{};
    if (!parseIconMap(vfs_.readText(AssetKind::IconMap, set), atlas)) {
        TraceLog(LOG_WARNING, "Failed to parse iconmap: %s", key.c_str());
        return nullptr;
    }

    const std::string atlasVirtual =
        atlas.atlasPath.empty() ? key : atlas.atlasPath;
    const auto resolved = vfs_.resolve(AssetKind::Icon, atlasVirtual);
    if (!resolved) {
        TraceLog(LOG_WARNING, "Icon atlas texture not found: %s", atlasVirtual.c_str());
        return nullptr;
    }

    atlas.texture = LoadTexture(resolved->string().c_str());
    if (atlas.texture.id == 0) {
        TraceLog(LOG_WARNING, "Failed to load icon atlas texture: %s", atlasVirtual.c_str());
        return nullptr;
    }

    return &iconAtlases_.emplace(key, std::move(atlas)).first->second;
}

std::optional<Rectangle> AssetStore::getIconRect(std::string_view set, std::string_view id) {
    const IconAtlas* atlas = getIconAtlas(set);
    if (atlas == nullptr) {
        return std::nullopt;
    }
    return findIconRect(*atlas, id);
}

bool AssetStore::drawIcon(std::string_view set, std::string_view id, Vector2 position, float size) {
    const IconAtlas* atlas = getIconAtlas(set);
    if (atlas == nullptr || atlas->texture.id == 0) {
        return false;
    }
    const auto rect = findIconRect(*atlas, id);
    if (!rect) {
        return false;
    }
    DrawTexturePro(
        atlas->texture,
        *rect,
        Rectangle{position.x, position.y, size, size},
        Vector2{0.0f, 0.0f},
        0.0f,
        WHITE);
    return true;
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

namespace {

std::string cubemapCacheKey(
    std::string_view px,
    std::string_view nx,
    std::string_view py,
    std::string_view ny,
    std::string_view pz,
    std::string_view nz) {
    std::string key;
    key.reserve(
        px.size() + nx.size() + py.size() + ny.size() + pz.size() + nz.size() + 6);
    for (std::string_view face : {px, nx, py, ny, pz, nz}) {
        key.append(face);
        key.push_back('\0');
    }
    return key;
}

} // namespace

TextureCubemap AssetStore::getCubemapFaces(
    std::string_view px,
    std::string_view nx,
    std::string_view py,
    std::string_view ny,
    std::string_view pz,
    std::string_view nz) {
    const std::string key = cubemapCacheKey(px, nx, py, ny, pz, nz);
    const auto existing = cubemaps_.find(key);
    if (existing != cubemaps_.end()) {
        return existing->second;
    }

    const std::string_view faces[6] = {px, nx, py, ny, pz, nz};
    Texture2D faceTextures[6]{};
    int faceWidth = 0;
    int faceHeight = 0;
    int faceFormat = 0;
    for (int i = 0; i < 6; ++i) {
        faceTextures[i] = getTexture(faces[i]);
        if (faceTextures[i].id == 0) {
            TraceLog(LOG_WARNING, "ASSET: cubemap face missing '%.*s'", static_cast<int>(faces[i].size()), faces[i].data());
            return {};
        }
        if (i == 0) {
            faceWidth = faceTextures[i].width;
            faceHeight = faceTextures[i].height;
            faceFormat = faceTextures[i].format;
        }
        if (faceTextures[i].width != faceTextures[i].height) {
            TraceLog(
                LOG_WARNING,
                "ASSET: cubemap face '%.*s' is %dx%d, not square; cube faces must be square and share the same size",
                static_cast<int>(faces[i].size()), faces[i].data(),
                faceTextures[i].width, faceTextures[i].height);
            return {};
        }
        if (faceTextures[i].width != faceWidth || faceTextures[i].height != faceHeight) {
            TraceLog(
                LOG_WARNING,
                "ASSET: cubemap face '%.*s' is %dx%d, expected %dx%d to match the other faces",
                static_cast<int>(faces[i].size()), faces[i].data(),
                faceTextures[i].width, faceTextures[i].height,
                faceWidth, faceHeight);
            return {};
        }
    }

    unsigned int cubemapId = 0;
    glGenTextures(1, &cubemapId);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapId);
    for (int i = 0; i < 6; ++i) {
        Image image = LoadImageFromTexture(faceTextures[i]);
        if (image.data == nullptr) {
            TraceLog(LOG_WARNING, "ASSET: failed to read cubemap face '%.*s'", static_cast<int>(faces[i].size()), faces[i].data());
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
            glDeleteTextures(1, &cubemapId);
            return {};
        }
        glTexImage2D(
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
            0,
            GL_RGBA8,
            image.width,
            image.height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            image.data);
        UnloadImage(image);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    TextureCubemap cubemap{};
    cubemap.id = cubemapId;
    cubemap.width = faceWidth;
    cubemap.height = faceHeight;
    cubemap.format = faceFormat;
    cubemaps_.emplace(key, cubemap);
    return cubemap;
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

std::string AssetStore::getParticleSource(std::string_view path) {
    return vfs_.readText(AssetKind::Particle, path);
}

Material AssetStore::resolveMaterial(std::string_view path) {
    const TextureResolver resolveTexture = [this](std::string_view texturePath) {
        return getTexture(texturePath);
    };
    const TextureAnimFrameResolver resolveAnimFrame = [this](std::string_view animPath, int frameIndex) {
        return resolveTextureAnimFrame(animPath, "default", frameIndex);
    };

    const MaterialAsset* asset = getMaterialAsset(path);
    if (asset == nullptr) {
        return createRaylibMaterial({}, resolveTexture, resolveAnimFrame);
    }
    return createRaylibMaterial(*asset, resolveTexture, resolveAnimFrame);
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

namespace {

std::optional<std::filesystem::file_time_type> newestWriteTime(
    const std::optional<std::filesystem::path>& a,
    const std::optional<std::filesystem::path>& b) {
    std::optional<std::filesystem::file_time_type> newest;
    std::error_code ec;
    if (a) {
        const auto t = std::filesystem::last_write_time(*a, ec);
        if (!ec) {
            newest = t;
        }
    }
    if (b) {
        ec.clear();
        const auto t = std::filesystem::last_write_time(*b, ec);
        if (!ec && (!newest || t > *newest)) {
            newest = t;
        }
    }
    return newest;
}

} // namespace

const SpriteAsset* AssetStore::getSpriteAsset(std::string_view path) {
    const std::string key = cacheKey(path);
    const auto existing = spriteAssets_.find(key);
    if (existing != spriteAssets_.end()) {
        return &existing->second;
    }

    if (!hasSprite(path)) {
        TraceLog(LOG_WARNING, "Sprite not found: %s", key.c_str());
        return nullptr;
    }

    SpriteAsset asset{};
    if (!parseSpriteAsset(getSpriteSource(path), asset)) {
        TraceLog(LOG_WARNING, "Failed to parse sprite: %s", key.c_str());
        return nullptr;
    }

    SpriteAtlas atlas = buildSpriteAtlas(asset, [this](std::string_view texturePath) {
        return resolvePath(AssetKind::Texture, texturePath);
    });

    for (SpriteFrame& frame : asset.frames) {
        for (int rotation = 0; rotation < kSpriteRotationCount; ++rotation) {
            if (!frame.rotations[rotation].has_value()) {
                continue;
            }
            SpriteRotation& entry = *frame.rotations[rotation];
            const auto rectIt = atlas.rects.find(entry.texturePath);
            if (rectIt == atlas.rects.end()) {
                continue;
            }
            entry.pixelWidth = static_cast<int>(rectIt->second.source.width);
            entry.pixelHeight = static_cast<int>(rectIt->second.source.height);
        }
    }

    auto stamp = newestWriteTime(
        resolvePath(AssetKind::Sprite, path), resolvePath(AssetKind::SpriteAnim, path));
    {
        std::error_code ec;
        for (const SpriteFrame& frame : asset.frames) {
            for (int rotation = 0; rotation < kSpriteRotationCount; ++rotation) {
                if (!frame.rotations[rotation].has_value()) {
                    continue;
                }
                if (const auto tex =
                        resolvePath(AssetKind::Texture, frame.rotations[rotation]->texturePath)) {
                    ec.clear();
                    const auto t = std::filesystem::last_write_time(*tex, ec);
                    if (!ec && (!stamp || t > *stamp)) {
                        stamp = t;
                    }
                }
            }
        }
    }
    if (stamp) {
        spriteSourceMtimes_[key] = *stamp;
    }

    spriteAtlases_.emplace(key, std::move(atlas));
    return &spriteAssets_.emplace(key, std::move(asset)).first->second;
}

const SpriteAtlas* AssetStore::getSpriteAtlas(std::string_view path) {
    if (getSpriteAsset(path) == nullptr) {
        return nullptr;
    }
    const auto it = spriteAtlases_.find(cacheKey(path));
    if (it == spriteAtlases_.end()) {
        return nullptr;
    }
    return &it->second;
}

const SpriteAnimBank* AssetStore::getSpriteAnimBank(std::string_view path) {
    const std::string key = cacheKey(path);
    const auto existing = spriteAnimBanks_.find(key);
    if (existing != spriteAnimBanks_.end()) {
        return &existing->second;
    }

    if (!hasSpriteAnim(path)) {
        TraceLog(LOG_WARNING, "Sprite anim not found: %s", key.c_str());
        return nullptr;
    }

    SpriteAnimBank bank{};
    if (!parseSpriteAnimBank(getSpriteAnimSource(path), bank)) {
        TraceLog(LOG_WARNING, "Failed to parse sprite anim: %s", key.c_str());
        return nullptr;
    }

    const auto stamp = newestWriteTime(
        resolvePath(AssetKind::Sprite, path), resolvePath(AssetKind::SpriteAnim, path));
    if (stamp) {
        spriteSourceMtimes_[key] = *stamp;
    }

    return &spriteAnimBanks_.emplace(key, std::move(bank)).first->second;
}

std::string AssetStore::getTextureAnimSource(std::string_view path) {
    return vfs_.readText(AssetKind::TextureAnim, path);
}

const TextureAnimBank* AssetStore::getTextureAnimBank(std::string_view path) {
    const std::string key = cacheKey(path);
    const auto existing = textureAnimBanks_.find(key);
    if (existing != textureAnimBanks_.end()) {
        return &existing->second;
    }

    if (!hasTextureAnim(path)) {
        TraceLog(LOG_WARNING, "Texture anim not found: %s", key.c_str());
        return nullptr;
    }

    TextureAnimBank bank{};
    if (!parseTextureAnimBank(getTextureAnimSource(path), bank)) {
        TraceLog(LOG_WARNING, "Failed to parse texture anim: %s", key.c_str());
        return nullptr;
    }

    return &textureAnimBanks_.emplace(key, std::move(bank)).first->second;
}

Texture2D AssetStore::resolveTextureAnimFrame(
    std::string_view animPath,
    std::string_view clipName,
    int frameIndex) {
    const TextureAnimBank* bank = getTextureAnimBank(animPath);
    if (bank == nullptr) {
        return {};
    }

    const auto clipIt = bank->clipIndexByName.find(std::string{clipName});
    if (clipIt == bank->clipIndexByName.end() || clipIt->second >= bank->clips.size()) {
        return {};
    }

    const TextureAnimClip& clip = bank->clips[clipIt->second];
    const std::string_view texturePath = textureAnimFrameTexture(clip, frameIndex);
    if (texturePath.empty()) {
        return {};
    }
    return getTexture(texturePath);
}

const ParticleSystemAsset* AssetStore::getParticleAsset(std::string_view path) {
    const std::string key = cacheKey(path);
    const auto existing = particleAssets_.find(key);
    if (existing != particleAssets_.end()) {
        return &existing->second;
    }

    if (!hasParticle(path)) {
        TraceLog(LOG_WARNING, "Particle system not found: %s", key.c_str());
        return nullptr;
    }

    ParticleSystemAsset asset{};
    if (!parseParticleSystemAsset(getParticleSource(path), asset)) {
        TraceLog(LOG_WARNING, "Failed to parse particle system: %s", key.c_str());
        return nullptr;
    }

    return &particleAssets_.emplace(key, std::move(asset)).first->second;
}

void AssetStore::invalidateSprite(std::string_view path) {
    const std::string key = cacheKey(path);
    spriteAssets_.erase(key);
    spriteAnimBanks_.erase(key);
    spriteSourceMtimes_.erase(key);
    const auto atlasIt = spriteAtlases_.find(key);
    if (atlasIt != spriteAtlases_.end()) {
        unloadSpriteAtlas(atlasIt->second);
        spriteAtlases_.erase(atlasIt);
    }
}

bool AssetStore::reloadSpriteIfChanged(std::string_view path) {
    const std::string key = cacheKey(path);
    const auto assetIt = spriteAssets_.find(key);
    const bool cached =
        assetIt != spriteAssets_.end() || spriteAnimBanks_.find(key) != spriteAnimBanks_.end();
    if (!cached) {
        return false;
    }
    auto stamp = newestWriteTime(
        resolvePath(AssetKind::Sprite, path), resolvePath(AssetKind::SpriteAnim, path));
    if (assetIt != spriteAssets_.end()) {
        std::error_code ec;
        for (const SpriteFrame& frame : assetIt->second.frames) {
            for (int rotation = 0; rotation < kSpriteRotationCount; ++rotation) {
                if (!frame.rotations[rotation].has_value()) {
                    continue;
                }
                const SpriteRotation& entry = *frame.rotations[rotation];
                if (const auto tex = resolvePath(AssetKind::Texture, entry.texturePath)) {
                    ec.clear();
                    const auto t = std::filesystem::last_write_time(*tex, ec);
                    if (!ec && (!stamp || t > *stamp)) {
                        stamp = t;
                    }
                }
            }
        }
    }
    if (!stamp) {
        return false;
    }
    const auto cachedStamp = spriteSourceMtimes_.find(key);
    if (cachedStamp != spriteSourceMtimes_.end() && *stamp <= cachedStamp->second) {
        return false;
    }
    invalidateSprite(path);
    return true;
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
    return findPackage(packageId) != nullptr;
}

const Package* AssetStore::findPackage(std::string_view packageId) const {
    return vfs_.findPackage(packageId);
}

std::string_view AssetStore::basePackageId() const {
    for (const Package& package : vfs_.packages()) {
        if (package.role() == PackageRole::Base) {
            return package.meta().id;
        }
    }
    return {};
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

std::string AssetStore::getSpriteSource(std::string_view path) {
    return vfs_.readText(AssetKind::Sprite, path);
}

std::string AssetStore::getSpriteAnimSource(std::string_view path) {
    return vfs_.readText(AssetKind::SpriteAnim, path);
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
    const auto owned = vfs_.resolveOwned(AssetKind::Script, path);
    if (!owned) {
        return false;
    }

    const std::string_view packageId =
        owned->package != nullptr ? std::string_view{owned->package->meta().id} : std::string_view{};
    const PackageRole role =
        owned->package != nullptr ? owned->package->role() : PackageRole::Base;
    const ProcRoleSnapshot before = snapshotProcRoles(scheme);
    PackageLoadContextGuard loadGuard(packageId, role);
    s7_load(scheme, owned->path.string().c_str());
    stampProcRoles(scheme, before, role);
    return true;
}

bool AssetStore::loadScriptFromPackage(
    s7_scheme* scheme,
    std::string_view packageId,
    std::string_view path) {
    const auto owned = vfs_.resolveInPackage(AssetKind::Script, packageId, path);
    if (!owned) {
        return false;
    }

    const PackageRole role =
        owned->package != nullptr ? owned->package->role() : PackageRole::Base;
    const ProcRoleSnapshot before = snapshotProcRoles(scheme);
    PackageLoadContextGuard loadGuard(packageId, role);
    s7_load(scheme, owned->path.string().c_str());
    stampProcRoles(scheme, before, role);
    return true;
}

bool AssetStore::loadMapSource(s7_scheme* scheme, std::string_view path, s7_cell* environment) {
    const auto resolved = vfs_.resolve(AssetKind::MapSource, path);
    if (!resolved) {
        return false;
    }

    if (environment != nullptr) {
        s7_load_with_environment(scheme, resolved->string().c_str(), environment);
    } else {
        s7_load(scheme, resolved->string().c_str());
    }
    return true;
}

bool AssetStore::loadMapCarved(s7_scheme* scheme, std::string_view path, s7_cell* environment) {
    const auto resolved = vfs_.resolve(AssetKind::MapCarved, path);
    if (!resolved) {
        return false;
    }

    if (environment != nullptr) {
        s7_load_with_environment(scheme, resolved->string().c_str(), environment);
    } else {
        s7_load(scheme, resolved->string().c_str());
    }
    return true;
}

bool AssetStore::loadMapThings(s7_scheme* scheme, std::string_view path, s7_cell* environment) {
    const auto resolved = vfs_.resolve(AssetKind::MapThings, path);
    if (!resolved) {
        return false;
    }

    if (environment != nullptr) {
        s7_load_with_environment(scheme, resolved->string().c_str(), environment);
    } else {
        s7_load(scheme, resolved->string().c_str());
    }
    return true;
}

bool AssetStore::loadMapGraphs(s7_scheme* scheme, std::string_view path, s7_cell* environment) {
    const auto resolved = vfs_.resolve(AssetKind::MapGraphs, path);
    if (!resolved) {
        return false;
    }

    if (environment != nullptr) {
        s7_load_with_environment(scheme, resolved->string().c_str(), environment);
    } else {
        s7_load(scheme, resolved->string().c_str());
    }
    return true;
}

bool AssetStore::loadData(s7_scheme* scheme, std::string_view path) {
    const auto owned = vfs_.resolveOwned(AssetKind::Data, path);
    if (!owned) {
        return false;
    }

    const std::string_view packageId =
        owned->package != nullptr ? std::string_view{owned->package->meta().id} : std::string_view{};
    const PackageRole role =
        owned->package != nullptr ? owned->package->role() : PackageRole::Base;
    const ProcRoleSnapshot before = snapshotProcRoles(scheme);
    PackageLoadContextGuard loadGuard(packageId, role);
    s7_load(scheme, owned->path.string().c_str());
    stampProcRoles(scheme, before, role);
    return true;
}

bool AssetStore::loadDataFromPackage(
    s7_scheme* scheme,
    std::string_view packageId,
    std::string_view path) {
    const auto owned = vfs_.resolveInPackage(AssetKind::Data, packageId, path);
    if (!owned) {
        return false;
    }

    const PackageRole role =
        owned->package != nullptr ? owned->package->role() : PackageRole::Base;
    const ProcRoleSnapshot before = snapshotProcRoles(scheme);
    PackageLoadContextGuard loadGuard(packageId, role);
    s7_load(scheme, owned->path.string().c_str());
    stampProcRoles(scheme, before, role);
    return true;
}

bool AssetStore::loadPrefabSource(s7_scheme* scheme, std::string_view path, s7_cell* environment) {
    const auto resolved = vfs_.resolve(AssetKind::PrefabSource, path);
    if (!resolved) {
        return false;
    }

    if (environment != nullptr) {
        s7_load_with_environment(scheme, resolved->string().c_str(), environment);
    } else {
        s7_load(scheme, resolved->string().c_str());
    }
    return true;
}

bool AssetStore::loadPrefabThings(s7_scheme* scheme, std::string_view path, s7_cell* environment) {
    const auto resolved = vfs_.resolve(AssetKind::PrefabThings, path);
    if (!resolved) {
        return false;
    }

    if (environment != nullptr) {
        s7_load_with_environment(scheme, resolved->string().c_str(), environment);
    } else {
        s7_load(scheme, resolved->string().c_str());
    }
    return true;
}

}
