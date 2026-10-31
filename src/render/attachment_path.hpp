#pragma once

#include <flecs.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

struct AttachmentPath {
    std::string anchor;
    std::vector<std::string> names;
};

std::optional<AttachmentPath> splitAttachmentPath(
    std::string_view path,
    const std::function<bool(std::string_view)>& isAnchor);

flecs::entity findAttachmentChild(flecs::entity host, std::string_view name);

flecs::entity descendAttachmentPath(flecs::entity host, const std::vector<std::string>& names);

std::string attachmentEventSource(flecs::entity entity);

void destroyAttachmentTree(flecs::entity entity);

}
