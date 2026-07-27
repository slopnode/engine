#pragma once

#include <flecs.h>

#include <string_view>

namespace slopengine {

void registerSightModule(flecs::world& world);

bool actorCanSee(flecs::world& world, std::string_view fromId, std::string_view toId);

}
