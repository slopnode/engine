#pragma once

#include <flecs.h>

#include <string_view>

struct s7_scheme;

namespace slopengine {

void bindThingRuntimeApi(flecs::world& world, s7_scheme* scheme);
void queueThingDespawn(flecs::world& world, std::string_view id);

}
