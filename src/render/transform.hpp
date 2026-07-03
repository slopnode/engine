#pragma once

#include "render/components.hpp"

#include <flecs.h>

namespace daggerlike {

void updateTransform(flecs::entity entity, LocalTransformation& local, GlobalTransformation& global);

}
