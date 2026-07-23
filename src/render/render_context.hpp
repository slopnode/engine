#pragma once

#include "render/animation_player.hpp"
#include "render/components.hpp"

#include <flecs.h>

namespace slopengine {

struct RenderContext {
    flecs::query<Model3D, GlobalTransformation> worldModelQuery;
    flecs::query<Model3D, GlobalTransformation> viewModelQuery;
    flecs::query<SpriteInstance, GlobalTransformation> worldSpriteQuery;
    flecs::query<Model3D, GlobalTransformation, AnimationPlayer> animOverlayQuery;
};

struct PlayerEntity {
    flecs::entity entity{};
};

}
