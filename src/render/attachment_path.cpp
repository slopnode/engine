#include "render/attachment_path.hpp"

#include "assets/skeleton_loader.hpp"
#include "render/bone_attachment.hpp"
#include "render/components.hpp"

#include <algorithm>

namespace slopengine {

std::optional<AttachmentPath> splitAttachmentPath(
    std::string_view path,
    const std::function<bool(std::string_view)>& isAnchor) {
    if (path.empty()) {
        return std::nullopt;
    }
    std::size_t end = path.size();
    while (true) {
        const std::string_view prefix = path.substr(0, end);
        if (!prefix.empty() && isAnchor(prefix)) {
            AttachmentPath result;
            result.anchor = std::string(prefix);
            std::string_view rest = path.substr(end);
            while (!rest.empty()) {
                rest.remove_prefix(1);
                const std::size_t slash = rest.find('/');
                const std::string_view name = rest.substr(0, slash);
                if (name.empty()) {
                    return std::nullopt;
                }
                result.names.emplace_back(name);
                rest = slash == std::string_view::npos ? std::string_view{} : rest.substr(slash);
            }
            return result;
        }
        if (end == 0) {
            return std::nullopt;
        }
        const std::size_t slash = path.rfind('/', end - 1);
        if (slash == std::string_view::npos) {
            return std::nullopt;
        }
        end = slash;
    }
}

flecs::entity findAttachmentChild(flecs::entity host, std::string_view name) {
    flecs::entity found{};
    if (!host.is_valid()) {
        return found;
    }
    host.children([&](flecs::entity child) {
        if (!found.is_valid() && child.has<BoneAttachment>() && std::string_view(child.name().c_str()) == name) {
            found = child;
        }
    });
    return found;
}

flecs::entity descendAttachmentPath(flecs::entity host, const std::vector<std::string>& names) {
    flecs::entity cursor = host;
    for (const std::string& name : names) {
        cursor = findAttachmentChild(cursor, name);
        if (!cursor.is_valid()) {
            return {};
        }
    }
    return cursor;
}

std::string attachmentEventSource(flecs::entity entity) {
    std::vector<std::string> names;
    flecs::entity cursor = entity;
    while (cursor.is_valid() && cursor.has<BoneAttachment>()) {
        const char* name = cursor.name().c_str();
        if (name != nullptr && *name != '\0') {
            names.emplace_back(name);
        }
        cursor = cursor.parent();
    }
    std::string source;
    for (; cursor.is_valid(); cursor = cursor.parent()) {
        const char* name = cursor.name().c_str();
        if (name != nullptr && *name != '\0') {
            source = name;
            break;
        }
    }
    if (source.empty()) {
        return {};
    }
    std::reverse(names.begin(), names.end());
    for (const std::string& name : names) {
        source += '/';
        source += name;
    }
    return source;
}

void destroyAttachmentTree(flecs::entity entity) {
    if (!entity.is_valid()) {
        return;
    }
    std::vector<flecs::entity> children;
    entity.children([&](flecs::entity child) { children.push_back(child); });
    for (flecs::entity child : children) {
        destroyAttachmentTree(child);
    }
    if (entity.has<Model3D>()) {
        unloadClonedGeoModelInstance(entity.get_mut<Model3D>().model);
    }
    entity.destruct();
}

}
