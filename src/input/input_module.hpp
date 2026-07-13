#pragma once

#include <flecs.h>

namespace slopengine {

struct InputContextStack;

void registerInputModule(flecs::world& world);
void syncCursorCapture(const InputContextStack& contexts);

}