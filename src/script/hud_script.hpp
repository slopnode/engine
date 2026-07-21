#pragma once

#include <flecs.h>

struct s7_scheme;

namespace slopengine {

void bindHudApi(flecs::world& world, s7_scheme* scheme);

}
