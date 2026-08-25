#pragma once

#include <flecs.h>

namespace slopengine {

struct InputContextStack;
struct InputState;

void registerInputModule(flecs::world& world);
void syncCursorCapture(const InputContextStack& contexts, InputState& input);

}