#pragma once

#include <flecs.h>

#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

flecs::entity spawnPlayerBody(flecs::world& world, std::string_view name, int index);
void despawnPlayerBody(flecs::world& world, flecs::entity body);
void resetPlayerBody(flecs::world& world, flecs::entity body);
std::vector<std::string> playerBodyIds(const flecs::world& world);

}
