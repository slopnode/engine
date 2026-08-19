#pragma once

#include <flecs.h>

#include <string>

namespace slopengine {

void registerUiModule(flecs::world& world, bool debugEnabled = false, std::string profile = "default");
void prepareUiInput(flecs::world world);
void drawUi(flecs::world world);

}