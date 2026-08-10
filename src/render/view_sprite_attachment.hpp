#pragma once

#include <flecs.h>
#include <raylib.h>

#include <optional>
#include <string>

namespace slopengine {

std::optional<Vector3> resolveViewSpriteAttachmentWorld(
    flecs::world& world,
    flecs::entity host,
    const std::string& attachName,
    float depth);

}
