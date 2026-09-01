#include "core/vfs.hpp"

#include <cstddef>
#include <fstream>
#include <unordered_set>
#include <vector>

namespace slopengine {

void VirtualFileSystem::setBasePackage(Package package) {
    packages_.clear();
    packages_.push_back(std::move(package));
}

void VirtualFileSystem::addPackage(Package package) {
    packages_.push_back(std::move(package));
}

const char* VirtualFileSystem::kindDirectory(AssetKind kind) {
    switch (kind) {
    case AssetKind::Texture: return "textures";
    case AssetKind::TextureAnim: return "textures";
    case AssetKind::Material: return "materials";
    case AssetKind::Mesh: return "meshes";
    case AssetKind::Shader: return "shaders";
    case AssetKind::Script: return "scripts";
    case AssetKind::Skeleton: return "skeletons";
    case AssetKind::SkeletonBind: return "skeletons";
    case AssetKind::Geo: return "geometry";
    case AssetKind::GeoVert: return "geometry";
    case AssetKind::GeoWeights: return "geometry";
    case AssetKind::Anim: return "animations";
    case AssetKind::AnimTracks: return "animations";
    case AssetKind::MapCsg: return "maps";
    case AssetKind::MapMeta: return "maps";
    case AssetKind::MapBsp: return "maps";
    case AssetKind::MapVis: return "maps";
    case AssetKind::MapNav: return "maps";
    case AssetKind::MapRad: return "maps";
    case AssetKind::MapLightmap: return "maps";
    case AssetKind::MapThings: return "maps";
    case AssetKind::MapGraphs: return "maps";
    case AssetKind::PrefabCsg: return "prefabs";
    case AssetKind::PrefabThings: return "prefabs";
    case AssetKind::Sprite: return "sprites";
    case AssetKind::SpriteAnim: return "sprites";
    case AssetKind::Icon: return "icons";
    case AssetKind::IconMap: return "icons";
    case AssetKind::Data: return "data";
    case AssetKind::Font: return "fonts";
    case AssetKind::Sound: return "sound";
    case AssetKind::SoundWav: return "sound";
    case AssetKind::Audio: return "audio";
    case AssetKind::AudioSaudio: return "audio";
    case AssetKind::Particle: return "particles";
    }
    return "";
}

const char* VirtualFileSystem::implicitExtension(AssetKind kind) {
    switch (kind) {
    case AssetKind::Texture: return ".png";
    case AssetKind::TextureAnim: return ".texanim";
    case AssetKind::Material: return ".mat";
    case AssetKind::Mesh: return ".glb";
    case AssetKind::Shader: return ".glsl";
    case AssetKind::Script: return ".s7";
    case AssetKind::Skeleton: return ".skel";
    case AssetKind::SkeletonBind: return ".bind";
    case AssetKind::Geo: return ".geo";
    case AssetKind::GeoVert: return ".vert";
    case AssetKind::GeoWeights: return ".weights";
    case AssetKind::Anim: return ".anim";
    case AssetKind::AnimTracks: return ".tracks";
    case AssetKind::MapCsg: return ".csg";
    case AssetKind::MapMeta: return ".meta";
    case AssetKind::MapBsp: return ".bsp";
    case AssetKind::MapVis: return ".vis";
    case AssetKind::MapNav: return ".nav";
    case AssetKind::MapRad: return ".rad";
    case AssetKind::MapLightmap: return ".png";
    case AssetKind::MapThings: return ".s7";
    case AssetKind::MapGraphs: return ".s7";
    case AssetKind::PrefabCsg: return ".csg";
    case AssetKind::PrefabThings: return ".s7";
    case AssetKind::Sprite: return ".spr";
    case AssetKind::SpriteAnim: return ".spanim";
    case AssetKind::Icon: return ".png";
    case AssetKind::IconMap: return ".iconmap";
    case AssetKind::Data: return ".s7";
    case AssetKind::Font: return ".ttf";
    case AssetKind::Sound: return ".ogg";
    case AssetKind::SoundWav: return ".wav";
    case AssetKind::Audio: return ".s7";
    case AssetKind::AudioSaudio: return ".saudio";
    case AssetKind::Particle: return ".prt";
    }
    return "";
}

std::filesystem::path VirtualFileSystem::normalizeVirtualPath(std::string_view virtualPath) {
    std::string normalized;
    normalized.reserve(virtualPath.size());

    for (char ch : virtualPath) {
        if (ch == '\\') {
            normalized.push_back('/');
        } else {
            normalized.push_back(ch);
        }
    }

    while (!normalized.empty() && normalized.front() == '/') {
        normalized.erase(normalized.begin());
    }

    return std::filesystem::path{normalized}.lexically_normal();
}

std::filesystem::path VirtualFileSystem::followSymlinks(const std::filesystem::path& path) {
    std::filesystem::path current = path;
    std::unordered_set<std::string> visited;

    for (int depth = 0; depth < 32; ++depth) {
        if (!std::filesystem::is_symlink(current)) {
            return current;
        }

        const std::string key = current.string();
        if (visited.contains(key)) {
            return {};
        }
        visited.insert(key);

        std::filesystem::path target = std::filesystem::read_symlink(current);
        if (target.is_relative()) {
            current = (current.parent_path() / target).lexically_normal();
        } else {
            current = std::move(target);
        }
    }

    return {};
}

bool VirtualFileSystem::exists(AssetKind kind, std::string_view virtualPath) const {
    return resolve(kind, virtualPath).has_value();
}

bool VirtualFileSystem::buildAssetRelative(
    AssetKind kind,
    std::string_view virtualPath,
    std::filesystem::path& outDirectory,
    std::string& outFilename) {
    const auto relative = normalizeVirtualPath(virtualPath);
    if (relative.empty() || relative.is_absolute()) {
        return false;
    }

    const auto assetPath = std::filesystem::path{kindDirectory(kind)} / relative;
    outFilename = assetPath.filename().string() + implicitExtension(kind);
    outDirectory = assetPath.parent_path();
    return true;
}

std::optional<ResolvedAsset> VirtualFileSystem::resolveOwned(
    AssetKind kind,
    std::string_view virtualPath) const {
    if (packages_.empty()) {
        return std::nullopt;
    }

    std::filesystem::path directory;
    std::string filename;
    if (!buildAssetRelative(kind, virtualPath, directory, filename)) {
        return std::nullopt;
    }

    for (auto it = packages_.rbegin(); it != packages_.rend(); ++it) {
        const auto candidate = it->root() / directory / filename;
        const auto resolved = followSymlinks(candidate);
        if (!resolved.empty() && std::filesystem::exists(resolved)) {
            return ResolvedAsset{resolved, &(*it)};
        }
    }

    return std::nullopt;
}

const Package* VirtualFileSystem::findPackage(std::string_view packageId) const {
    if (packageId.empty()) {
        return nullptr;
    }
    for (const Package& package : packages_) {
        if (package.meta().id == packageId) {
            return &package;
        }
    }
    return nullptr;
}

std::optional<ResolvedAsset> VirtualFileSystem::resolveInPackage(
    AssetKind kind,
    std::string_view packageId,
    std::string_view virtualPath) const {
    const Package* package = findPackage(packageId);
    if (package == nullptr) {
        return std::nullopt;
    }

    std::filesystem::path directory;
    std::string filename;
    if (!buildAssetRelative(kind, virtualPath, directory, filename)) {
        return std::nullopt;
    }

    const auto candidate = package->root() / directory / filename;
    const auto resolved = followSymlinks(candidate);
    if (!resolved.empty() && std::filesystem::exists(resolved)) {
        return ResolvedAsset{resolved, package};
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> VirtualFileSystem::resolve(
    AssetKind kind,
    std::string_view virtualPath) const {
    if (auto owned = resolveOwned(kind, virtualPath)) {
        return std::move(owned->path);
    }
    return std::nullopt;
}

std::string VirtualFileSystem::readText(AssetKind kind, std::string_view virtualPath) const {
    const auto bytes = readBinary(kind, virtualPath);
    return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::vector<std::byte> VirtualFileSystem::readBinary(AssetKind kind, std::string_view virtualPath) const {
    const auto path = resolve(kind, virtualPath);
    if (!path) {
        return {};
    }

    std::ifstream file{*path, std::ios::binary | std::ios::ate};
    if (!file) {
        return {};
    }

    const auto size = file.tellg();
    if (size <= 0) {
        return {};
    }

    std::vector<std::byte> buffer(static_cast<std::size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    if (!file) {
        return {};
    }

    return buffer;
}

}
