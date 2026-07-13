#pragma once

#include <flecs.h>

namespace slopengine {

void registerUiModule(flecs::world& world);
void prepareUiInput(flecs::world world);
void drawUi(flecs::world world);

}