#pragma once

#include <flecs.h>
#include <s7.h>

namespace slopengine {

flecs::entity scriptPlayer(const flecs::world& world, s7_pointer args);
bool scriptPlayerFeet(const flecs::world& world, flecs::entity player, float& x, float& y, float& z);

}
