#pragma once

#include <flecs.h>
#include <raylib.h>

#include <optional>
#include <string>

namespace slopengine {

/**
 * Resolves a named (attach ...) point declared on @p host's current sprite frame to a
 * world-space position, using @p host's already-posed billboard geometry (position, facing,
 * texel scale) rather than a camera-ray projection — unlike
 * `resolveViewSpriteAttachmentWorld` (first-person view sockets), a world-space actor sprite
 * already has a real 3D quad each frame, so no depth guess is needed.
 */
std::optional<Vector3> resolveWorldSpriteAttachmentWorld(
    flecs::world& world,
    flecs::entity host,
    const std::string& attachName);

}
