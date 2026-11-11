#pragma once

#include <flecs.h>

struct s7_scheme;

namespace slopengine {

struct NetEventMessage;

void bindNetApi(flecs::world& world, s7_scheme* scheme);
void deliverNetEvent(s7_scheme* scheme, const NetEventMessage& event);

}
