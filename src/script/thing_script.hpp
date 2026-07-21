#pragma once

#include <flecs.h>

struct s7_scheme;

namespace slopengine {

void bindThingRuntimeApi(flecs::world& world, s7_scheme* scheme);

}
