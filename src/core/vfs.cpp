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
    case AssetKind::MapRad: return "maps";
    case AssetKind::MapLightmap: return "maps";
    case AssetKind::Sprite: return "sprites";
    case AssetKind::SpriteAnim: return "sprites";
    }
    return "";
}

const char* VirtualFileSystem::implicitExtension(AssetKind kind) {
    switch (kind) {
    case AssetKind::Texture: return ".png";
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
    case AssetKind::MapRad: return ".rad";
    case AssetKind::MapLightmap: return ".png";
    case AssetKind::Sprite: return ".spr";
    case AssetKind::SpriteAnim: return ".spanim";
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

std::optional<std::filesystem::path> VirtualFileSystem::resolve(AssetKind kind, std::string_view virtualPath) const {
    if (packages_.empty()) {
        return std::nullopt;
    }

    const auto relative = normalizeVirtualPath(virtualPath);
    if (relative.empty() || relative.is_absolute()) {
        return std::nullopt;
    }

    const auto assetPath = std::filesystem::path{kindDirectory(kind)} / relative;
    const auto filename = assetPath.filename().string() + implicitExtension(kind);
    const auto directory = assetPath.parent_path();

    for (auto it = packages_.rbegin(); it != packages_.rend(); ++it) {
        const auto candidate = it->root() / directory / filename;
        const auto resolved = followSymlinks(candidate);
        if (!resolved.empty() && std::filesystem::exists(resolved)) {
            return resolved;
        }
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
