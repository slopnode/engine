#pragma once

#include "render/components.hpp"

#include <flecs.h>

namespace slopengine {

struct RenderContext {
    flecs::query<Model3D, GlobalTransformation> worldModelQuery;
    flecs::query<SpriteInstance, GlobalTransformation> worldSpriteQuery;
};

}
