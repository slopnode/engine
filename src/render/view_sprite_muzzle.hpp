#pragma once

#include <flecs.h>
#include <raylib.h>

#include <optional>

namespace slopengine {

std::optional<Vector3> resolveViewSpriteMuzzleWorld(
    flecs::world& world,
    flecs::entity host,
    float depth);

}
