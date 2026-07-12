#pragma once

#include "render/components.hpp"

#include <flecs.h>

namespace slopengine {

/** Recomputes @p global from @p local and propagates the result to child entities. */
void updateTransform(flecs::entity entity, LocalTransformation& local, GlobalTransformation& global);

}
